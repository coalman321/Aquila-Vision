// Runs a YOLOv8 or RF-DETR model on one image and writes a copy with the detections drawn on.
//   detect_image <model.onnx|model.trt> <input image> <output image> [detect|segment] [yolov8|rfdetr]

#include <chrono>
#include <cstdio>
#include <exception>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <vector>

#include "object_detector_lib/detector.hpp"

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 6) {
        std::fprintf(stderr,
                     "usage: %s <model.onnx|model.trt> <input image> <output image> [detect|segment] "
                     "[yolov8|rfdetr]\n",
                     argv[0]);
        return 2;
    }
    const std::string mode = argc >= 5 ? argv[4] : "detect";
    if (mode != "detect" && mode != "segment") {
        std::fprintf(stderr, "unknown mode '%s' (expected detect or segment)\n", mode.c_str());
        return 2;
    }
    const auto type = mode == "segment" ? object_detector::SEGMENT : object_detector::DETECT;
    const std::string arch = argc == 6 ? argv[5] : "yolov8";
    if (arch != "yolov8" && arch != "rfdetr") {
        std::fprintf(stderr, "unknown architecture '%s' (expected yolov8 or rfdetr)\n", arch.c_str());
        return 2;
    }
    const auto architecture =
        arch == "rfdetr" ? object_detector::ModelArchitecture::RF_DETR : object_detector::ModelArchitecture::YOLOV8;

    try {
        cv::Mat image = cv::imread(argv[2], cv::IMREAD_COLOR);
        if (image.empty()) {
            std::fprintf(stderr, "unable to read image %s\n", argv[2]);
            return 1;
        }

        object_detector::ObjectDetector detector(argv[1], type, 0.6, 0.6, architecture);

        // The first inference pays for CUDA/TensorRT warm-up; time the second.
        detector.runDetection(image);
        const auto start = std::chrono::steady_clock::now();
        std::vector<object_detector::Detection> detections = detector.runDetection(image);
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);

        std::printf("%zu detections in %.2f ms (%d classes)\n", detections.size(), elapsed.count(),
                    detector.numClasses());
        for (object_detector::Detection &detection : detections) {
            const cv::Rect box = detection.bbox();
            std::printf("  class %d conf %.3f box [%d, %d, %d x %d]\n", detection.classId(), detection.confidence(),
                        box.x, box.y, box.width, box.height);
            if (type == object_detector::SEGMENT) {
                cv::addWeighted(image, 1.0, detection.mask(), 0.4, 0, image);
            }
            cv::rectangle(image, box, cv::Scalar(0, 255, 0), 2);
            cv::putText(image, std::to_string(detection.classId()), box.tl(), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                        cv::Scalar(255, 0, 0), 2);
        }

        if (!cv::imwrite(argv[3], image)) {
            std::fprintf(stderr, "unable to write image %s\n", argv[3]);
            return 1;
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}
