#include "object_detector_lib/detector.hpp"

#include "object_detector_lib/rfdetr_detect.hpp"
#include "object_detector_lib/rfdetr_segment.hpp"
#include "object_detector_lib/yolov8_detect.hpp"
#include "object_detector_lib/yolov8_segment.hpp"

namespace object_detector {
ObjectDetector::ObjectDetector(std::filesystem::path filepath, DetectionType type, float conf_threshold,
                               float iou_threshold, ModelArchitecture architecture) {
    if (type != DetectionType::SEGMENT && type != DetectionType::DETECT) {
        throw std::runtime_error("Uknown detector type");
    }
    const bool segment = type == DetectionType::SEGMENT;
    switch (architecture) {
        case ModelArchitecture::YOLOV8:
            if (segment) {
                det_ = std::make_unique<YoloV8Segmenter>(filepath, conf_threshold, iou_threshold);
            } else {
                det_ = std::make_unique<YoloV8Detector>(filepath, conf_threshold, iou_threshold);
            }
            return;
        case ModelArchitecture::RF_DETR:
            if (segment) {
                det_ = std::make_unique<RfDetrSegmenter>(filepath, conf_threshold);
            } else {
                det_ = std::make_unique<RfDetrDetector>(filepath, conf_threshold);
            }
            return;
    }
    throw std::runtime_error("Unknown model architecture");
}

ObjectDetector::~ObjectDetector() = default;

int ObjectDetector::numClasses() { return det_->numClasses(); }

std::vector<Detection> ObjectDetector::runDetection(cv::Mat &image) { return det_->runDetection(image); }
}  // namespace object_detector
