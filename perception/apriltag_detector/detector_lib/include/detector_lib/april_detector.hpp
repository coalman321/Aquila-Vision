#pragma once

#include <apriltag/apriltag.h>
#include <Eigen/Geometry>
#include <map>
#include <opencv2/core.hpp>
#include <optional>
#include <vector>

#include <rclcpp/logging.hpp>

#include "detector_lib/detector_common.hpp"

namespace frc971::apriltag {
class GpuDetector;
}  // namespace frc971::apriltag

namespace landmark_detector {

struct AprilTagDetection {
    std::map<int, std::vector<cv::Point2d>> tag_corners;         ///< the information about each of the four tag corners
    Eigen::Isometry3d tag_pose = Eigen::Isometry3d::Identity();  ///< The 3d transform between the camera and tag
};

class Corners3d {
   public:
    Corners3d(Eigen::Isometry3d pose, double tag_size);

    std::vector<cv::Point3d> getCorners();

   private:
    std::vector<cv::Point3d> corners_;

    rclcpp::Logger logger_ = rclcpp::get_logger("april_detector.Corners3d");
};

struct CameraConfig {
    unsigned int width;
    unsigned int height;
    frc971::apriltag::CameraMatrix intrinsics;
    frc971::apriltag::DistCoeffs distortion;
};

class AprilDetect {
   public:
    /// @brief
    /// @param cfg
    AprilDetect(CameraConfig cfg, bool use_multitag = false);

    /// @brief Gets a list of tag detections in a given image
    /// @param image_ptr the image to detect on. should already be in GPU memory as a single channel gray image
    /// @return a list of apriltag detections in the image
    std::vector<AprilTagDetection> detectGray(void *image_ptr);

    /// @brief Gets a list of tag detections in a given image
    /// @param image_ptr the image to detect on
    /// @return a list of apriltag detections in the image
    std::vector<AprilTagDetection> detectGrayHost(void *image_ptr);

    /// @brief Gets a list of tag detections in a given image
    /// @param image_ptr the image to detect on. should already be in GPU memory as a RGB image
    /// @return a list of apriltag detections in the image
    std::vector<AprilTagDetection> detectColor(void *image_ptr);

    /// @brief Adds a tag to the internal cache of 3d corners
    /// @details for a non multitag detector, you must call this with a tag id of -1 to setup the single tag corners
    /// @param tag_id the ID of the tag
    /// @param pose the 3d pose of the tag in the world
    /// @param tag_size the size of the tag edge length
    void addTagToCache(int tag_id, Eigen::Isometry3d pose, double tag_size);

    /// @brief Removes a tag from the internal cache of 3d corners
    /// @param tag_id the ID of the tag to remove
    void removeTagFromCache(int tag_id);

    /// @brief Gets a list of currently cached tag IDs
    /// @return vector of cached tag IDs
    std::vector<int> getCachedTagIds();

   private:
    /// @brief Common detection processing
    /// @details The detector instance must have already been called to detect on an image before invoking detectCommon
    /// @return the list of apriltag detections
    std::vector<AprilTagDetection> detectCommon();

    /// @brief
    /// @param image_points
    /// @param tag_points
    /// @return
    std::optional<Eigen::Isometry3d> getTagTransform(const std::vector<cv::Point2d> &image_points,
                                                     const std::vector<cv::Point3d> &tag_points);

    std::vector<cv::Point2d> getImagePoints(const apriltag_detection_t *detection);

    std::shared_ptr<frc971::apriltag::GpuDetector> detector_;  ///< Handle used to interface with the detector library.

    CameraConfig cfg_;       ///< Camera configuration during construction
    cv::Mat dist_coeffs_;    ///< Camera distortion coefficients matrix for solvePNP
    cv::Mat intrinsic_mat_;  ///< Camera intrinsics matrix for solvePNP

    bool use_multitag_;  ///< Whether to use the multitag detector or not

    std::map<int, Corners3d> tag_corners_cache_;  ///< Cache of 3d tag corners for different tag IDs

    rclcpp::Logger logger_ = rclcpp::get_logger("april_detector.AprilDetect");
};
}  // namespace landmark_detector
