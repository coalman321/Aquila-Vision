#include "landmark_detector_ros/detector_node.hpp"

#include <tf2/exceptions.hpp>

#include <chrono>
#include <cv_bridge/cv_bridge.hpp>
#include <detector_lib/april_detector.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <set>

#include "basin/node_core/node_monitor.hpp"

using namespace std::placeholders;

namespace landmark_detector {

DetectorNode::DetectorNode() : basin::node_core::NodeCore("detector_node") {
    RCLCPP_DEBUG(get_logger(), "Loading parameters");
    params_ = std::make_unique<DetectorNodeParams>(this);
    RCLCPP_INFO(get_logger(), "Parameters loaded: camera_topic=%s enabled=%d is_multitag=%d tag_edge_length=%f",
                params_->camera_topic.c_str(), params_->enabled, params_->is_multitag, params_->tag_edge_length);

    // Initialize TF2 listener
    RCLCPP_DEBUG(get_logger(), "Creating TF listener");
    tf_buffer_ = std::make_shared<tf2_ros::Buffer>(this->get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

    // Verify the detection covariance
    try {
        std::copy(params_->detect_covar.begin(), params_->detect_covar.end(), detection_covar_.begin());
    } catch (const std::out_of_range &e) {
        RCLCPP_FATAL(get_logger(), "Detection covariance must not exceed length 36");
        throw std::runtime_error("Detection covariance must not exceed length 36");
    }

    std::string node_name = get_name();

    // Setup the detection publisher
    RCLCPP_DEBUG(get_logger(), "Creating publishers");
    detection_pub_ =
        create_publisher<vision_msgs::msg::Detection3DArray>(node_name + "/detections", rclcpp::SensorDataQoS());

    tag_solution_pub_ =
        create_publisher<localization_msgs::msg::TagSolution>(node_name + "/tag_solution", rclcpp::SensorDataQoS());

    // Publisher for multitag pose when running in multitag mode
    multitag_pose_pub_ = create_publisher<geometry_msgs::msg::PoseStamped>(node_name + "/multitag_pose",
                                                                          rclcpp::SensorDataQoS());

    // Setup the camera subscription
    // QoS(10) is the reliable, keep-last-10 rmw_qos_profile_default the node-pointer overloads used.
    image_transport::TransportHints hints(*this);
    RCLCPP_DEBUG(get_logger(), "Subscribing to camera %s with transport %s", params_->camera_topic.c_str(),
                 hints.getTransport().c_str());
    camera_subscriber_ = image_transport::create_camera_subscription(
        *this, params_->camera_topic, std::bind(&DetectorNode::detectTags, this, _1, _2), hints.getTransport(),
        rclcpp::QoS(10));
    RCLCPP_DEBUG(get_logger(), "Camera subscription created on %s", camera_subscriber_.getTopic().c_str());

    // Setup the debug publisher
    RCLCPP_DEBUG(get_logger(), "Creating debug image publisher");
    debug_image_pub_ = image_transport::create_publisher(*this, node_name + "/debug_image", rclcpp::QoS(10));

    RCLCPP_INFO(get_logger(), "Detector loaded, waiting for image data");
}

void DetectorNode::initDetector(sensor_msgs::msg::CameraInfo::ConstSharedPtr info) {
    landmark_detector::CameraConfig intrinsics_cfg;
    intrinsics_cfg.width = info->width;
    intrinsics_cfg.height = info->height;
    intrinsics_cfg.distortion = { info->d.at(0), info->d.at(1), info->d.at(2), info->d.at(3), info->d.at(4) };
    intrinsics_cfg.intrinsics = { info->k.at(0), info->k.at(2), info->k.at(4), info->k.at(5) };
    detector_ = std::make_unique<landmark_detector::AprilDetect>(intrinsics_cfg, params_->tag_edge_length);

    if (params_->is_multitag) {
        RCLCPP_INFO(get_logger(), "Initializing multitag detector with edge length: %f", params_->tag_edge_length);
    } else {
        RCLCPP_INFO(get_logger(), "Initializing single tag detector with edge length: %f", params_->tag_edge_length);
        detector_->addTagToCache(-1, Eigen::Isometry3d::Identity(), params_->tag_edge_length);
    }

    RCLCPP_INFO(get_logger(), "Initialized GPU Apriltag detector");
}

void DetectorNode::detectTags(sensor_msgs::msg::Image::ConstSharedPtr image,
                              sensor_msgs::msg::CameraInfo::ConstSharedPtr info) {
    images_received_++;
    if (images_received_ == 1) {
        RCLCPP_INFO(get_logger(), "Received first image: %ux%u %s", image->width, image->height,
                    image->encoding.c_str());
    }

    // Check if detector is enabled
    if (!params_->enabled) {
        RCLCPP_DEBUG_THROTTLE(get_logger(), *get_clock(), 5000, "Detector disabled, dropping images");
        return;
    }

    // init the detector if we havent already
    if (!detector_ || params_->reload_needed) {
        // have to temp go back to starting here
        heartbeat_->sendBeat(basin::monitor_core::ProcessState::STARTING);

        RCLCPP_INFO(get_logger(), "Detector parameters changed, reinitializing");

        RCLCPP_DEBUG(get_logger(), "Camera info: %ux%u, d size %zu, k size %zu", info->width, info->height,
                     info->d.size(), info->k.size());
        initDetector(info);

        RCLCPP_INFO(get_logger(), "Detector reinitialized");

        params_->reload_needed = false;
    }

    RCLCPP_DEBUG(get_logger(), "Received image for tag detection");

    // Convert the image to an opencv mat and run the detector
    cv::Mat opencv_image = cv_bridge::toCvShare(image, "mono8")->image;
    std::vector<landmark_detector::AprilTagDetection> detections = detector_->detectGrayHost(opencv_image.ptr());

    if (params_->is_multitag) {
        publishMultitagDetections(detections, image->header);
    } else {
        publishSingletagDetections(detections, image->header);
    }

    // Publish debug image if there are subscribers
    if (debug_image_pub_.getNumSubscribers() > 0) {
        cv::Mat color_debug_img;
        cv::cvtColor(opencv_image, color_debug_img, cv::COLOR_GRAY2BGR);

        // put a red dot near 0x 0 y in the image
        cv::circle(color_debug_img, cv::Point(10, 10), 5, cv::Scalar(0, 0, 255), -1);
        // put a green line across the top of the image
        cv::line(color_debug_img, cv::Point(0, 0), cv::Point(color_debug_img.cols, 0), cv::Scalar(0, 255, 0), 2);
        //put a blue line across the left edge of the image
        cv::line(color_debug_img, cv::Point(0, 0), cv::Point(0, color_debug_img.rows), cv::Scalar(255, 0, 0), 2);

        for (landmark_detector::AprilTagDetection tag_det : detections) {
            for (const auto &tag : tag_det.tag_corners) {
                RCLCPP_DEBUG(get_logger(), "Detected Tag ID %d", tag.first);

                // border the tag in the debug image in cyan
                for (size_t i = 0; i < tag.second.size(); i++) {
                    cv::Point p1(tag.second.at(i).x, tag.second.at(i).y);
                    cv::Point p2(tag.second.at((i + 1) % 4).x, tag.second.at((i + 1) % 4).y);
                    cv::line(color_debug_img, p1, p2, cv::Scalar(255, 255, 0), 2);
                }

                // dots on the corners of the tag in red, blue, green, yellow order starting at the first corner
                cv::circle(color_debug_img, cv::Point(tag.second.at(0).x, tag.second.at(0).y), 5, cv::Scalar(0, 0, 255), -1);
                cv::circle(color_debug_img, cv::Point(tag.second.at(1).x, tag.second.at(1).y), 5, cv::Scalar(255, 0, 0), -1);
                cv::circle(color_debug_img, cv::Point(tag.second.at(2).x, tag.second.at(2).y), 5, cv::Scalar(0, 255, 0), -1);
                cv::circle(color_debug_img, cv::Point(tag.second.at(3).x, tag.second.at(3).y), 5, cv::Scalar(0, 255, 255), -1);

                // label the tag
                cv::putText(color_debug_img, std::to_string(tag.first), tag.second.at(0), cv::FONT_HERSHEY_DUPLEX, 5.0,
                            cv::Scalar(0, 0, 255), 4);
            }
        }

        sensor_msgs::msg::Image::SharedPtr image_msg =
            cv_bridge::CvImage(image->header, "bgr8", color_debug_img).toImageMsg();
        debug_image_pub_.publish(image_msg);
    }
}

void DetectorNode::publishSingletagDetections(const std::vector<landmark_detector::AprilTagDetection> &detections,
                                              std_msgs::msg::Header header) {
    // Start building the message
    vision_msgs::msg::Detection3DArray msg;
    msg.header = header;

    // Process each detections
    for (landmark_detector::AprilTagDetection detection : detections) {
        // Make a scaled covar for distance
        double detection_distance = detection.tag_pose.translation().norm();
        // sclar = C + Ld + Qddd
        double cov_scalar = params_->const_cov_gain + params_->linear_cov_gain * detection_distance +
                            params_->cubic_cov_gain * detection_distance * detection_distance * detection_distance;
        std::array<double, 36U> scaled_covar;
        for (size_t i = 0; i < detection_covar_.size(); i++) {
            scaled_covar[i] = detection_covar_[i] * cov_scalar;
        }

        const int id = detection.tag_corners.begin()->first;
        const std::string tag_frame_id = "tag_" + std::to_string(id);

        geometry_msgs::msg::Pose ros_pose = convertTagPose(detection.tag_pose);

        // Make the detection hypothesis
        vision_msgs::msg::ObjectHypothesisWithPose hypothesis_msg;
        hypothesis_msg.pose.pose = ros_pose;
        hypothesis_msg.pose.covariance = scaled_covar;
        hypothesis_msg.hypothesis.class_id = tag_frame_id;
        hypothesis_msg.hypothesis.score = 1.0;

        // Make the detection message to send out
        vision_msgs::msg::Detection3D detection_msg;
        detection_msg.header = msg.header;
        detection_msg.bbox.center = ros_pose;
        detection_msg.bbox.size.x = params_->tag_edge_length;
        detection_msg.bbox.size.y = params_->tag_edge_length;
        detection_msg.bbox.size.z = params_->tag_edge_length;
        detection_msg.id = tag_frame_id;
        detection_msg.results.push_back(hypothesis_msg);
    }

    RCLCPP_DEBUG(get_logger(), "Detected %lu tags", detections.size());
    detection_pub_->publish(msg);
}

void DetectorNode::publishMultitagDetections(const std::vector<AprilTagDetection> &detections,
                                             std_msgs::msg::Header header) {
    localization_msgs::msg::TagSolution tag_solution_msg;
    tag_solution_msg.header = header;

    if (detections.empty()) {
        RCLCPP_INFO(get_logger(), "No multitag solution");
    } else {
        // multitag only has one result if valid

        AprilTagDetection first_detection = detections.front();
        for (const auto &tag : first_detection.tag_corners) {
            tag_solution_msg.detected_tags.push_back(tag.first);
        }

        tag_solution_msg.pose = convertTagPose(first_detection.tag_pose * CAM_TO_X_FWD);
    }

    RCLCPP_DEBUG(get_logger(), "Detected %lu tags", tag_solution_msg.detected_tags.size());
    tag_solution_pub_->publish(tag_solution_msg);

    // Publish PoseStamped for multitag consumers when we have a valid solution
    if (!tag_solution_msg.detected_tags.empty()) {
        geometry_msgs::msg::PoseStamped pose_msg;
        pose_msg.header.stamp = tag_solution_msg.header.stamp;
        pose_msg.header.frame_id = "field";
        pose_msg.pose = tag_solution_msg.pose;
        multitag_pose_pub_->publish(pose_msg);
    }
}

geometry_msgs::msg::Pose DetectorNode::convertTagPose(const Eigen::Isometry3d &cam_to_tag) {
    // Translate to ROS message
    geometry_msgs::msg::Pose tag_pose;
    tag_pose.position.x = cam_to_tag.translation().x();
    tag_pose.position.y = cam_to_tag.translation().y();
    tag_pose.position.z = cam_to_tag.translation().z();
    Eigen::Quaterniond cam_to_tag_rot(cam_to_tag.rotation());
    tag_pose.orientation.w = cam_to_tag_rot.w();
    tag_pose.orientation.x = cam_to_tag_rot.x();
    tag_pose.orientation.y = cam_to_tag_rot.y();
    tag_pose.orientation.z = cam_to_tag_rot.z();
    return tag_pose;
}

void DetectorNode::onTick() {
    // hot load detector if needed
    RCLCPP_DEBUG_THROTTLE(get_logger(), *get_clock(), 1000,
                          "Tick: enabled=%d detector_initialized=%d images_received=%lu camera_publishers=%zu",
                          params_->enabled, detector_ != nullptr, images_received_,
                          camera_subscriber_.getNumPublishers());

    // Update tracked frames on each tick
    if (params_->is_multitag && detector_ != nullptr) {
        updateTrackedFrames();
    }
}

void DetectorNode::updateTrackedFrames() {
    // Get all available frames from the TF buffer
    std::vector<std::string> frame_names;
    tf_buffer_->_getFrameStrings(frame_names);

    // Track which tag IDs we find in this update
    std::set<int> found_tag_ids;

    // Search for frames with the tracked prefix
    for (const std::string &frame_name : frame_names) {
        // Check if this frame has the tracked prefix
        if (frame_name.find(params_->tracked_frame_prefix) == 0) {
            try {
                // Try to get the transform from parent to this frame
                geometry_msgs::msg::TransformStamped transform =
                    tf_buffer_->lookupTransform(params_->parent_frame, frame_name, tf2::TimePointZero);

                // Convert the transform to an Eigen isometry
                Eigen::Isometry3d tag_pose = Eigen::Isometry3d::Identity();
                tag_pose.translate(Eigen::Vector3d(transform.transform.translation.x, transform.transform.translation.y,
                                                   transform.transform.translation.z));
                Eigen::Quaterniond tag_rot(transform.transform.rotation.w, transform.transform.rotation.x,
                                           transform.transform.rotation.y, transform.transform.rotation.z);
                tag_pose.rotate(tag_rot);

                // log the final pose of the tag and report in RPY
                Eigen::Vector3d rpy = tag_pose.rotation().canonicalEulerAngles(0, 1, 2);
                RCLCPP_DEBUG(
                    get_logger(),
                    "Found tracked tag frame: %s with pose: position (%.2f, %.2f, %.2f) orientation (%.2f, %.2f, %.2f)",
                    frame_name.c_str(), tag_pose.translation().x(), tag_pose.translation().y(),
                    tag_pose.translation().z(), rpy.x(), rpy.y(), rpy.z());

                // Extract the tag ID from the frame name
                std::string tag_id_str = frame_name.substr(params_->tracked_frame_prefix.size());
                int tag_id = std::stoi(tag_id_str);

                // Track that we found this tag ID
                found_tag_ids.insert(tag_id);

                RCLCPP_DEBUG(get_logger(), "Found tracked tag frame: %s with ID %d", frame_name.c_str(), tag_id);

                // Add the tag to the detector cache
                detector_->addTagToCache(tag_id, tag_pose, params_->tag_edge_length);

            } catch (const tf2::TransformException &ex) {
                // Transform not available yet, skip this frame
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "Could not get transform for %s: %s",
                                     frame_name.c_str(), ex.what());
            }
        }
    }

    // Remove any tags from the cache that we didn't find in this update
    std::vector<int> cached_tag_ids = detector_->getCachedTagIds();
    for (int cached_id : cached_tag_ids) {
        if (found_tag_ids.find(cached_id) == found_tag_ids.end()) {
            // This tag is cached but was not found in TF, remove it
            detector_->removeTagFromCache(cached_id);
            RCLCPP_INFO(get_logger(), "Removed tag %d from cache (no longer in TF tree)", cached_id);
        }
    }
}

void DetectorNode::shutdown() {
    RCLCPP_INFO(get_logger(), "Shutting down detector node");
    detector_.reset();
}

}  // namespace landmark_detector

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::Logger logger = rclcpp::get_logger("detector_main");

    RCLCPP_INFO(logger, "Constructing detector node");
    auto node = std::make_shared<landmark_detector::DetectorNode>();

    RCLCPP_INFO(logger, "Spinning detector node");
    rclcpp::spin(node);

    RCLCPP_INFO(logger, "Spin returned (rclcpp::ok=%d), destroying detector node", rclcpp::ok());
    node.reset();

    RCLCPP_INFO(logger, "Shutting down rclcpp");
    rclcpp::shutdown();

    RCLCPP_INFO(logger, "Exiting");
    return 0;
}
