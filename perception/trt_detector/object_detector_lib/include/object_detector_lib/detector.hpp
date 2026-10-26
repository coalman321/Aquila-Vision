#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "object_detector_lib/detection.hpp"
#include "object_detector_lib/detector_impl.hpp"

namespace object_detector {

enum DetectionType { SEGMENT, DETECT };

// The network family a model belongs to, which decides its input and output layout.
//  - YOLOV8: an Ultralytics export (`images` 1x3x640x640, `output0`, and `output1` for segmentation).
//    Boxes are filtered by conf_threshold, then per-class NMS at iou_threshold.
//  - RF_DETR: an rfdetr export (`dets`, `labels`, and `masks` for segmentation). Queries are scored per
//    class and filtered by conf_threshold; there's no NMS, so iou_threshold is unused. Class ids are the
//    model's label indices: for the COCO checkpoints those are COCO category ids, so numClasses() is 91.
enum class ModelArchitecture { YOLOV8, RF_DETR };

// Forward declarations for each detector
class YoloV8Detector;
class YoloV8Segmenter;
class RfDetrDetector;
class RfDetrSegmenter;

class ObjectDetector {
   public:
    // `filepath` is a .onnx model or a .trt engine. A .onnx is converted to a .trt engine next to it on first
    // use, and that engine is loaded instead on every later one.
    ObjectDetector(std::filesystem::path filepath, DetectionType type, float conf_threshold, float iou_threshold,
                   ModelArchitecture architecture = ModelArchitecture::YOLOV8);

    ~ObjectDetector();

    int numClasses();

    std::vector<Detection> runDetection(cv::Mat& image);

   private:
    std::unique_ptr<DetectorImpl> det_;
};
}  // namespace object_detector
