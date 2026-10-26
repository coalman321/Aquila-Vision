#pragma once

#include <basin/node_core/node_params.hpp>
#include <string>
#include <vector>

namespace object_detector {
struct DetectorNodeParams : public basin::node_core::NodeParams {
    // Connection settings
    std::string image_topic{ "camera/image_raw" };
    std::string info_topic{ "camera/info" };

    // Network settings
    std::string network_path{ "/engine.trt" };
    std::string model_architecture{ "yolov8" };  ///< yolov8 or rfdetr
    double iou_threshold{ 0.6 };
    double conf_threshold{ 0.6 };
    std::vector<std::string> class_names{};

    DetectorNodeParams(rclcpp::Node* node) : NodeParams(node) {
        // Connection settings
        createParam("image_topic", image_topic);
        createParam("cam_info_topic", info_topic);

        // Network settings. Changing a threshold reloads the model (see ObjectDetectorNode::onTick()).
        createParam("network_path", network_path);
        createParam("model_architecture", model_architecture);
        createDynParam("iou_threshold", iou_threshold);
        createDynParam("conf_threshold", conf_threshold);
        createDynParam("class_names", class_names);
    }
};
}  // namespace object_detector
