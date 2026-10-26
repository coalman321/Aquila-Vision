#include "camera_webserver/pipelines/pipeline_handler.hpp"

#include <tf2/LinearMath/Quaternion.hpp>

#include <geometry_msgs/msg/transform_stamped.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/parameter.hpp>
#include <utility>

#include "camera_webserver/pipelines/pipeline_base.hpp"
#include "camera_webserver/sources/source_base.hpp"

namespace camera_web::pipelines {

PipelineHandler::PipelineHandler(std::shared_ptr<basin::web_core::WebNode> web_node, PipelineHandlerConfig config) :
    config_(config) {
    // create the config directory if it doesn't exist
    if (!std::filesystem::exists(config_.config_dir)) {
        RCLCPP_DEBUG(logger_, "Creating pipeline config directory: %s", config_.config_dir.c_str());
        std::filesystem::create_directories(config_.config_dir);
    }

    // load initial pipelines
    std::vector<std::pair<sources::VideoSourceKey, PipelineConfig>> initial_pipelines = getConfiguredPipelines();
    for (const auto& pipeline : initial_pipelines) {
        // use insert_or_assign to avoid requiring a default-constructible PipelineConfig
        pipelines_.insert_or_assign(pipeline.first, pipeline.second);
    }

    // load the param client
    param_client_ = std::make_unique<basin::node_core::ParamClient>(web_node);

    // initialize static tf broadcaster
    tf_broadcaster_ = std::make_shared<tf2_ros::StaticTransformBroadcaster>(web_node);

    // register web routes
    web_node->getServer().Get("/api/pipelines", [this](const httplib::Request& req, httplib::Response& res) {
        json j;
        j["pipelines"] = json::array();
        for (const auto& key : listPipelines()) {
            j["pipelines"].push_back(key.toJson());
        }
        res.set_content(j.dump(), "application/json");
    });

    web_node->getServer().Get(R"(/api/pipelines/([\w\-]+))",
                              [this](const httplib::Request& req, httplib::Response& res) {
                                  std::string source_name = req.matches[1];
                                  sources::VideoSourceKey source_key = sources::VideoSourceKey::fromString(source_name);

                                  PipelineConfig pipeline = getPipeline(source_key);

                                  // update available models before sending
                                  pipeline.getDetectionConfig().models = getAvailableModels();

                                  if (pipeline == PipelineConfig{}) {
                                      res.status = 404;
                                      res.set_content("{\"error\": \"Pipeline not found\"}", "application/json");
                                      return;
                                  }

                                  json j = pipeline.toJson();
                                  res.set_content(j.dump(), "application/json");
                              });

    web_node->getServer().Post(
        R"(/api/pipelines/([\w\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
            std::string source_name = req.matches[1];
            sources::VideoSourceKey source_key = sources::VideoSourceKey::fromString(source_name);

            RCLCPP_INFO(logger_, "Updating pipeline %s", source_name.c_str());

            try {
                PipelineConfig pipeline_config;
                pipeline_config.fromJson(json::parse(req.body));

                if (!createOrUpdatePipeline(source_key, pipeline_config)) {
                    res.status = 500;
                    res.set_content("{\"error\": \"Failed to create or update pipeline\"}", "application/json");
                    return;
                }

                res.status = 200;
                res.set_content("{\"status\": \"Pipeline created or updated successfully\"}", "application/json");
            } catch (const std::exception& e) {
                res.status = 400;
                res.set_content("{\"error\": \"Invalid JSON payload\"}", "application/json");
            }
        });
}

std::vector<sources::VideoSourceKey> PipelineHandler::listPipelines() {
    std::vector<sources::VideoSourceKey> keys;
    for (const auto& [key, cfg] : pipelines_) {
        // only list enabled pipelines
        if (cfg.isEnabled()) {
            keys.push_back(key);
        }
    }
    return keys;
}

PipelineConfig PipelineHandler::getPipeline(const sources::VideoSourceKey& source_key) {
    auto it = pipelines_.find(source_key);
    if (it != pipelines_.end()) {
        return it->second;
    } else {
        return PipelineConfig{};
    }
}

bool PipelineHandler::createOrUpdatePipeline(const sources::VideoSourceKey& source_key, const PipelineConfig& config,
                                             bool initial_load) {
    RCLCPP_INFO(logger_, "Creating or updating pipeline for source: %s", source_key.getFullName().c_str());

    // update the memory copy of the pipeline
    auto it = pipelines_.find(source_key);
    bool is_new = (it == pipelines_.end());
    bool is_enabled = false;
    if (!is_new) {
        is_enabled = it->second.isEnabled();
        it->second = config;
    } else if (!pipelines_.insert_or_assign(source_key, config).second) {
        RCLCPP_ERROR(logger_, "Failed to create new pipeline for source: %s", source_key.getFullName().c_str());
        return false;
    }

    // restore the enabled status of the pipeline regardless of what the frontend sends
    it->second.setEnabled(is_enabled);

    if (it->second.isEnabled()) {
        RCLCPP_INFO(logger_, "Pipeline enabled for source: %s", source_key.getFullName().c_str());
    }

    // now update the config file
    json j = pipelines_.at(source_key).toJson();
    std::filesystem::path config_file = config_.config_dir / source_key.getFileName();
    try {
        std::ofstream ofs(config_file);
        ofs << j.dump();
        RCLCPP_INFO(logger_, "Wrote pipeline config for source %s to file: %s", source_key.getFullName().c_str(),
                    config_file.c_str());
    } catch (const std::exception& e) {
        RCLCPP_ERROR(logger_, "Failed to write pipeline config for source %s to file %s: %s",
                     source_key.getFullName().c_str(), config_file.c_str(), e.what());
        return false;
    }

    if (!initial_load) {
        RCLCPP_INFO(logger_, "Updating pipeline for source: %s", source_key.getFullName().c_str());

        // generate the namespace for this source
        std::string ns = "/" + source_key.getFullName() + "/";

        // send required param updates to the driver node
        std::string driver_node_name = ns + "driver";
        param_client_->isNodeAvailable(driver_node_name, 50);
        rclcpp::Parameter gain_param("gain", config.getGain());
        if (!param_client_->setParameter(driver_node_name, gain_param)) {
            RCLCPP_ERROR(logger_, "Failed to set gain parameter on node %s: %s", driver_node_name.c_str(),
                         param_client_->getLastError().c_str());
        }
        rclcpp::Parameter exposure_param("exposure", config.getExposure());
        if (!param_client_->setParameter(driver_node_name, exposure_param)) {
            RCLCPP_ERROR(logger_, "Failed to set exposure parameter on node %s: %s", driver_node_name.c_str(),
                         param_client_->getLastError().c_str());
        }

        // also update the apriltag pipeline
        std::string apriltag_node_name = ns + "apriltag_detector";
        param_client_->isNodeAvailable(apriltag_node_name, 50);
        rclcpp::Parameter apriltag_enabled_param("enabled", config.isAprilTagEnabled());
        if (!param_client_->setParameter(apriltag_node_name, apriltag_enabled_param)) {
            RCLCPP_ERROR(logger_, "Failed to set enabled parameter on node %s: %s", apriltag_node_name.c_str(),
                         param_client_->getLastError().c_str());
        }

        // last update the object detection pipeline
        // std::string object_detect_node_name = ns + "object_detector";
        // param_client_->isNodeAvailable(object_detect_node_name, 50);
        // rclcpp::Parameter object_detect_enabled_param("enabled", config.isObjectDetectEnabled());
        // if (!param_client_->setParameter(object_detect_node_name, object_detect_enabled_param)) {
        //     RCLCPP_ERROR(logger_, "Failed to set enabled parameter on node %s: %s", object_detect_node_name.c_str(),
        //                  param_client_->getLastError().c_str());
        // }

        // validate model exists in the models list
        // if (config.isObjectDetectEnabled()) {
        //     std::vector<std::string> models = getAvailableModels();
        //     auto model_it = std::find(models.begin(), models.end(), config.getModel());
        //     if (model_it == models.end()) {
        //         RCLCPP_ERROR(logger_, "invalid model selected: %s", config.getModel().c_str());
        //         return false;
        //     }

        //     std::filesystem::path model_path = config_.model_dir / config.getModel();
        //     rclcpp::Parameter od_model("network_path", model_path.string());
        //     if (!param_client_->setParameter(object_detect_node_name, od_model)) {
        //         RCLCPP_ERROR(logger_, "Failed to set OD model on node %s: %s", object_detect_node_name.c_str(),
        //                      param_client_->getLastError().c_str());
        //     }
        // }
    }

    return true;
}

std::vector<std::pair<sources::VideoSourceKey, PipelineConfig>> PipelineHandler::getConfiguredPipelines() {
    // load from the config files
    std::vector<std::pair<sources::VideoSourceKey, PipelineConfig>> configs;
    for (const auto& entry : std::filesystem::directory_iterator(config_.config_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::string camera_name = entry.path().filename().replace_extension("");

            RCLCPP_INFO(logger_, "Loading %s pipeline config from file: %s", camera_name.c_str(), entry.path().c_str());

            try {
                // load the file
                json j;
                {
                    std::ifstream ifs(entry.path());
                    ifs >> j;
                }

                // deserialize the config
                PipelineConfig pipeline_config;
                pipeline_config.fromJson(j);

                // force disable on load
                pipeline_config.setEnabled(false);

                configs.push_back(std::pair<sources::VideoSourceKey, PipelineConfig>(
                    sources::VideoSourceKey::fromString(camera_name), pipeline_config));
            } catch (const std::exception& e) {
                RCLCPP_ERROR(logger_, "Failed to load pipeline from %s: %s", entry.path().c_str(), e.what());
            }
        }
    }

    return configs;
}

std::vector<std::string> PipelineHandler::getAvailableModels() {
    std::vector<std::string> models;
    if (std::filesystem::exists(config_.model_dir) && std::filesystem::is_directory(config_.model_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(config_.model_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".onnx") {
                models.push_back(entry.path().filename().string());
            }
        }
    } else {
        RCLCPP_WARN(logger_, "Model directory does not exist: %s", config_.model_dir.c_str());
    }
    return models;
}

bool PipelineHandler::setEnablePipeline(const sources::VideoSourceKey& source_key, bool is_enabled) {
    auto it = pipelines_.find(source_key);
    if (it != pipelines_.end()) {
        it->second.setEnabled(is_enabled);
    } else {
        RCLCPP_ERROR(logger_, "Unable to find an enable pipeline for source: %s", source_key.getFullName().c_str());
        return false;
    }
    return true;
}

void PipelineHandler::broadcastTransforms() {
    for (const auto& [source_key, config] : pipelines_) {
        if (!config.isEnabled()) {
            continue;
        }

        geometry_msgs::msg::TransformStamped transform;
        transform.header.stamp = rclcpp::Clock().now();
        transform.header.frame_id = config_.base_link;
        transform.child_frame_id = source_key.getFullName();

        // Set translation from camera pose
        const auto& pose = config.getCameraPose();
        transform.transform.translation.x = pose.x;
        transform.transform.translation.y = pose.y;
        transform.transform.translation.z = pose.z;

        // Convert roll, pitch, yaw to quaternion (pose angles are already in radians)
        tf2::Quaternion q;
        q.setRPY(pose.roll, pose.pitch, pose.yaw);
        transform.transform.rotation.x = q.x();
        transform.transform.rotation.y = q.y();
        transform.transform.rotation.z = q.z();
        transform.transform.rotation.w = q.w();

        tf_broadcaster_->sendTransform(transform);
    }
}

}  // namespace camera_web::pipelines
