#pragma once

#include <Eigen/Geometry>
#include <tf2_ros/buffer.hpp>
#include <tf2_ros/transform_listener.hpp>

#include <basin/node_core/node_core.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <detector_lib/april_detector.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <image_transport/image_transport.hpp>
#include <localization_msgs/msg/tag_solution.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <vision_msgs/msg/detection3_d_array.hpp>

#include "landmark_detector_ros/detector_node_params.hpp"

namespace landmark_detector {
class DetectorNode : public basin::node_core::NodeCore {
   public:
    DetectorNode();

    void initDetector(sensor_msgs::msg::CameraInfo::ConstSharedPtr info);

    void detectTags(sensor_msgs::msg::Image::ConstSharedPtr image, sensor_msgs::msg::CameraInfo::ConstSharedPtr info);

    void publishSingletagDetections(const std::vector<landmark_detector::AprilTagDetection> &detections,
                                    std_msgs::msg::Header header);

    void publishMultitagDetections(const std::vector<landmark_detector::AprilTagDetection> &detections,
                                   std_msgs::msg::Header header);

    void onTick() override;

    void shutdown() override;

   private:

    static inline const Eigen::Isometry3d CAM_TO_X_FWD = []() {
        Eigen::Isometry3d tf;
        tf.setIdentity();
        tf.rotate(Eigen::AngleAxisd(M_PI / 2.0, Eigen::Vector3d::UnitX()));
        tf.rotate(Eigen::AngleAxisd(M_PI / 2.0, Eigen::Vector3d::UnitZ()));
        return tf;
    }();

    geometry_msgs::msg::Pose convertTagPose(const Eigen::Isometry3d &cam_to_tag);
    void updateTrackedFrames();

    std::unique_ptr<DetectorNodeParams> params_;

    std::array<double, 36u> detection_covar_;                   ///< Covariance for detections
    std::unique_ptr<landmark_detector::AprilDetect> detector_;  ///< Instance of the detector used for the last frame
    bool detector_needs_reinit_{ false };                       ///< Flag to indicate detector needs reinitialization
    uint64_t images_received_{ 0 };                             ///< Images received since startup, for status logs

    image_transport::CameraSubscriber camera_subscriber_;  ///< Subscription for the camera data

    ///< Publisher for the 3d detections
    rclcpp::Publisher<vision_msgs::msg::Detection3DArray>::SharedPtr detection_pub_;
    rclcpp::Publisher<localization_msgs::msg::TagSolution>::SharedPtr tag_solution_pub_;
    rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr multitag_pose_pub_;
    image_transport::Publisher debug_image_pub_;  ///< Publisher for the debug images / camera stream to the dashboard

    // Transform tracking
    std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
    std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
};
}  // namespace landmark_detector
