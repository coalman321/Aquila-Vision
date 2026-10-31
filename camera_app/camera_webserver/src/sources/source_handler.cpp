#include "camera_webserver/sources/source_handler.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/logging.hpp>
#include <vector>

#include "camera_webserver/sources/basler_camera_source.hpp"
#include "camera_webserver/sources/source_base.hpp"

using json = nlohmann::json;

namespace camera_web::sources {

CameraSourceHandler::CameraSourceHandler(std::shared_ptr<basin::web_core::WebNode> web_node,
                                         OnSrcUpdateCallback on_src_update, CameraSourceHandlerConfig config) :
    on_source_update_(on_src_update), config_(config) {
    // ensure config directories exist
    createDirIfNotExists(config_.config_dir);
    createDirIfNotExists(config_.calibration_dir);

    // Register available source types
    available_source_types_.emplace(
        CameraType::BASLER,
        std::make_shared<sources::BaslerCameraSource>());  // right now just the basler source

    // load initial sources
    std::vector<sources::VideoSourceConfig> initial_sources = loadConfiguredSources();
    for (const auto& source : initial_sources) {
        VideoSourceKey key{ source.camera_type, source.camera_name };
        sources_[key] = source;

        // check that the camera is connected
        auto it = available_source_types_.find(source.camera_type);
        if (it != available_source_types_.end()) {
            sources_[key].source_connected = it->second->isSourceConnected(source.camera_name);
        } else {
            sources_[key].source_connected = false;
        }

        // notify of the source update
        // Note: This may trigger ROS2 operations which can fail due to threading/executor constraints
        // We catch and log these errors but still return success since the config was saved
        try {
            on_source_update_(key, source.source_enabled, true);
        } catch (const std::exception& e) {
            RCLCPP_WARN(logger_, "Source update callback threw exception (config still saved): %s", e.what());
        }
    }

    // list connected sources to update connection status
    web_node->getServer().Get("/api/sources", [this](const httplib::Request&, httplib::Response& res) {
        auto sources = listSources();

        json j_sources = json::array();
        for (const auto& [key, source] : sources_) {
            json j_source = key.toJson();
            j_source["missing_calibration"] = source.isMissingCalibration();
            j_sources.push_back(j_source);
        }

        res.status = 200;
        res.set_content(j_sources.dump(), "application/json");
    });

    // setup web routes
    web_node->getServer().Get("/api/sources/source", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            // parse the source key from query parameters
            sources::VideoSourceKey key;
            if (!req.has_param("camera_type") || !req.has_param("camera_name")) {
                res.status = 400;
                res.set_content("{\"error\": \"Missing camera_type or camera_name query parameters.\"}",
                                "application/json");
                return;
            }
            std::string camera_type_str = req.get_param_value("camera_type");
            std::string camera_name_str = req.get_param_value("camera_name");
            key.camera_type =
                (camera_type_str == "BASLER") ? sources::CameraType::BASLER : sources::CameraType::UNKNOWN;
            key.camera_name = camera_name_str;

            // isolate the specific source and prepare the response
            if (sources_.find(key) != sources_.end()) {
                sources::VideoSourceConfig& source = sources_[key];

                std::vector<std::string> resolutions_available;
                for (int bin = 1; bin <= 4; ++bin) {
                    int width = source.base_width / bin;
                    int height = source.base_height / bin;
                    resolutions_available.push_back(std::to_string(width) + "x" + std::to_string(height));
                }

                // add custom source info
                json j_source = source.toJson();
                j_source["source_connected"] = source.source_connected;
                j_source["resolution"] = source.getResolutionStr();
                j_source["resolutions_available"] = resolutions_available;
                j_source["calibration_files_available"] = getAvailableCalibrationFiles();

                res.status = 200;
                res.set_content(j_source.dump(), "application/json");
            } else {
                res.status = 400;
                res.set_content("{\"error\": \"Source not found.\"}", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content("{\"error\": \"Invalid source key: " + std::string(e.what()) + "\"}", "application/json");
        }
    });

    // post route for updating source config
    web_node->getServer().Post("/api/sources/source", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json j;
            j = json::parse(req.body);

            sources::VideoSourceConfig config;
            config.fromJson(j, config_.calibration_dir);

            // update binning based on resolution string
            std::string camera_res = j.value("resolution", config.getResolutionStr());
            int new_width = std::stoi(camera_res.substr(0, camera_res.find('x')));
            config.binning = config.base_width / new_width;

            bool success = updateSourceConfig(config);
            if (!success) {
                res.status = 400;
                res.set_content("{\"error\": \"Source not found for update.\"}", "application/json");
                return;
            }

            res.status = 200;
            res.set_content("{\"message\": \"Source configuration updated successfully.\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content("{\"error\": \"Failed to update source configuration: " + std::string(e.what()) + "\"}",
                            "application/json");
        }
    });

    RCLCPP_INFO(logger_, "CameraSourceHandler initialized on route /api/sources and /api/sources/source");
    RCLCPP_INFO(logger_, "CameraSourceHandler using config dir: %s", config_.config_dir.c_str());
    RCLCPP_INFO(logger_, "CameraSourceHandler using calibration dir: %s", config_.calibration_dir.c_str());
}

std::vector<sources::VideoSourceConfig> CameraSourceHandler::listSources() {
    std::vector<sources::VideoSourceKey> connected_sources{};

    // check each source type for connected sources
    for (auto& source_type : available_source_types_) {
        RCLCPP_INFO(logger_, "checking source type %s for sources", source_type.second->getSourceTypeName().c_str());

        std::set<sources::VideoSourceConfig> detected_sources = source_type.second->listConnectedSources();
        for (const auto& detected_source : detected_sources) {
            // if the source is new, add it to the sources_ map
            sources::VideoSourceKey key{ detected_source.camera_type, detected_source.camera_name };
            if (sources_.find(key) == sources_.end()) {
                sources_[key] = detected_source;
            }

            // always add to connected sources list
            connected_sources.push_back(key);
        }
    }

    // Update source_connected status for all sources based on detected connected sources
    for (auto& [key, source] : sources_) {
        source.source_connected =
            (std::find(connected_sources.begin(), connected_sources.end(), key) != connected_sources.end());
    }

    // flatten the map to a vector for return
    std::vector<sources::VideoSourceConfig> sources;
    for (const auto& [key, source] : sources_) {
        sources.push_back(source);
    }

    return sources;
}

bool CameraSourceHandler::updateSourceConfig(const sources::VideoSourceConfig& config) {
    sources::VideoSourceKey key{ config.camera_type, config.camera_name };

    // verify the source exists
    if (sources_.find(key) == sources_.end()) {
        return false;
    }

    // copy the specific config fields
    sources_.at(key).binning = config.binning;
    sources_.at(key).frame_rate = config.frame_rate;
    sources_.at(key).source_enabled = config.source_enabled;
    sources_.at(key).calibration_file = config.calibration_file;

    // serialize the config to the file as well
    std::filesystem::path config_file = config_.config_dir / key.getFileName();
    json j = sources_.at(key).toJson();
    {
        std::ofstream ofs(config_file);
        ofs << j.dump();
    }

    // notify of the source update
    // Note: This may trigger ROS2 operations which can fail due to threading/executor constraints
    // We catch and log these errors but still return success since the config was saved
    try {
        on_source_update_(key, sources_.at(key).source_enabled, false);
    } catch (const std::exception& e) {
        RCLCPP_WARN(logger_, "Source update callback threw exception (config still saved): %s", e.what());
    }

    return true;
}

std::vector<sources::VideoSourceConfig> CameraSourceHandler::loadConfiguredSources() const {
    std::vector<sources::VideoSourceConfig> configs;
    for (const auto& entry : std::filesystem::directory_iterator(config_.config_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            RCLCPP_INFO(logger_, "Loading source config from file: %s", entry.path().c_str());

            // load the file
            json j;
            {
                std::ifstream ifs(entry.path());
                ifs >> j;
            }

            // deserialize the config
            sources::VideoSourceConfig source_config;
            source_config.fromJson(j, config_.calibration_dir);
            configs.push_back(source_config);
        }
    }

    return configs;
}

std::vector<std::string> CameraSourceHandler::getAvailableCalibrationFiles() {
    std::vector<std::string> calibration_files;

    if (std::filesystem::exists(config_.calibration_dir) && std::filesystem::is_directory(config_.calibration_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(config_.calibration_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".yaml") {
                RCLCPP_DEBUG(logger_, "Found calibration file: %s", entry.path().c_str());
                calibration_files.push_back(entry.path().filename());
            }
        }
    }

    return calibration_files;
}

void CameraSourceHandler::createDirIfNotExists(const std::filesystem::path& dir_path) {
    if (!std::filesystem::exists(dir_path)) {
        RCLCPP_DEBUG(logger_, "creating directory: %s", dir_path.c_str());
        std::filesystem::create_directories(dir_path);
    }
}

}  // namespace camera_web::sources
