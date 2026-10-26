#include "object_detector_ros/object_detector_node.hpp"

#include <cv_bridge/cv_bridge.hpp>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::placeholders;

namespace object_detector {
ObjectDetectorNode::ObjectDetectorNode(const rclcpp::NodeOptions &options) :
    basin::node_core::NodeCore("object_detector", "", options) {
    params_ = std::make_unique<DetectorNodeParams>(this);

    // force trigger detector load
    loadModel();

    cam_model_ = std::make_unique<image_geometry::PinholeCameraModel>();

    // QoS(10) is the reliable, keep-last-10 rmw_qos_profile_default the node-pointer overloads used.
    debug_pub_ = image_transport::create_publisher(*this, "debug_img", rclcpp::QoS(10));
    detections_pub_ = create_publisher<vision_msgs::msg::Detection2DArray>("detections", rclcpp::SensorDataQoS());

    image_transport::TransportHints hints(*this);
    img_sub_ = image_transport::create_subscription(*this, params_->image_topic,
                                                    std::bind(&ObjectDetectorNode::imageCallback, this, _1),
                                                    hints.getTransport(), rclcpp::QoS(10));
    cam_info_sub_ = create_subscription<sensor_msgs::msg::CameraInfo>(
        params_->info_topic, rclcpp::SystemDefaultsQoS(), std::bind(&ObjectDetectorNode::infoCallback, this, _1));

    RCLCPP_INFO(get_logger(), "Detector node loaded");
}

void ObjectDetectorNode::infoCallback(const sensor_msgs::msg::CameraInfo::ConstSharedPtr &info) {
    cam_model_->fromCameraInfo(info);
}

void ObjectDetectorNode::imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr &msg) {
    if (!cam_model_->initialized()) {
        RCLCPP_WARN(get_logger(), "Waiting for camera info");
        return;
    }

    // Convert the ROS image to OpenCV format
    cv::Mat image = cv_bridge::toCvShare(msg, "bgr8")->image;

    // Now use the model to rectify the image
    cv::Mat rectified_image;
    cam_model_->rectifyImage(image, rectified_image);

    // Finally feed it to the detector
    std::vector<Detection> detections = detector_->runDetection(rectified_image);

    auto class_name = [this](int class_id) -> std::string {
        return class_id >= 0 && static_cast<size_t>(class_id) < params_->class_names.size()
                   ? params_->class_names.at(class_id)
                   : "Unknown";
    };

    // build the new detections message
    vision_msgs::msg::Detection2DArray out_msg;
    out_msg.header = msg->header;
    for (const Detection &detection : detections) {
        // convert requried data
        int centerX = detection.bbox().x + detection.bbox().width / 2;
        int centerY = detection.bbox().y + detection.bbox().height / 2;
        std::string class_id_str = class_name(detection.classId());

        // build the detection
        vision_msgs::msg::Detection2D detection_msg;
        detection_msg.id = class_id_str;
        detection_msg.bbox.center.theta = 0;
        detection_msg.bbox.center.position.x = centerX;
        detection_msg.bbox.center.position.y = centerY;
        detection_msg.bbox.size_x = detection.bbox().width;
        detection_msg.bbox.size_y = detection.bbox().height;
        vision_msgs::msg::ObjectHypothesisWithPose hypo_pose;
        hypo_pose.hypothesis.class_id = class_id_str;
        hypo_pose.hypothesis.score = detection.confidence();
        detection_msg.results.push_back(hypo_pose);

        out_msg.detections.push_back(detection_msg);
    }
    detections_pub_->publish(out_msg);

    // optionally publish the debug message
    if (debug_pub_.getNumSubscribers() > 0) {
        for (const Detection &detection : detections) {
            cv::rectangle(rectified_image, detection.bbox(), cv::Scalar(0, 255, 0), 4);
            cv::Point2d bottomLeft(detection.bbox().x, detection.bbox().y);
            cv::putText(rectified_image, class_name(detection.classId()), bottomLeft, cv::FONT_HERSHEY_DUPLEX, 2.0,
                        cv::Scalar(255, 0, 0), 2);
        }

        sensor_msgs::msg::Image::SharedPtr debug_out =
            cv_bridge::CvImage(msg->header, "bgr8", rectified_image).toImageMsg();
        debug_pub_.publish(debug_out);
    }
}

void ObjectDetectorNode::onTick() {
    // Compared rather than flagged from the parameter callbacks, which also fire for the node's own
    // declarations at startup and would reload the model it just loaded.
    if (params_->conf_threshold != loaded_conf_threshold_ || params_->iou_threshold != loaded_iou_threshold_) {
        loadModel();
    }
}

void ObjectDetectorNode::loadModel() {
    ModelArchitecture architecture;
    if (params_->model_architecture == "yolov8") {
        architecture = ModelArchitecture::YOLOV8;
    } else if (params_->model_architecture == "rfdetr") {
        architecture = ModelArchitecture::RF_DETR;
    } else {
        throw std::runtime_error("model_architecture must be yolov8 or rfdetr, not " + params_->model_architecture);
    }

    RCLCPP_INFO_STREAM(get_logger(), "Loading " << params_->model_architecture << " model " << params_->network_path
                                                << "! This may take a minute!");
    detector_ = std::make_unique<ObjectDetector>(params_->network_path, DetectionType::DETECT,
                                                 params_->conf_threshold, params_->iou_threshold, architecture);
    loaded_conf_threshold_ = params_->conf_threshold;
    loaded_iou_threshold_ = params_->iou_threshold;
}

}  // namespace object_detector

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(object_detector::ObjectDetectorNode)
