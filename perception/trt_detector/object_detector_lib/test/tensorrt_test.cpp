#include <NvInfer.h>
#include <NvOnnxParser.h>
#include <cuda_runtime.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <memory>
#include <string>

#include "tools/cuda_test/gpu_available.h"

namespace {

class Logger : public nvinfer1::ILogger {
    void log(Severity severity, const nvinfer1::AsciiChar* msg) noexcept override {
        if (severity <= Severity::kWARNING) std::printf("[TensorRT] %s\n", msg);
    }
};

class TensorRTTest : public testing::Test {
   protected:
    void SetUp() override {
        std::string reason;
        if (!cuda_test::GpuAvailable(&reason)) GTEST_SKIP() << reason;
    }

    Logger logger_;
};

TEST_F(TensorRTTest, BuildsAndRunsANetwork) {
    std::unique_ptr<nvinfer1::IBuilder> builder(nvinfer1::createInferBuilder(logger_));
    ASSERT_NE(builder, nullptr);
    std::unique_ptr<nvinfer1::INetworkDefinition> network(builder->createNetworkV2(0));
    ASSERT_NE(network, nullptr);

    // out = relu(in)
    nvinfer1::ITensor* input = network->addInput("in", nvinfer1::DataType::kFLOAT, nvinfer1::Dims4(1, 1, 2, 2));
    ASSERT_NE(input, nullptr);
    nvinfer1::IActivationLayer* relu = network->addActivation(*input, nvinfer1::ActivationType::kRELU);
    ASSERT_NE(relu, nullptr);
    relu->getOutput(0)->setName("out");
    network->markOutput(*relu->getOutput(0));

    // The ONNX parser is a second library; it only has to attach to the network.
    std::unique_ptr<nvonnxparser::IParser> parser(nvonnxparser::createParser(*network, logger_));
    EXPECT_NE(parser, nullptr);

    std::unique_ptr<nvinfer1::IBuilderConfig> config(builder->createBuilderConfig());
    std::unique_ptr<nvinfer1::IHostMemory> plan(builder->buildSerializedNetwork(*network, *config));
    ASSERT_NE(plan, nullptr);

    std::unique_ptr<nvinfer1::IRuntime> runtime(nvinfer1::createInferRuntime(logger_));
    ASSERT_NE(runtime, nullptr);
    std::unique_ptr<nvinfer1::ICudaEngine> engine(runtime->deserializeCudaEngine(plan->data(), plan->size()));
    ASSERT_NE(engine, nullptr);
    std::unique_ptr<nvinfer1::IExecutionContext> context(engine->createExecutionContext());
    ASSERT_NE(context, nullptr);

    const std::array<float, 4> in_host{ -1.0f, 2.0f, -3.0f, 4.0f };
    std::array<float, 4> out_host{};
    float* in_dev = nullptr;
    float* out_dev = nullptr;
    ASSERT_EQ(cudaMalloc(&in_dev, sizeof(in_host)), cudaSuccess);
    ASSERT_EQ(cudaMalloc(&out_dev, sizeof(out_host)), cudaSuccess);
    cudaStream_t stream;
    ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);

    ASSERT_EQ(cudaMemcpy(in_dev, in_host.data(), sizeof(in_host), cudaMemcpyHostToDevice), cudaSuccess);
    ASSERT_TRUE(context->setTensorAddress("in", in_dev));
    ASSERT_TRUE(context->setTensorAddress("out", out_dev));
    ASSERT_TRUE(context->enqueueV3(stream));
    ASSERT_EQ(cudaStreamSynchronize(stream), cudaSuccess);
    ASSERT_EQ(cudaMemcpy(out_host.data(), out_dev, sizeof(out_host), cudaMemcpyDeviceToHost), cudaSuccess);

    EXPECT_EQ(out_host, (std::array<float, 4>{ 0.0f, 2.0f, 0.0f, 4.0f }));

    cudaStreamDestroy(stream);
    cudaFree(in_dev);
    cudaFree(out_dev);
}

}  // namespace
