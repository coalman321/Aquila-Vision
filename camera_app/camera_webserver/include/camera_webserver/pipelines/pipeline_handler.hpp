#pragma once

#include <tf2_ros/static_transform_broadcaster.hpp>

#include <basin/node_core/param_client.hpp>
#include <basin/web_core/web_node.hpp>
#include <map>
#include <memory>
#include <rclcpp/timer.hpp>
#include <vector>

#include "camera_webserver/pipelines/pipeline_base.hpp"
#include "camera_webserver/sources/source_base.hpp"

namespace camera_web::pipelines {

struct PipelineHandlerConfig {
    std::filesystem::path config_dir{ "/etc/app/pipelines" };  ///< Directory for pipeline configs
    std::filesystem::path model_dir{ "/etc/app/models" };      ///< Directory for models
    std::string base_link{ "base_link" };                      ///< Base link frame for transforms
};

class PipelineHandler {
   public:
    PipelineHandler(std::shared_ptr<basin::web_core::WebNode> web_node, PipelineHandlerConfig config);

    /// @brief list all currently loaded pipelines
    /// @return vector of VideoSourceKeys for each pipeline
    std::vector<sources::VideoSourceKey> listPipelines();

    /// @brief get the pipeline config for a specific source
    /// @param source_key VideoSourceKey identifying the source
    /// @return PipelineConfig for the source
    PipelineConfig getPipeline(const sources::VideoSourceKey& source_key);

    /// @brief Callback for when a source is updated
    /// @param source_key VideoSourceKey identifying the source
    /// @param source_enabled whether the source is enabled
    void sourceUpdated(const sources::VideoSourceKey& source_key, bool source_enabled);

    /// @brief Create or update a pipeline for a specific source
    /// @param source_key VideoSourceKey identifying the source
    /// @param config PipelineConfig with desired settings
    /// @param initial_load whether this is the initial load of the pipeline
    /// @return true if successful, false otherwise
    bool createOrUpdatePipeline(const sources::VideoSourceKey& source_key, const PipelineConfig& config,
                                bool initial_load = false);

    /// @brief Finds a pipeline and enables it forcibly
    /// @param source_key VideoSourceKey identifying the source
    /// @param is_enabled true to enable the pipeline, false to disable
    bool setEnablePipeline(const sources::VideoSourceKey& source_key, bool is_enabled);

    /// @brief broadcast transforms for all enabled pipelines
    void broadcastTransforms();

   private:
    /// @brief get list of available detection models from model directory
    /// @return vector of model names
    std::vector<std::string> getAvailableModels();

    /// @brief load configured pipelines from config files
    /// @return vector of PipelineConfigs
    std::vector<std::pair<sources::VideoSourceKey, PipelineConfig>> getConfiguredPipelines();

    std::unique_ptr<basin::node_core::ParamClient> param_client_;
    std::shared_ptr<tf2_ros::StaticTransformBroadcaster> tf_broadcaster_;
    rclcpp::TimerBase::SharedPtr transform_timer_;

    rclcpp::Logger logger_{ rclcpp::get_logger("PipelineHandler") };

    PipelineHandlerConfig config_;

    std::map<sources::VideoSourceKey, pipelines::PipelineConfig> pipelines_;  ///< Available pipeline types
};

}  // namespace camera_web::pipelines
