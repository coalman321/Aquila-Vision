#pragma once

#include <filesystem>
#include <vector>

#include "object_detector_lib/detection.hpp"
#include "object_detector_lib/rfdetr_base.hpp"

namespace object_detector {

// RF-DETR boxes with a mask each. Needs a segmentation model, one with a `masks` output.
class RfDetrSegmenter : public RfDetrBase {
   public:
    RfDetrSegmenter(std::filesystem::path filepath, float conf_threshold);

    std::vector<Detection> runDetection(cv::Mat &image) override;

   private:
    std::vector<float> maskCopy_;  ///< Host copy of one query's mask
};

}  // namespace object_detector
