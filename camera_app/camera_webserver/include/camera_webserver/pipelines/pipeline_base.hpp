#pragma once

#include <basin/jsonable/pose3d.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

#include "camera_webserver/sources/source_base.hpp"

using json = nlohmann::json;

namespace camera_web::pipelines {

struct PipelineType {
    bool enabled{ false };  ///< Whether detection is enabled

    nlohmann::json toJson() const {
        json j;
        j["enabled"] = enabled;
        return j;
    }

    void fromJson(const nlohmann::json& j) {
        if (j.contains("enabled")) {
            enabled = j["enabled"].get<bool>();
        }
    }
};

struct DetectionConfig : public PipelineType {
    std::string model_name{ "" };       ///< Name of the detection model
    std::vector<std::string> models;    ///< Available models
    float confidence_threshold{ 0.5 };  ///< Confidence threshold for detections
    float iou_threshold{ 0.5 };         ///< Intersection over Union threshold for non-max suppression

    nlohmann::json toJson() const {
        json j = PipelineType::toJson();
        j["model_name"] = model_name;
        j["models"] = models;
        j["confidence_threshold"] = confidence_threshold;
        j["iou_threshold"] = iou_threshold;
        return j;
    }

    void fromJson(const nlohmann::json& j) {
        PipelineType::fromJson(j);
        if (j.contains("model_name")) {
            model_name = j["model_name"].get<std::string>();
        }
        if (j.contains("confidence_threshold")) {
            confidence_threshold = j["confidence_threshold"].get<float>();
        }
        if (j.contains("iou_threshold")) {
            iou_threshold = j["iou_threshold"].get<float>();
        }
    }
};

enum class TagFamily { TAG36H11, UNKNOWN_FAMILY };

struct ApriltagConfig : public PipelineType {
    TagFamily family{ TagFamily::TAG36H11 };  ///< AprilTag family type
    float tag_size{ 0.1651 };                 ///< Size of the AprilTag in meters

    nlohmann::json toJson() const {
        json j = PipelineType::toJson();
        // convert family enum to string
        std::string family_str;
        switch (family) {
            case TagFamily::TAG36H11:
                family_str = "TAG36H11";
                break;
            default:
                family_str = "UNKNOWN";
                break;
        }
        j["family"] = family_str;
        j["families"] = { "TAG36H11", "UNKNOWN" };
        j["tag_size"] = tag_size;
        return j;
    }

    void fromJson(const nlohmann::json& j) {
        PipelineType::fromJson(j);
        if (j.contains("family")) {
            std::string family_str = j["family"].get<std::string>();
            if (family_str == "TAG36H11") {
                family = TagFamily::TAG36H11;
            } else {
                family = TagFamily::UNKNOWN_FAMILY;
            }
        }
        if (j.contains("tag_size")) {
            tag_size = j["tag_size"].get<float>();
        }
    }
};

class PipelineConfig {
   public:
    PipelineConfig() : exposure(1000.0), gain(0.0) {}

    json toJson() const {
        json j;
        j["camera_pose"] = camera_pose.toJson();
        j["exposure"] = exposure;
        j["gain"] = gain;
        j["detection_config"] = detection_config.toJson();
        j["apriltag_config"] = apriltag_config.toJson();
        return j;
    }

    void fromJson(const json& j) {
        if (j.contains("camera_pose")) {
            camera_pose.fromJson(j["camera_pose"]);
        }
        if (j.contains("exposure")) {
            exposure = j["exposure"].get<double>();
        }
        if (j.contains("gain")) {
            gain = j["gain"].get<double>();
        }
        if (j.contains("detection_config")) {
            detection_config.fromJson(j["detection_config"]);
        }
        if (j.contains("apriltag_config")) {
            apriltag_config.fromJson(j["apriltag_config"]);
        }
    }

    void setEnabled(bool en) { enabled = en; }
    bool isEnabled() const { return enabled; }

    double getExposure() const { return exposure; }
    double getGain() const { return gain; }

    const basin::jsonable::Pose3d& getCameraPose() const { return camera_pose; }

    ApriltagConfig& getApriltagConfig() { return apriltag_config; }
    bool isAprilTagEnabled() const { return apriltag_config.enabled; }
    TagFamily getTagFamily() const { return apriltag_config.family; }

    DetectionConfig& getDetectionConfig() { return detection_config; }
    bool isObjectDetectEnabled() const { return detection_config.enabled; }
    std::string getModel() const { return detection_config.model_name; }

    bool operator==(const PipelineConfig& other) const {
        return enabled == other.enabled && camera_pose == other.camera_pose && exposure == other.exposure &&
               gain == other.gain && detection_config.enabled == other.detection_config.enabled &&
               detection_config.model_name == other.detection_config.model_name &&
               detection_config.confidence_threshold == other.detection_config.confidence_threshold &&
               detection_config.iou_threshold == other.detection_config.iou_threshold &&
               apriltag_config.enabled == other.apriltag_config.enabled &&
               apriltag_config.family == other.apriltag_config.family &&
               apriltag_config.tag_size == other.apriltag_config.tag_size;
    }

   private:
    /// @brief Key identifying the source this pipeline is associated with
    bool enabled{ false };  ///< Whether the pipeline is enabled

    // Common settings for preprocessing
    basin::jsonable::Pose3d camera_pose;  ///< Pose of the camera
    double exposure{ 10000 };             ///< Camera exposure setting
    double gain{ 0.0 };                   ///< Camera gain setting

    // Pipeline-specific settings
    DetectionConfig detection_config;  ///< Object detection configuration
    ApriltagConfig apriltag_config;    ///< AprilTag detection configuration
};

}  // namespace camera_web::pipelines
