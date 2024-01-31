
#include "QNNKVCache.hpp"
#include "QnnTypes.h"
#include "Types.hpp"
#include "QNNCommonOp.hpp"
#include <cstdint>
#include <memory>

namespace mllm {
QNNKVCache::QNNKVCache(Backend *bn, string opName, bool isK) :
    QNNCommonOp(bn, opName), isK_(isK) {
    cache_size_ = 1;
    dimension_size_ = 4096;
    seq_pos_cpu_ = 0;
    seq_pos_.setBackend(bn);
}

ErrorCode QNNKVCache::reshape(vector<shared_ptr<Tensor>> inputs, vector<shared_ptr<Tensor>> outputs) {
    assert(inputs.size() == 1);
    assert(outputs.size() == 1);

    outputs[0]->reshape(1, 1, cache_size_, dimension_size_);

    return Op::reshape(inputs, outputs);
}

ErrorCode QNNKVCache::setUp(vector<shared_ptr<Tensor>> inputs, vector<shared_ptr<Tensor>> outputs) {  
    qnnBackend_->pushInputBuffers(seq_pos_.hostPtr<uint8_t>());

    outputs[0]->setDtype(MLLM_TYPE_F32);
    outputs[0]->setBackend(qnnBackend_);
    outputs[0]->alloc();

    // output for net shape and QNN name index
    // cache_ for QNN shared buffer storage
    qnnBackend_->pushOutputBuffers(outputs[0]->hostPtr<uint8_t>());

    vector<Qnn_Param_t> paramsKVCache = {
        {.paramType = QNN_PARAMTYPE_SCALAR,
         .name = "hidden_dim",
         {.scalarParam = (Qnn_Scalar_t){QNN_DATATYPE_UINT_32, {.uint32Value = static_cast<uint32_t>(dimension_size_)}}}},
    };

    // add seq_pos tensor to qnn
    uint32_t dimensionsSeq[4] = {1, 1, 1, 1};
    qnnBackend_->modelAddTensor(seq_pos_.name(), (Qnn_Tensor_t){
                                                     .version = QNN_TENSOR_VERSION_1,
                                                     {.v1 = {
                                                          .id = 0,
                                                          .name = seq_pos_.name().c_str(),
                                                          .type = QNN_TENSOR_TYPE_APP_WRITE,
                                                          .dataFormat = QNN_TENSOR_DATA_FORMAT_FLAT_BUFFER,
                                                          .dataType = QNN_DATATYPE_UINT_32,
                                                          .quantizeParams = {QNN_DEFINITION_UNDEFINED,
                                                                             QNN_QUANTIZATION_ENCODING_UNDEFINED,
                                                                             {.scaleOffsetEncoding = {.scale = 0.0000000000000000f, .offset = 0}}},
                                                          .rank = 4,
                                                          .dimensions = dimensionsSeq,
                                                          .memType = QNN_TENSORMEMTYPE_RAW,
                                                          {.clientBuf = {.data = nullptr,
                                                                         .dataSize = 0}}}}});

    // add cache output tensor to qnn
    uint32_t dimensionsCache[4] = {static_cast<uint32_t>(outputs[0]->batch()),
                                   static_cast<uint32_t>(outputs[0]->sequence()),
                                   static_cast<uint32_t>(outputs[0]->head()),
                                   static_cast<uint32_t>(outputs[0]->dimension())};

    auto outName = outputs[0]->name();
    vector<Qnn_Tensor_t> kvcache_output = {{QNN_TENSOR_VERSION_1,
                                            {.v1 = {
                                                 .id = 0,
                                                 .name = outName.c_str(),
                                                 .type = QNN_TENSOR_TYPE_APP_READ,
                                                 .dataFormat = QNN_TENSOR_DATA_FORMAT_FLAT_BUFFER,
                                                 .dataType = QNN_DATATYPE_FLOAT_32,
                                                 .quantizeParams = {QNN_DEFINITION_UNDEFINED,
                                                                    QNN_QUANTIZATION_ENCODING_UNDEFINED,
                                                                    {.scaleOffsetEncoding = {.scale = 0.0000000000000000f, .offset = 0}}},
                                                 .rank = 4,
                                                 .dimensions = dimensionsCache,
                                                 .memType = QNN_TENSORMEMTYPE_RAW,
                                                 {.clientBuf = {.data = nullptr,
                                                                .dataSize = 0}}}}}};
    return graphAddNode(name() + ".kvcache", "KVCache", {inputs[0]->name(), seq_pos_.name()}, kvcache_output, paramsKVCache, "LLaMAPackage");
}

ErrorCode QNNKVCache::load(AbstructLoader &loader) {
    seq_pos_.setName(name() + ".seq_pos");
    seq_pos_.reshape(1, 1, 1, 1);
    seq_pos_.setDtype(MLLM_TYPE_I32);
    seq_pos_.setBackend(qnnBackend_);
    seq_pos_.alloc();

    seq_pos_.setDataAt<uint32_t>(0, 0, 0, 0, 0);
    return Op::load(loader);
}

ErrorCode QNNKVCache::free(vector<shared_ptr<Tensor>> inputs, vector<shared_ptr<Tensor>> outputs) {
    seq_pos_.free();
    outputs[0]->free();
    return MLLM_NO_ERROR;
}
} // namespace mllm
