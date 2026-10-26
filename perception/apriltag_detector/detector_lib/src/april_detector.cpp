#include "detector_lib/april_detector.hpp"

#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>

#include <Eigen/Core>
#include <Eigen/Dense>
#include <opencv2/calib3d.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/core/eigen.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <vector>

#include "detector_lib/971apriltag.hpp"

namespace landmark_detector {

// Forward function declarations

AprilDetect::AprilDetect(CameraConfig cfg, bool use_multitag) : cfg_(cfg), use_multitag_(use_multitag) {
    // Explicitly set CUDA device to avoid "invalid device ordinal" errors
    // This is critical for containerized environments and multi-GPU systems
    cudaSetDevice(0);
    cudaDeviceSynchronize();

    // Configure the detector settings
    apriltag_detector_t *tag_detector = apriltag_detector_create();
    apriltag_detector_add_family_bits(tag_detector, tag36h11_create(), 1);
    tag_detector->nthreads = 6;
    tag_detector->wp = workerpool_create(tag_detector->nthreads);
    tag_detector->qtp.min_white_black_diff = 5;
    tag_detector->quad_decimate = 2.0;
    tag_detector->debug = false;

    RCLCPP_DEBUG(logger_, "AprilTag detector created with image size %ux%u", cfg_.width, cfg_.height);

    // Now create the detector pipeline for the tags
    detector_ = std::make_shared<frc971::apriltag::GpuDetector>(cfg_.width, cfg_.height, tag_detector, cfg_.intrinsics,
                                                                cfg_.distortion);

    // setup the solvepnp matrices
    dist_coeffs_ = (cv::Mat_<double>(5, 1) << cfg_.distortion.k1, cfg_.distortion.k2, cfg_.distortion.p1,
                    cfg_.distortion.p2, cfg_.distortion.k3);
    intrinsic_mat_ = (cv::Mat_<double>(3, 3) << cfg_.intrinsics.fx, 0, cfg_.intrinsics.cx, 0, cfg_.intrinsics.fy,
                      cfg_.intrinsics.cy, 0, 0, 1);
}

std::vector<AprilTagDetection> AprilDetect::detectColor(void *image_ptr) {
    // Attempt tag detection
    detector_->DetectColor((unsigned char *)image_ptr);

    return detectCommon();
}

std::vector<AprilTagDetection> AprilDetect::detectGray(void *image_ptr) {
    // Attempt tag detection
    detector_->DetectGray2((unsigned char *)image_ptr);

    return detectCommon();
}

std::vector<AprilTagDetection> AprilDetect::detectGrayHost(void *image_ptr) {
    // Attempt tag detection
    detector_->DetectGrayHost((unsigned char *)image_ptr);

    return detectCommon();
}

std::vector<cv::Point2d> AprilDetect::getImagePoints(const apriltag_detection_t *detection) {
    RCLCPP_DEBUG(logger_, "Getting image points for tag ID %d", detection->id);
    std::vector<cv::Point2d> image_points;
    for (unsigned int corner_idx = 0; corner_idx < 4; corner_idx++) {
        image_points.emplace_back(cv::Point2d{ detection->p[corner_idx][0], detection->p[corner_idx][1] });
    }
    return image_points;
}

std::vector<AprilTagDetection> AprilDetect::detectCommon() {
    // Get the tag detections array
    const zarray_t *raw_detections = detector_->Detections();
    long unsigned int num_detections = zarray_size(raw_detections);

    // setup the output detections
    std::vector<AprilTagDetection> detections;

    if (!use_multitag_) {
        RCLCPP_DEBUG(logger_, "Running single tag detection for %lu tags", num_detections);

        // Iterate through each of the tags we detected to output their location
        detections.reserve(num_detections);
        for (long unsigned int i = 0; i < num_detections; ++i) {
            // Get the ith detection
            apriltag_detection_t *gpu_detection;
            zarray_get(raw_detections, i, &gpu_detection);

            // If we want optionally we can filter on hamming error here

            // Create the detection
            AprilTagDetection detection;

            // Work over the 4 points of the tag
            std::vector<cv::Point2d> tag_corners_2d_ = getImagePoints(gpu_detection);
            detection.tag_corners.emplace(gpu_detection->id, tag_corners_2d_);

            std::vector<cv::Point3d> tag_corners_3d_ = tag_corners_cache_.at(-1).getCorners();
            detection.tag_pose = getTagTransform(tag_corners_2d_, tag_corners_3d_);

            // Push the detection into the output data
            detections.push_back(detection);
        }
    } else {
        RCLCPP_DEBUG(logger_, "Running multitag detection for %lu tags", num_detections);

        // Sanity check to prevent memory corruption issues
        if (num_detections > 10000) {
            RCLCPP_ERROR(logger_, "Suspicious detection count %lu, possible memory corruption. Aborting.",
                         num_detections);
        }

        std::vector<cv::Point2d> image_points;
        std::vector<cv::Point3d> tag_points;
        AprilTagDetection detection;

        // reserve space for the number of detections times 4 corners
        image_points.reserve(num_detections * 4);
        tag_points.reserve(num_detections * 4);

        // use multitag detection
        for (long unsigned int i = 0; i < num_detections; ++i) {
            // Get the ith detection
            apriltag_detection_t *gpu_detection;
            zarray_get(raw_detections, i, &gpu_detection);

            RCLCPP_DEBUG(logger_, "Found tag ID %d in multitag detection", gpu_detection->id);

            // determine if we want to filter

            // determine if it's in the cache
            auto corner_cache_iter = tag_corners_cache_.find(gpu_detection->id);
            if (corner_cache_iter == tag_corners_cache_.end()) {
                // not found, skip it
                RCLCPP_INFO(logger_, "Tag ID %d not found in corner cache, skipping", gpu_detection->id);
                continue;
            }

            // get the image corners
            std::vector<cv::Point2d> detection_points = getImagePoints(gpu_detection);
            if (detection_points.size() != 4) {
                RCLCPP_ERROR(logger_, "Invalid detection points size %lu for tag %d, expected 4. Skipping.",
                             detection_points.size(), gpu_detection->id);
                continue;
            }
            detection.tag_corners.emplace(gpu_detection->id, detection_points);

            // add the corners to the lists
            std::vector<cv::Point3d> cached_corners = corner_cache_iter->second.getCorners();
            RCLCPP_DEBUG(logger_, "Adding ID %d to 3d multitag detection", gpu_detection->id);
            tag_points.insert(tag_points.end(), cached_corners.begin(), cached_corners.end());

            RCLCPP_DEBUG(logger_, "Adding ID %d to 2d multitag detection", gpu_detection->id);
            image_points.insert(image_points.end(), detection_points.begin(), detection_points.end());
        }

        // now solve for the multitag pose
        if (!tag_points.empty()) {
            RCLCPP_DEBUG(logger_, "Solving for multitag pose with %lu points", tag_points.size());
            Eigen::Isometry3d map_to_cam = getTagTransform(image_points, tag_points);

            // https://docs.opencv.org/4.x/d5/d1f/calib3d_solvePnP.html
            detection.tag_pose = map_to_cam.inverse();
        }

        detections.push_back(detection);
    }

    // Return the detections
    return detections;
}

Eigen::Isometry3d AprilDetect::getTagTransform(const std::vector<cv::Point2d> &image_points,
                                               const std::vector<cv::Point3d> &tag_points) {
    RCLCPP_DEBUG(logger_, "Solving PnP for %lu points", image_points.size());

    // print each correlated point pair
    for (size_t i = 0; i < image_points.size(); i++) {
        RCLCPP_DEBUG(logger_, "Point %lu: Image (%.2f, %.2f) Tag (%.2f, %.2f, %.2f)", i, image_points[i].x,
                     image_points[i].y, tag_points[i].x, tag_points[i].y, tag_points[i].z);
    }

    // Solve for the camera to tag transform
    cv::Mat rot_mat(3, 3, cv::DataType<double>::type, 0.0);
    cv::Mat r_vec(3, 1, cv::DataType<double>::type, 0.0);
    cv::Mat t_vec(3, 1, cv::DataType<double>::type, 0.0);
    cv::solvePnP(tag_points, image_points, intrinsic_mat_, dist_coeffs_, r_vec, t_vec, false, cv::SOLVEPNP_SQPNP);
    // cv::solvePnP(tag_points, image_points, intrinsic_mat_, dist_coeffs_, r_vec,
    // t_vec, false, cv::SOLVEPNP_IPPE_SQUARE);
    cv::Rodrigues(r_vec, rot_mat);

    // convert to eigen
    Eigen::Matrix3d eigen_rot;
    cv2eigen(rot_mat, eigen_rot);
    Eigen::Vector3d eigen_translation;
    cv2eigen(t_vec, eigen_translation);

    // Create the transform
    Eigen::Isometry3d cam_to_tag;
    cam_to_tag.setIdentity();
    cam_to_tag.matrix().block<3, 3>(0, 0) = eigen_rot;
    cam_to_tag.matrix().block<3, 1>(0, 3) = eigen_translation;

    // print the final transform
    RCLCPP_DEBUG(
        logger_,
        "PnP result: Rotation:\n%.2f %.2f %.2f\n%.2f %.2f %.2f\n%.2f %.2f %.2f\nTranslation: (%.2f, %.2f, %.2f)",
        cam_to_tag.rotation().matrix()(0, 0), cam_to_tag.rotation().matrix()(0, 1),
        cam_to_tag.rotation().matrix()(0, 2), cam_to_tag.rotation().matrix()(1, 0),
        cam_to_tag.rotation().matrix()(1, 1), cam_to_tag.rotation().matrix()(1, 2),
        cam_to_tag.rotation().matrix()(2, 0), cam_to_tag.rotation().matrix()(2, 1),
        cam_to_tag.rotation().matrix()(2, 2), cam_to_tag.translation().x(), cam_to_tag.translation().y(),
        cam_to_tag.translation().z());

    return cam_to_tag;
}

void AprilDetect::addTagToCache(int tag_id, Eigen::Isometry3d pose, double tag_size) {
    RCLCPP_DEBUG(logger_, "Adding tag ID %d to corner cache", tag_id);
    tag_corners_cache_.emplace(tag_id, Corners3d(pose, tag_size));
}

void AprilDetect::removeTagFromCache(int tag_id) { tag_corners_cache_.erase(tag_id); }

std::vector<int> AprilDetect::getCachedTagIds() {
    std::vector<int> tag_ids;
    for (const auto &entry : tag_corners_cache_) {
        tag_ids.push_back(entry.first);
    }
    return tag_ids;
}

Corners3d::Corners3d(Eigen::Isometry3d pose, double tag_size) {
    const double half_size = tag_size / 2.0;

    // order the algorithm expects the corners in: bottom left, bottom right, top right, top left
    std::vector<Eigen::Vector3d> corners;
    corners.push_back(Eigen::Vector3d(0.0, -half_size, -half_size));
    corners.push_back(Eigen::Vector3d(0.0, half_size, -half_size));
    corners.push_back(Eigen::Vector3d(0.0, half_size, half_size));
    corners.push_back(Eigen::Vector3d(0.0, -half_size, half_size));

    corners_.clear();
    corners_.reserve(corners.size());

    for (const auto &corner : corners) {
        Eigen::Vector3d transformed_corner = pose * corner;
        corners_.emplace_back(transformed_corner.x(), transformed_corner.y(), transformed_corner.z());

        RCLCPP_DEBUG(logger_, "Transformed corner: (%.2f, %.2f, %.2f)", transformed_corner.x(), transformed_corner.y(),
                     transformed_corner.z());
    }
}

std::vector<cv::Point3d> Corners3d::getCorners() { return corners_; }

}  // namespace landmark_detector
