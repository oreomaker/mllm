#include "Layer.hpp"
#include "Module.hpp"
#include "Types.hpp"
#include "backends/xnnpack/XpWrapper.hpp"
#include "backends/xnnpack/Utils/Logger.hpp"
#include <gtest/gtest.h>

using namespace mllm;

struct XpLLaMAMHANameCfg {
    std::string _q_proj_name = "q_proj";
    std::string _k_proj_name = "k_proj";
    std::string _v_proj_name = "v_proj";
    std::string _o_proj_name = "o_proj";
};

class XpLLaMAMHA final : public Module {
    Layer q_proj;
    Layer k_proj;
    Layer v_proj;
    Layer q_rope;
    Layer k_rope;
    Layer k_cache;
    Layer v_cache;
    Layer o_proj;
    Layer mask;
    Layer softmax;

    int head_size_ = 0;
    int kv_head_size_ = 0;
    int attn_hidden_dim_ = 0;

public:
    XpLLaMAMHA() = default;

    XpLLaMAMHA(
        int hidden_dim,
        int head_size,
        int kv_head_size,
        int attn_hidden_dim,
        RoPEType RoPE_type,
        float rope_theta,
        int max_position_embeddings,
        int cache_limit,
        const XpLLaMAMHANameCfg &names,
        const string &base_name) {
        q_proj = Linear(hidden_dim, head_size * attn_hidden_dim, false, base_name + names._q_proj_name);
        k_proj = Linear(hidden_dim, kv_head_size * attn_hidden_dim, false, base_name + names._k_proj_name);
        v_proj = Linear(hidden_dim, kv_head_size * attn_hidden_dim, false, base_name + names._v_proj_name);

        q_rope = RoPE(RoPE_type, rope_theta, max_position_embeddings, base_name + "q_rope");
        k_rope = RoPE(RoPE_type, rope_theta, max_position_embeddings, base_name + "k_rope");

        k_cache = XP_KVCache(head_size / kv_head_size, cache_limit, base_name + "k_cache");
        v_cache = XP_KVCache(head_size / kv_head_size, cache_limit, base_name + "v_cache");

        o_proj = Linear(head_size * attn_hidden_dim, hidden_dim, false, base_name + names._o_proj_name);

        mask = Causalmask("mask");

        softmax = Softmax(DIMENSION, "softmax");

        head_size_ = head_size;
        kv_head_size_ = kv_head_size;
        attn_hidden_dim_ = attn_hidden_dim;
    }

    vector<Tensor>
    Forward(vector<Tensor> inputs, vector<std::any> args) override {
        // inputs is [B, S, H=1, D=dim]
        // Q, K, V is also [B, S, H=1, D=heads * dim]
        auto q = q_proj(inputs[0]);
        auto k = k_proj(inputs[0]);
        auto v = v_proj(inputs[0]);

        // [B, S, H=heads, D=dim]
        q = q.view(-1, head_size_, -1, attn_hidden_dim_);
        k = k.view(-1, kv_head_size_, -1, attn_hidden_dim_);
        v = v.view(-1, kv_head_size_, -1, attn_hidden_dim_);

        // [B, S, H=heads, D=dim]
        q = q_rope(q);
        k = k_rope(k);

        // [B, S-new, H=heads, D=dim]
        k = k_cache(k);
        v = v_cache(v);

        // TODO
        // shape maybe error
        auto qk = Tensor::mm(q, k.transpose(SEQUENCE, DIMENSION));
        qk = qk / std::sqrt(attn_hidden_dim_);
        qk = mask(qk);
        qk = softmax(qk);

        auto o = Tensor::mm(qk, v);
        o = o.view(-1, 1, -1, attn_hidden_dim_ * head_size_);
        o = o_proj(o);

        return {o};
    }
};

TEST(XpLLaMAMHATest, XpLLaMAMHA) {
    mllm::xnnpack::Log::log_level = mllm::xnnpack::Log::WARN;

    XpLLaMAMHANameCfg model_cfg;
    auto model = ::mllm::xnnpack::wrap2xnn<XpLLaMAMHA>(
        1,
        1,
        /*hidden_dim*/ 4096,
        /*head_size*/ 32,
        /*kv_headsize*/ 32,
        /*attn_headdim*/ 4096 / 32,
        /*rope*/ RoPEType::HFHUBROPE,
        /*rope_theta*/ 10000.0f,
        /*max_position_embeddings*/ 16384,
        /*cache_limit*/ 2048,
        model_cfg,
        "base-");
    model.setNoLoadWeightsDtype(DataType::MLLM_TYPE_F32);

    EXPECT_EQ(Backend::global_backends[MLLM_XNNPACK] != nullptr, true);

    Tensor x(1, 1, 16, 4096, Backend::global_backends[MLLM_XNNPACK], true);
    x.setTtype(TensorType::INPUT_TENSOR);

    auto start = std::chrono::high_resolution_clock::now();
    auto out_1 = model({x})[0];
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    mllm::xnnpack::Log::warn("XpLLaMAMHA 1, time={} microseconds", duration.count());

    out_1.printShape();
}
