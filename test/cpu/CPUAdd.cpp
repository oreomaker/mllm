//
// Created by 咸的鱼 on 2023/10/14.
//

#include "CPUTest.hpp"
#include "backends/cpu/CPUAdd.hpp"
#include "TestLoader.hpp"
#define SETUP_OP(type_)                     \
    auto op = new type_(bn_, #type_, false); \
    auto loader = TestLoader(::testing::UnitTest::GetInstance()->current_test_info()->name())
#define TENSOR(name_) auto name_ = std::make_shared<Tensor>(bn_); name_->setName(#name_);
using namespace mllm;
TEST_F(CPUTest, CPUAdd1) {
    SETUP_OP(CPUAdd);
    TENSOR(input0);
    TENSOR(input1);
    TENSOR(output);
    input0->reshape({2, 2, 1, 1});
    input1->reshape({2, 2, 1, 1});
    op->reshape({input0, input1}, {output});
    EXPECT_EQ(output->shape(0), 2);
    EXPECT_EQ(output->shape(1), 2);
    EXPECT_EQ(output->shape(2), 1);
    EXPECT_EQ(output->shape(3), 1);
    op->setUp({input0, input1}, {output});
    loader.load(input0);
    loader.load(input1);
    input0->printData<float>();
    input1->printData<float>();
    op->execute({input0, input1}, {output});
    //TODO: check output?
    Tensor *torch_output = new Tensor(bn_);
    torch_output->setName("output");
    loader.load(torch_output);
    EXPECT_TRUE(isSame(output.get(), torch_output));
}
