// End-to-end check of the GPU detector: renders a tag36h11 tag with apriltag's own renderer onto a
// synthetic image, runs AprilDetect on it and checks the decoded ID, the corner locations and the
// range from solvePnP. Skips unless there is a CUDA device whose architecture the build targets (see
// build:sm86 in .bazelrc for a desktop GPU).
#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>
#include <cuda_runtime.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "detector_lib/april_detector.hpp"
#include "tools/cuda_test/gpu_available.h"

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr int kTagId = 7;
constexpr int kScale = 20;  // Image pixels per tag cell.
constexpr int kOffsetX = 200;
constexpr int kOffsetY = 140;
constexpr double kFocalLength = 500.0;

// White image with tag kTagId drawn at kOffset, each tag cell kScale pixels wide. Returns the
// image-space corners of the tag's black square (the detector's corners) through corners.
std::vector<uint8_t> RenderTag(std::vector<cv::Point2d> *corners, int *black_width_px) {
    apriltag_family_t *family = tag36h11_create();
    image_u8_t *tag = apriltag_to_image(family, kTagId);

    std::vector<uint8_t> image(kWidth * kHeight, 255);
    for (int y = 0; y < tag->height * kScale; y++) {
        for (int x = 0; x < tag->width * kScale; x++) {
            image[(kOffsetY + y) * kWidth + kOffsetX + x] = tag->buf[(y / kScale) * tag->stride + x / kScale];
        }
    }

    // apriltag_to_image draws a one-cell white border around the width_at_border-cell black square.
    const int border = (family->total_width - family->width_at_border) / 2;
    const double lo_x = kOffsetX + border * kScale, hi_x = lo_x + family->width_at_border * kScale;
    const double lo_y = kOffsetY + border * kScale, hi_y = lo_y + family->width_at_border * kScale;
    *corners = { { lo_x, lo_y }, { hi_x, lo_y }, { hi_x, hi_y }, { lo_x, hi_y } };
    *black_width_px = family->width_at_border * kScale;

    image_u8_destroy(tag);
    tag36h11_destroy(family);
    return image;
}

landmark_detector::CameraConfig Camera() {
    landmark_detector::CameraConfig cfg;
    cfg.width = kWidth;
    cfg.height = kHeight;
    cfg.intrinsics = { kFocalLength, kWidth / 2.0, kFocalLength, kHeight / 2.0 };
    cfg.distortion = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    return cfg;
}

class AprilDetectorTest : public ::testing::Test {
   protected:
    void SetUp() override {
        std::string reason;
        if (!cuda_test::GpuAvailable(&reason)) {
            GTEST_SKIP() << reason;
        }
    }
};

TEST_F(AprilDetectorTest, DetectsRenderedTag) {
    std::vector<cv::Point2d> expected_corners;
    int black_width_px = 0;
    std::vector<uint8_t> image = RenderTag(&expected_corners, &black_width_px);

    // Single tag mode, with the tag's edge length chosen so the black square sits at 0.5 m.
    const double tag_size = 0.16;
    const double expected_range = kFocalLength * tag_size / black_width_px;
    landmark_detector::AprilDetect detector(Camera(), /*use_multitag=*/false);
    detector.addTagToCache(-1, Eigen::Isometry3d::Identity(), tag_size);

    std::vector<landmark_detector::AprilTagDetection> detections = detector.detectGrayHost(image.data());
    ASSERT_EQ(cudaGetLastError(), cudaSuccess);

    ASSERT_EQ(detections.size(), 1u);
    ASSERT_EQ(detections[0].tag_corners.size(), 1u);
    ASSERT_EQ(detections[0].tag_corners.count(kTagId), 1u);

    // Every rendered corner must have a detected corner within 2 px (in whichever order the detector
    // reports them).
    const std::vector<cv::Point2d> &corners = detections[0].tag_corners.at(kTagId);
    ASSERT_EQ(corners.size(), 4u);
    for (const cv::Point2d &expected : expected_corners) {
        double nearest = INFINITY;
        for (const cv::Point2d &corner : corners) {
            nearest = std::min(nearest, std::hypot(corner.x - expected.x, corner.y - expected.y));
        }
        EXPECT_LT(nearest, 2.0) << "no detected corner near (" << expected.x << ", " << expected.y << ")";
    }

    EXPECT_NEAR(detections[0].tag_pose.translation().norm(), expected_range, 0.01);
}

TEST_F(AprilDetectorTest, EmptyImageHasNoDetections) {
    std::vector<uint8_t> image(kWidth * kHeight, 255);
    landmark_detector::AprilDetect detector(Camera(), /*use_multitag=*/false);
    detector.addTagToCache(-1, Eigen::Isometry3d::Identity(), 0.16);

    EXPECT_TRUE(detector.detectGrayHost(image.data()).empty());
    EXPECT_EQ(cudaGetLastError(), cudaSuccess);
}

}  // namespace
