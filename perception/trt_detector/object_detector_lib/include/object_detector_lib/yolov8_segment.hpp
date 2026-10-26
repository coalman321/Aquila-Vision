#pragma once
#include <NvInfer.h>
#include <cuda_runtime.h>

#include <filesystem>

#include "object_detector_lib/detection.hpp"
#include "object_detector_lib/detector_common.hpp"
#include "object_detector_lib/detector_impl.hpp"

namespace object_detector {

class YoloV8Segmenter : public DetectorImpl, public DetectorCommon {
   public:
    YoloV8Segmenter(std::filesystem::path filepath, float conf_threshold, float iou_threshold);

    ~YoloV8Segmenter() override;

    int numClasses() override;

    std::vector<Detection> runDetection(cv::Mat &image) override;

   private:
    float *input, *output0, *output1, *output0Copy, *output1Copy, *maskWeights, *mask;
    int numClasses_;
    float confThreshold_, iouThreshold_;

    // Must be gpu pointers
    cv::Mat calculateMask(int iVal);
};

}  // namespace object_detector
