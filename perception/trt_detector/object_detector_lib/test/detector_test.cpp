#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <opencv2/core.hpp>
#include <stdexcept>

#include "object_detector_lib/detection.hpp"
#include "object_detector_lib/detector.hpp"

namespace object_detector {
namespace {

TEST(DetectionTest, BoxDetectionHasNoMask) {
    Detection detection(3, 0.75f, cv::Rect(1, 2, 30, 40));
    EXPECT_EQ(detection.classId(), 3);
    EXPECT_FLOAT_EQ(detection.confidence(), 0.75f);
    EXPECT_EQ(detection.bbox(), cv::Rect(1, 2, 30, 40));
    EXPECT_THROW(detection.mask(), std::runtime_error);
}

TEST(DetectionTest, SegmentationKeepsItsMask) {
    cv::Mat mask = cv::Mat::ones(4, 4, CV_8UC3);
    Detection detection(0, 0.5f, cv::Rect(0, 0, 4, 4), mask);
    EXPECT_EQ(detection.mask().data, mask.data);
}

// Both of these fail while resolving the model path, before CUDA or TensorRT is touched.
TEST(ObjectDetectorTest, RejectsMissingModel) {
    const std::filesystem::path missing = std::filesystem::path(testing::TempDir()) / "missing.onnx";
    EXPECT_THROW(ObjectDetector(missing, DETECT, 0.5f, 0.5f), std::runtime_error);
    EXPECT_THROW(ObjectDetector(missing, SEGMENT, 0.5f, 0.5f), std::runtime_error);
    EXPECT_THROW(ObjectDetector(missing, DETECT, 0.5f, 0.5f, ModelArchitecture::RF_DETR), std::runtime_error);
}

TEST(ObjectDetectorTest, RejectsUnknownModelFormat) {
    const std::filesystem::path model = std::filesystem::path(testing::TempDir()) / "model.pt";
    std::ofstream(model) << "not a model";
    EXPECT_THROW(ObjectDetector(model, DETECT, 0.5f, 0.5f), std::runtime_error);
    EXPECT_THROW(ObjectDetector(model, DETECT, 0.5f, 0.5f, ModelArchitecture::RF_DETR), std::runtime_error);
}

}  // namespace
}  // namespace object_detector
