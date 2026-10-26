#include "object_detector_lib/rfdetr_detect.hpp"

namespace object_detector {

RfDetrDetector::RfDetrDetector(std::filesystem::path filepath, float conf_threshold) :
    RfDetrBase(filepath, conf_threshold) {}

std::vector<Detection> RfDetrDetector::runDetection(cv::Mat &image) {
    std::vector<Detection> detections;
    for (const Candidate &candidate : infer(image)) {
        detections.emplace_back(candidate.classId, candidate.score, bbox(candidate, image));
    }
    return detections;
}

}  // namespace object_detector
