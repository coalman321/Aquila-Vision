#pragma once

#include <NvInfer.h>
#include <cstdio>

namespace object_detector {

// To make ILogger happy
class Logger : public nvinfer1::ILogger {
    void log(nvinfer1::ILogger::Severity severity, const nvinfer1::AsciiChar *msg) noexcept {
        if (severity == nvinfer1::ILogger::Severity::kERROR) {
            printf("%s\n", msg);
        }
    }
};
}  // namespace object_detector
