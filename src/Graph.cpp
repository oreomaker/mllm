//
// Created by 30500 on 2020/12/2 0002.
//
#include "Graph.hpp"
#include "OpDefined.hpp"
#include "Types.hpp"

std::string intToStringWithLeadingZero(int num) {
    if (num < 10) {
        return "0" + std::to_string(num);
    }
    return std::to_string(num);
}

namespace mllm {
// template class Graph;
// template class Graph;

/**
 * @brief 初始化
 * @param in_param
 */

Graph::Graph(const NetParameter &param, Backend *bn, unordered_map<string, shared_ptr<Tensor>> &external_tensors) {
    backend_ = bn;

    for (int i = 0; i < (int)param.net_tensors.size(); ++i) {
        auto *net_tensor = param.net_tensors[i];
        auto it = external_tensors.find(net_tensor->name);
        if (it == tensors_.end()) { // not in external_tensors
            tensors_[net_tensor->name] = std::make_shared<Tensor>(backend_);
            tensors_[net_tensor->name]->setName(net_tensor->name);
        }
    }
    for (int i = 0; i < (int)param.net_ops.size(); ++i) {
        auto *net_op = param.net_ops[i];
        shared_ptr<Op> my_op(NULL);
        auto *new_op = backend_->opCreate(net_op->param, net_op->name);
        my_op.reset(new_op);
        ops_[net_op->name] = my_op;
    }
    for (int i = 0; i < (int)param.net_ops.size(); ++i) {
        auto *net_op = param.net_ops[i];
        string lname = net_op->name;
        op_names_.push_back(lname);
        auto in_tensors = net_op->in;
        vector<shared_ptr<Tensor>> inTensors;
        for (auto *in_t : in_tensors) {
            auto in_t_name = in_t->name;
            auto it = tensors_.find(in_t_name);
            if (it != tensors_.end()) {
                inTensors.push_back(tensors_[in_t_name]);
            } else {
                inTensors.push_back(external_tensors[in_t_name]);
            }
        }
        vector<shared_ptr<Tensor>> outTensors;
        for (int oz = 0; oz < net_op->out_size; oz++) {
            auto out_t_name = "outtensor-" + lname + "-" + intToStringWithLeadingZero(oz);
            auto it = tensors_.find(out_t_name);
            if (it != tensors_.end()) {
                outTensors.push_back(tensors_[out_t_name]);
            } else {
                outTensors.push_back(external_tensors[out_t_name]);
            }
        }
        ops_input_tensors_[lname] = inTensors;
        ops_output_tensors_[lname] = outTensors;
    }
}


void Graph::reflashInput(unordered_map<string, shared_ptr<Tensor>> &external_tensors, string input_tensor_name){
    ops_input_tensors_[op_names_[0]].clear();
//    auto in_tensors = param_.net_ops[0]->in;
    //    vector<shared_ptr<Tensor>> inTensors;
//    for (auto *in_t : in_tensors)
    {
        auto in_t_name = input_tensor_name;
        auto it = tensors_.find(in_t_name);
        if (it != tensors_.end()) {
            ops_input_tensors_[op_names_[0]].push_back(tensors_[in_t_name]);
        } else {
            ops_input_tensors_[op_names_[0]].push_back(external_tensors[in_t_name]);
        }
    }
    //ops_input_tensors_[param_.net_ops[0]->name][0]->printData<float>();
    //std::cout << param_.net_ops[0]->name << std::endl;
    //    ops_input_tensors_[param_.net_ops[0]->name] = inTensors;
}
void Graph::reshape() {
    // RESHAPE
    for (int i = 0; i < (int)op_names_.size(); ++i) {
        string lname = op_names_[i];
        ops_[lname]->reshape(ops_input_tensors_[lname], ops_output_tensors_[lname]); // tensors_[lname]:1.reshape
    }
}

void Graph::setUpTensors() {
    auto &graph_in_tensors = ops_input_tensors_[op_names_[0]];
    this->backend_->onSetUpStart(graph_in_tensors);
    for (auto &t : graph_in_tensors) {
        t->alloc();
    }
    // set graph out tensor TensorType
    auto &graph_out_tensors = ops_output_tensors_[op_names_[op_names_.size() - 1]];
    for (auto &t : graph_out_tensors) {
        t->setTensorType(GRAPH_OUTPUT);
    }
    // set up tensors of ops
    for (int i = 0; i < (int)op_names_.size(); ++i) {
        std::cout << "EXE:: SetUpTensors " << op_names_[i] << std::endl;
        string lname = op_names_[i];
        std::cout << "EXE:: SetUpTensors " << op_names_[i] << std::endl;
        std::cout <<"======" <<ops_[lname]->name() << std::endl;
        ops_[lname]->setUp(ops_input_tensors_[lname], ops_output_tensors_[lname]);
        std::cout << "EXE:: SetUpTensors " << op_names_[i] << std::endl;
    }
}

void Graph::setUpOps(ParamLoader &loader) {
    for (int i = 0; i < (int)op_names_.size(); ++i) {
        string lname = op_names_[i];
        ops_[lname]->load(loader);
    }
}

//void Graph::reshapeOutputs() {
//    // RESHAPE
//    for (int i = 0; i < (int)param_.net_ops.size(); ++i) {
//        auto *net_op = param_.net_ops[i];
//        string lname = net_op->name;
//        ops_[lname]->reshapeOutputs(ops_input_tensors_[lname], ops_output_tensors_[lname]);
//    }
//}

//void Graph::setUp(unordered_map<string, shared_ptr<Tensor>> &external_tensors, bool init, bool reshape, bool graph0) {
//    if (init) {
//        std::cout << "EXE:: Init" << std::endl;
//        this->setUpTensors();
//    } else if (reshape) {
//        std::cout << "EXE:: Reshape" << std::endl;
//        if (graph0) {
//            this->reFlashInput(external_tensors);
//        }
//        this->reshapeOutputs();
//    }
//}


/**
 * @brief 前向传播
 * @param loss
 * @return
 */
// #define DEBUG
const vector<shared_ptr<Tensor>> &Graph::forward(bool autofree) {
    // TODO 改为递归
    // backend event hook
    this->backend_->onExecuteStart(ops_input_tensors_[op_names_[0]], ops_output_tensors_[op_names_[op_names_.size() - 1]]);
    for (int i = 0; i < (int)op_names_.size(); ++i) {
        string lname = op_names_[i];
#ifdef DEBUG
        uint64_t t_start = mllm_time_us();
#endif
        ops_[lname]->execute(ops_input_tensors_[lname], ops_output_tensors_[lname]);
// currently, when QNN is enabled, result will not write there
// TODO: better solution
#ifndef QNN_ENABLED
        for(auto &t: ops_output_tensors_[lname]){
            t->checkData<float>();
        }
#endif
#ifdef DEBUG
        uint64_t t_end = mllm_time_us();
        std::cout<<"\n ====  "<<lname<<" ====  "<< (t_end - t_start)/1000.0F << " ms" ;
#endif
        if(autofree){
            ops_[lname]->free(ops_input_tensors_[lname], ops_output_tensors_[lname]);
        }
    }
    // backend event hook
    this->backend_->onExecuteEnd();
    // TODO
    return ops_output_tensors_[op_names_[op_names_.size() - 1]];
}

//const vector<shared_ptr<Tensor>> &Graph::forward(const vector<shared_ptr<Tensor>> &inTensors) {
//    // Copy
//    // for (int i = 0; i < inTensors.size(); ++i) {
//    //     tensors_["Input0"][i]->CopyFrom(*inTensors[i]);
//    // }
//    return forward();
//}

void Graph::freeOps(){
    for (int i = 0; i < (int)op_names_.size(); ++i) {
        string lname = op_names_[i];
        ops_[lname]->free(ops_input_tensors_[lname], ops_output_tensors_[lname]);
    }
}
void Graph::freeTensors(){
    for(auto& t: tensors_){
        t.second->free();
    }
}
void Graph::free() {
    //TODO update
    freeOps();
    freeTensors();
}
} // namespace mllm
