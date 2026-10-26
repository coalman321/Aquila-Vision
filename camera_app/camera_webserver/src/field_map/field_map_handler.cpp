#include "camera_webserver/field_map/field_map_handler.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/logging.hpp>
#include <tf2/LinearMath/Quaternion.hpp>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>

using json = nlohmann::json;

namespace camera_web::field_map {

nlohmann::json FieldPosition::toJson() const {
    json j;
    j["id"] = id;
    j["pose"] = pose.toJson();
    j["description"] = description;
    return j;
}

void FieldPosition::fromJson(const nlohmann::json& j) {
    if (j.contains("id")) {
        id = j["id"];
    }
    if (j.contains("pose")) {
        pose.fromJson(j["pose"]);
    }
    if (j.contains("description")) {
        description = j["description"];
    }
}

FieldMapHandler::FieldMapHandler(std::shared_ptr<basin::web_core::WebNode> web_node, FieldMapHandlerConfig config) :
    config_(config) {
    // Initialize TF broadcaster
    tf_broadcaster_ = std::make_shared<tf2_ros::StaticTransformBroadcaster>(web_node);

    // Ensure config directory exists
    createDirIfNotExists(config_.config_dir);

    // Load initial positions
    std::vector<FieldPosition> initial_positions = loadConfiguredPositions();
    for (const auto& position : initial_positions) {
        positions_[position.id] = position;
    }

    // GET route: List all positions
    web_node->getServer().Get("/api/field_map/positions", [this](const httplib::Request&, httplib::Response& res) {
        auto positions = listPositions();

        json j_positions = json::array();
        for (const auto& position : positions) {
            j_positions.push_back(position.toJson());
        }

        res.status = 200;
        res.set_content(j_positions.dump(), "application/json");
    });

    // GET route: Get specific position by ID
    web_node->getServer().Get("/api/field_map/position",
                              [this](const httplib::Request& req, httplib::Response& res) {
                                  try {
                                      if (!req.has_param("id")) {
                                          res.status = 400;
                                          res.set_content("{\"error\": \"Missing id query parameter.\"}",
                                                          "application/json");
                                          return;
                                      }

                                      std::string id = req.get_param_value("id");
                                      auto position = getPosition(id);

                                      if (position.has_value()) {
                                          res.status = 200;
                                          res.set_content(position->toJson().dump(), "application/json");
                                      } else {
                                          res.status = 404;
                                          res.set_content("{\"error\": \"Position not found.\"}", "application/json");
                                      }
                                  } catch (const std::exception& e) {
                                      res.status = 400;
                                      res.set_content("{\"error\": \"" + std::string(e.what()) + "\"}",
                                                      "application/json");
                                  }
                              });

    // POST route: Update or create position
    web_node->getServer().Post("/api/field_map/position",
                               [this](const httplib::Request& req, httplib::Response& res) {
                                   try {
                                       json j = json::parse(req.body);

                                       FieldPosition position;
                                       position.fromJson(j);

                                       // Validate required fields
                                       if (position.id.empty()) {
                                           res.status = 400;
                                           res.set_content("{\"error\": \"Position ID is required.\"}",
                                                           "application/json");
                                           return;
                                       }

                                       // Check if this is a rename operation (old_id provided and different from new id)
                                       if (j.contains("old_id") && !j["old_id"].is_null()) {
                                           std::string old_id = j["old_id"].get<std::string>();
                                           if (old_id != position.id && positions_.find(old_id) != positions_.end()) {
                                               // Delete the old position
                                               deletePosition(old_id);
                                               RCLCPP_INFO(logger_, "Renamed position from %s to %s", old_id.c_str(), position.id.c_str());
                                           }
                                       }

                                       bool success = updatePosition(position);
                                       if (success) {
                                           res.status = 200;
                                           res.set_content(
                                               "{\"message\": \"Position updated successfully.\", \"id\": \"" +
                                                   position.id + "\"}",
                                               "application/json");
                                       } else {
                                           res.status = 500;
                                           res.set_content("{\"error\": \"Failed to update position.\"}",
                                                           "application/json");
                                       }
                                   } catch (const std::exception& e) {
                                       res.status = 400;
                                       res.set_content(
                                           "{\"error\": \"Failed to update position: " + std::string(e.what()) + "\"}",
                                           "application/json");
                                   }
                               });

    // DELETE route: Delete position by ID
    web_node->getServer().Delete("/api/field_map/position",
                                 [this](const httplib::Request& req, httplib::Response& res) {
                                     try {
                                         if (!req.has_param("id")) {
                                             res.status = 400;
                                             res.set_content("{\"error\": \"Missing id query parameter.\"}",
                                                             "application/json");
                                             return;
                                         }

                                         std::string id = req.get_param_value("id");
                                         bool success = deletePosition(id);

                                         if (success) {
                                             res.status = 200;
                                             res.set_content("{\"message\": \"Position deleted successfully.\"}",
                                                             "application/json");
                                         } else {
                                             res.status = 404;
                                             res.set_content("{\"error\": \"Position not found.\"}", "application/json");
                                         }
                                     } catch (const std::exception& e) {
                                         res.status = 400;
                                         res.set_content(
                                             "{\"error\": \"Failed to delete position: " + std::string(e.what()) + "\"}",
                                             "application/json");
                                     }
                                 });

    RCLCPP_INFO(logger_, "FieldMapHandler initialized on route /api/field_map");
    RCLCPP_INFO(logger_, "FieldMapHandler using config dir: %s", config_.config_dir.c_str());
}

std::vector<FieldPosition> FieldMapHandler::listPositions() {
    std::vector<FieldPosition> positions;
    for (const auto& [id, position] : positions_) {
        positions.push_back(position);
    }
    return positions;
}

std::optional<FieldPosition> FieldMapHandler::getPosition(const std::string& id) {
    if (positions_.find(id) != positions_.end()) {
        return positions_[id];
    }
    return std::nullopt;
}

bool FieldMapHandler::updatePosition(const FieldPosition& position) {
    try {
        // Update in-memory map
        positions_[position.id] = position;

        // Save to file
        savePositionToFile(position);

        RCLCPP_INFO(logger_, "Updated position: %s", position.id.c_str());
        return true;
    } catch (const std::exception& e) {
        RCLCPP_ERROR(logger_, "Failed to update position %s: %s", position.id.c_str(), e.what());
        return false;
    }
}

bool FieldMapHandler::deletePosition(const std::string& id) {
    // Check if position exists
    if (positions_.find(id) == positions_.end()) {
        RCLCPP_WARN(logger_, "Attempted to delete non-existent position: %s", id.c_str());
        return false;
    }

    // Remove from memory
    positions_.erase(id);

    // Delete file
    std::filesystem::path config_file = config_.config_dir / (id + ".json");
    if (std::filesystem::exists(config_file)) {
        std::filesystem::remove(config_file);
        RCLCPP_INFO(logger_, "Deleted position: %s", id.c_str());
    }

    return true;
}

std::vector<FieldPosition> FieldMapHandler::loadConfiguredPositions() const {
    std::vector<FieldPosition> positions;

    if (!std::filesystem::exists(config_.config_dir)) {
        return positions;
    }

    for (const auto& entry : std::filesystem::directory_iterator(config_.config_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            RCLCPP_INFO(logger_, "Loading position config from file: %s", entry.path().c_str());

            try {
                // Load the file
                json j;
                {
                    std::ifstream ifs(entry.path());
                    ifs >> j;
                }

                // Deserialize the config
                FieldPosition position;
                position.fromJson(j);
                positions.push_back(position);
            } catch (const std::exception& e) {
                RCLCPP_ERROR(logger_, "Failed to load position from %s: %s", entry.path().c_str(), e.what());
            }
        }
    }

    return positions;
}

void FieldMapHandler::savePositionToFile(const FieldPosition& position) {
    std::filesystem::path config_file = config_.config_dir / (position.id + ".json");
    json j = position.toJson();

    std::ofstream ofs(config_file);
    if (!ofs) {
        throw std::runtime_error("Failed to open file for writing: " + config_file.string());
    }
    ofs << j.dump(4);  // Pretty print with 4 space indent
}

void FieldMapHandler::createDirIfNotExists(const std::filesystem::path& dir_path) {
    if (!std::filesystem::exists(dir_path)) {
        RCLCPP_DEBUG(logger_, "Creating directory: %s", dir_path.c_str());
        std::filesystem::create_directories(dir_path);
    }
}

void FieldMapHandler::publishTransforms() {
    if (positions_.empty()) {
        return;
    }

    std::vector<geometry_msgs::msg::TransformStamped> transforms;
    transforms.reserve(positions_.size());

    for (const auto& [id, position] : positions_) {
        geometry_msgs::msg::TransformStamped transform;
        transform.header.stamp = rclcpp::Clock().now();
        transform.header.frame_id = config_.base_frame;
        transform.child_frame_id = config_.child_frame_prefix + id;

        // Set translation
        transform.transform.translation.x = position.pose.x;
        transform.transform.translation.y = position.pose.y;
        transform.transform.translation.z = position.pose.z;

        // Convert roll, pitch, yaw to quaternion
        tf2::Quaternion quat;
        quat.setRPY(position.pose.roll, position.pose.pitch, position.pose.yaw);
        transform.transform.rotation = tf2::toMsg(quat);

        transforms.push_back(transform);
    }

    // Publish all transforms
    tf_broadcaster_->sendTransform(transforms);
}

}  // namespace camera_web::field_map
