#include "camera_webserver/camera_web_node.hpp"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <memory>
#include <rclcpp/qos.hpp>

#include "camera_webserver/pipelines/pipeline_base.hpp"

namespace camera_web {

CameraWebNode::CameraWebNode(const rclcpp::NodeOptions& options) :
    basin::web_core::WebNode("camera_web_node", options) {
    // load parameters
    params_ = std::make_unique<CameraWebParams>(this);

    // Create a shared pointer to this node for components
    std::shared_ptr<basin::web_core::WebNode> web_node(this, [](auto*) {});
    std::filesystem::path app_path = params_->app_dir;

    getServer().Get("/api/test", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"message\": \"Hello from web_test!\"}", "application/json");
    });

    // Setup file managers
    basin::web_core::FileManagerComponentConfig fm_models;
    fm_models.base_route = "/api/upload/models";
    fm_models.files_dir = app_path / params_->models_dir;
    models_file_manager_ = std::make_unique<basin::web_core::FileManagerComponent>(web_node, fm_models);

    basin::web_core::FileManagerComponentConfig fm_calibrations;
    fm_calibrations.base_route = "/api/upload/calibrations";
    fm_calibrations.files_dir = app_path / params_->calibrations_dir;
    cal_file_manager_ = std::make_unique<basin::web_core::FileManagerComponent>(web_node, fm_calibrations);

    // setup calibration handler
    calibration_handler_ = std::make_unique<calibration::CalibrationHandler>(web_node);

    // setup launch handler
    launch_component_ = std::make_shared<basin::web_core::LaunchComponent>(web_node);

    // Setup pipeline handler
    pipelines::PipelineHandlerConfig pipeline_config;
    pipeline_config.config_dir = app_path / params_->pipeline_config_dir;
    pipeline_config.model_dir = app_path / params_->models_dir;
    pipeline_config.base_link = params_->camera_base_frame;
    pipeline_handler_ = std::make_unique<pipelines::PipelineHandler>(web_node, pipeline_config);

    // Setup source handler
    sources::CameraSourceHandlerConfig source_handler_config;
    source_handler_config.config_dir = app_path / params_->source_config_dir;
    source_handler_config.calibration_dir = app_path / params_->calibrations_dir;
    source_handler_ = std::make_unique<sources::CameraSourceHandler>(
        web_node,
        std::bind(&CameraWebNode::onSourceUpdate, this, std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3),
        source_handler_config);

    // Setup field map handler
    field_map::FieldMapHandlerConfig field_map_config;
    field_map_config.config_dir = app_path / "field_positions";
    field_map_config.base_frame = params_->field_map_base_frame;
    field_map_config.child_frame_prefix = params_->field_map_child_prefix;
    field_map_handler_ = std::make_unique<field_map::FieldMapHandler>(web_node, field_map_config);

    // setup static server component
    // if the static content directory from the params does not exist, fall back to the packaged version
    std::filesystem::path static_content_path = app_path / params_->static_content_dir;
    if (!std::filesystem::exists(static_content_path)) {
        RCLCPP_WARN(get_logger(), "Static content directory '%s' does not exist. Falling back to packaged version.",
                    static_content_path.c_str());
        std::filesystem::path package_path = ament_index_cpp::get_package_share_directory("camera_webserver");
        static_content_path = package_path / "static_content";
    }

    basin::web_core::StaticServerComponentConfig static_config;
    static_config.base_route = "/static/";
    static_config.index_file = "index.html";
    static_config.static_dir = static_content_path;
    static_config.enable_directory_listing = false;
    static_component_ = std::make_shared<basin::web_core::StaticServerComponent>(web_node, static_config);

    // Setup redirect from root to static content
    getServer().Get("/", [this](const httplib::Request&, httplib::Response& res) {
        RCLCPP_INFO(get_logger(), "Redirecting '/' to '/static/index.html'");
        res.set_redirect("/static/index.html");
    });

    // Start the server
    startServer();
}

void CameraWebNode::onTick() {
    // Publish field position transforms
    if (field_map_handler_) {
        field_map_handler_->publishTransforms();
    }

    if (pipeline_handler_) {
        pipeline_handler_->broadcastTransforms();
    }
}

void CameraWebNode::onSourceUpdate(sources::VideoSourceKey key, bool enabled, bool initial_load) {
    std::string camera_name = key.getFullName();
    RCLCPP_INFO(get_logger(), "Source %s update: %s", camera_name.c_str(), enabled ? "enabled" : "disabled");

    // update pipelines
    pipelines::PipelineConfig cfg = pipeline_handler_->getPipeline(key);
    pipeline_handler_->createOrUpdatePipeline(key, cfg, initial_load);
    pipeline_handler_->setEnablePipeline(key, enabled);

    // update the calibration handler's list of available cameras for calibration
    if(enabled){
        calibration_handler_->registerCamera(camera_name);
    } else {
        calibration_handler_->removeCamera(camera_name);
    }

    // reload launch configurations
    if (launch_component_) {
        for (std::string host : launch_component_->listSystems()) {
            RCLCPP_INFO(get_logger(), "Reloading launch system: %s", host.c_str());

            try {
                process_msgs::srv::RemoteRequest::Request request;
                request.request_id = process_msgs::srv::RemoteRequest::Request::REQUEST_HOTLOAD;
                launch_component_->callRemoteRequest(host, request);
            } catch (const std::runtime_error& e) {
                RCLCPP_WARN_STREAM(get_logger(),
                                   "Failed to get process lists for system " << host << " with error: " << e.what());
                continue;
            }
        }
    }
}

void CameraWebNode::shutdown() { basin::web_core::WebNode::shutdown(); }

}  // namespace camera_web

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(camera_web::CameraWebNode)
