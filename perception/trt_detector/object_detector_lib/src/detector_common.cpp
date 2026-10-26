#include <NvOnnxParser.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "object_detector_lib/detector_common.hpp"
#include "object_detector_lib/nvinfer_logger.hpp"

namespace object_detector {
DetectorCommon::~DetectorCommon() {
    // Reverse order of creation: the context belongs to the engine, which belongs to the runtime.
    delete context;
    delete engine_;
    delete runtime;
}

std::filesystem::path DetectorCommon::getEnginePath(std::filesystem::path network_path) {
    if (!std::filesystem::exists(network_path)) {
        std::string errorMsg = "File " + network_path.string() + " does not exist";
        throw std::runtime_error(errorMsg);
    }

    // figure out the model extension and the engine file path
    std::string network_ext = network_path.extension().string();
    std::filesystem::path engine_path = std::filesystem::path(network_path.string());
    engine_path = engine_path.replace_extension("trt");
    
    if (network_ext == ".onnx" && !std::filesystem::exists(engine_path)) {
        // Convert
        std::unique_ptr<nvinfer1::IBuilder> builder(nvinfer1::createInferBuilder(logger_));
        if (!builder) {
            throw std::runtime_error("Failed to create builder");
        }

        // Networks are always explicit batch since TensorRT 10, so no flags.
        std::unique_ptr<nvinfer1::INetworkDefinition> network(builder->createNetworkV2(0));
        if (!network) {
            throw std::runtime_error("Failed to build network");
        }

        std::cout << "Loading ONNX model" << std::endl;

        std::ifstream file(network_path, std::ios::binary | std::ios::ate);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) {
            throw std::runtime_error("Error, unable to read onnx file");
        }
        file.close();

        auto parser = std::unique_ptr<nvonnxparser::IParser>(nvonnxparser::createParser(*network, logger_));
        if (!parser) {
            throw std::runtime_error("Failed to create parser");
        }

        auto parsed = parser->parseFromFile(network_path.c_str(), 0);
        if (!parsed) {
            throw std::runtime_error("Failed to parse onnx file");
        }

        std::unique_ptr<nvinfer1::IBuilderConfig> config(builder->createBuilderConfig());
        if (!config) {
            throw std::runtime_error("Failed to build config");
        }

        // Has to be raw or else it won't compile
        nvinfer1::IOptimizationProfile *optProfile = builder->createOptimizationProfile();
        const auto input = network->getInput(0);
        const auto inputName = input->getName();
        const auto inputDims = input->getDimensions();
        int32_t inputC = inputDims.d[1];
        int32_t inputH = inputDims.d[2];
        int32_t inputW = inputDims.d[3];

        optProfile->setDimensions(inputName, nvinfer1::OptProfileSelector::kMIN,
                                  nvinfer1::Dims4(1, inputC, inputH, inputW));
        optProfile->setDimensions(inputName, nvinfer1::OptProfileSelector::kOPT,
                                  nvinfer1::Dims4(1, inputC, inputH, inputW));
        optProfile->setDimensions(inputName, nvinfer1::OptProfileSelector::kMAX,
                                  nvinfer1::Dims4(1, inputC, inputH, inputW));

        config->addOptimizationProfile(optProfile);

        std::cout << "Optimizing engine... This may take a few minutes..." << std::endl;

        std::unique_ptr<nvinfer1::IHostMemory> plan(builder->buildSerializedNetwork(*network, *config));
        if (!plan) {
            throw std::runtime_error("Failed to build engine");
        }

        std::cout << "Engine optimization complete" << std::endl;

        std::ofstream outfile(engine_path.c_str(), std::ofstream::binary);
        outfile.write(reinterpret_cast<const char *>(plan->data()), plan->size());
        outfile.close();
    } else if (network_ext != ".trt" && network_ext != ".onnx"){
        throw std::runtime_error("Network file " + network_path.string() + " invalid");
    } else {
        // do nothing here 
    }

    return engine_path;
}

void DetectorCommon::setupEngine(std::filesystem::path engine_path) {
    std::ifstream engineFile(engine_path.c_str(), std::ios::binary | std::ios::ate);
    if (!engineFile) {
        throw std::runtime_error("Unable to open engine file " + engine_path.string());
    }
    std::streamsize engineFileSize = engineFile.tellg();
    engineFile.seekg(0, std::ios::beg);
    std::vector<char> engineFileBuffer(engineFileSize);
    if (!engineFile.read(engineFileBuffer.data(), engineFileSize)) {
        throw std::runtime_error("Error, unable to read engine file");
    }

    runtime = nvinfer1::createInferRuntime(logger_);
    if (!runtime) {
        throw std::runtime_error("Failed to create runtime");
    }
    engine_ = runtime->deserializeCudaEngine(engineFileBuffer.data(), engineFileBuffer.size());
    if (!engine_) {
        throw std::runtime_error("Failed to create engine_");
    }
    context = engine_->createExecutionContext();
    if (!context) {
        throw std::runtime_error("Failed to create execution context");
    }

    cudaError_t ret = cudaStreamCreate(&contextStream);
    if (ret != cudaSuccess) {
        throw std::runtime_error("Failed to create context stream: " + std::string(cudaGetErrorName(ret)) + '\n' +
                                 std::string(cudaGetErrorString(ret)));
    }
}
}  // namespace object_detector
