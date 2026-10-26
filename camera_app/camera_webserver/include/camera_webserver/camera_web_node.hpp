#pragma once

#include <basin/web_core/file_manager_component.hpp>
#include <basin/web_core/launch_component.hpp>
#include <basin/web_core/static_server_component.hpp>
#include <basin/web_core/web_node.hpp>
#include <memory>

#include "camera_webserver/calibration/calibration_handler.hpp"
#include "camera_webserver/camera_web_params.hpp"
#include "camera_webserver/field_map/field_map_handler.hpp"
#include "camera_webserver/pipelines/pipeline_handler.hpp"
#include "camera_webserver/sources/source_base.hpp"
#include "camera_webserver/sources/source_handler.hpp"

namespace camera_web {

class CameraWebNode : public basin::web_core::WebNode {
   public:
    explicit CameraWebNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

    void onTick() override;

    void shutdown() override;

    void onSourceUpdate(sources::VideoSourceKey key, bool enabled, bool initial_load);

   private:
    std::unique_ptr<CameraWebParams> params_;  ///< Node parameters

    // File Handlers for uploading config files
    std::unique_ptr<basin::web_core::FileManagerComponent> models_file_manager_;  ///< File manager for models
    std::unique_ptr<basin::web_core::FileManagerComponent> cal_file_manager_;     ///< File manager for calibrations

    // source and pipeline handlers
    std::unique_ptr<sources::CameraSourceHandler> source_handler_;  ///< Camera source handler
    std::unique_ptr<pipelines::PipelineHandler> pipeline_handler_;  ///< Camera pipeline handler

    // field map handler
    std::unique_ptr<field_map::FieldMapHandler> field_map_handler_;  ///< Field map handler

    // calibration handler
    std::unique_ptr<calibration::CalibrationHandler> calibration_handler_;  ///< Camera calibration handler

    // launch handler
    std::shared_ptr<basin::web_core::LaunchComponent> launch_component_;

    // static server component
    std::shared_ptr<basin::web_core::StaticServerComponent> static_component_;
};

}  // namespace camera_web
