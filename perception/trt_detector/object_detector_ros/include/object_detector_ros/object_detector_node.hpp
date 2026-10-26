#pragma once

#include <basin/node_core/node_core.hpp>
#include <image_geometry/pinhole_camera_model.hpp>
#include <image_transport/image_transport.hpp>
#include <memory>
#include <object_detector_lib/detector.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <vision_msgs/msg/detection2_d_array.hpp>

#include "object_detector_ros/object_detector_params.hpp"

namespace object_detector {
class ObjectDetectorNode : public basin::node_core::NodeCore {
   public:
    explicit ObjectDetectorNode(const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

    void imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr &msg);
    void infoCallback(const sensor_msgs::msg::CameraInfo::ConstSharedPtr &info);

    /// @brief Reloads the model when a threshold has changed.
    void onTick() override;

    void shutdown() override {}

   private:
    void loadModel();

    std::unique_ptr<DetectorNodeParams> params_;

    std::unique_ptr<image_geometry::PinholeCameraModel> cam_model_;
    std::unique_ptr<ObjectDetector> detector_;
    double loaded_conf_threshold_{ 0.0 };  ///< Thresholds detector_ was built with
    double loaded_iou_threshold_{ 0.0 };

    image_transport::Publisher debug_pub_;
    rclcpp::Publisher<vision_msgs::msg::Detection2DArray>::SharedPtr detections_pub_;

    image_transport::Subscriber img_sub_;
    rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr cam_info_sub_;
};
}  // namespace object_detector
