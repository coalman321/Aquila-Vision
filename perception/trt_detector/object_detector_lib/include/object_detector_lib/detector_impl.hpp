#pragma once

#include <vector>

#include "object_detector_lib/detection.hpp"

/**
 * This file must be clean from all CUDA implementation details. This is how we keep the CUDA dependency out of the
 * other executables that use this library.
 */

namespace object_detector {

class DetectorImpl {
   public:
    virtual ~DetectorImpl() {}

    virtual int numClasses() = 0;

    virtual std::vector<Detection> runDetection(cv::Mat &image) = 0;
};

}  // namespace object_detector
