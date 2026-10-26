#pragma once

#include <filesystem>

#include "object_detector_lib/detection.hpp"
#include "object_detector_lib/detector_impl.hpp"
#include "object_detector_lib/detector_common.hpp"

namespace object_detector {

class YoloV8Detector : public DetectorImpl, public DetectorCommon {
   public:
    YoloV8Detector(std::filesystem::path filepath, float conf_threshold, float iou_threshold);

    ~YoloV8Detector() override;

    int numClasses() override;

    std::vector<Detection> runDetection(cv::Mat &image) override;

   private:
    float *input, *output0, *output0Copy;
    int numClasses_;
    float confThreshold_, iouThreshold_;
};

}  // namespace object_detector
