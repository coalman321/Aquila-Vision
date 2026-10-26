#pragma once

#include <filesystem>
#include <vector>

#include "object_detector_lib/detection.hpp"
#include "object_detector_lib/rfdetr_base.hpp"

namespace object_detector {

// RF-DETR boxes. Takes a detection or a segmentation model; a segmentation model's masks are ignored.
class RfDetrDetector : public RfDetrBase {
   public:
    RfDetrDetector(std::filesystem::path filepath, float conf_threshold);

    std::vector<Detection> runDetection(cv::Mat &image) override;
};

}  // namespace object_detector
