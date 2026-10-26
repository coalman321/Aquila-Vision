#pragma once

#include <NvInfer.h>
#include <cuda_runtime.h>

#include <filesystem>

#include "object_detector_lib/nvinfer_logger.hpp"

namespace object_detector {
class DetectorCommon {
   public:
    virtual ~DetectorCommon();

   protected:
    std::filesystem::path getEnginePath(std::filesystem::path network_path);

    void setupEngine(std::filesystem::path engine_path);

    nvinfer1::IRuntime *runtime{ nullptr };
    nvinfer1::IExecutionContext *context{ nullptr };
    cudaStream_t contextStream{ nullptr };
    nvinfer1::ICudaEngine *engine_{ nullptr };
    Logger logger_;
};
}  // namespace object_detector
