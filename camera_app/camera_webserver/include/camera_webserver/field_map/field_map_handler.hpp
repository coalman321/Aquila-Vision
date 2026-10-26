#pragma once

#include <basin/web_core/web_node.hpp>
#include <basin/jsonable/pose3d.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <tf2_ros/static_transform_broadcaster.hpp>

namespace camera_web::field_map {

struct FieldPosition {
    std::string id;                  ///< Unique identifier for the position
    basin::jsonable::Pose3d pose;    ///< 3D pose of the position on the field
    std::string description;         ///< Optional description of the position

    /// @brief Serialize to JSON
    /// @return JSON representation of the field position
    nlohmann::json toJson() const;

    /// @brief Deserialize from JSON
    /// @param j JSON object to deserialize from
    void fromJson(const nlohmann::json& j);

    /// @brief Check equality based on ID
    bool operator==(const FieldPosition& other) const { return id == other.id; }
};

struct FieldMapHandlerConfig {
    std::filesystem::path config_dir{"/etc/app/field_positions"};  ///< Directory for field position configs
    std::string base_frame{"base_link"};  ///< Base frame for transforms
    std::string child_frame_prefix{"field_position_"};  ///< Prefix for child frame IDs
};

class FieldMapHandler {
   public:
    FieldMapHandler(std::shared_ptr<basin::web_core::WebNode> web_node, FieldMapHandlerConfig config);

    /// @brief List all configured field positions
    /// @return vector of FieldPositions
    std::vector<FieldPosition> listPositions();

    /// @brief Get a specific field position by ID
    /// @param id Unique identifier of the position
    /// @return FieldPosition if found, nullptr otherwise
    std::optional<FieldPosition> getPosition(const std::string& id);

    /// @brief Update or create a field position
    /// @param position FieldPosition to update or create
    /// @return true if successful, false otherwise
    bool updatePosition(const FieldPosition& position);

    /// @brief Delete a field position
    /// @param id Unique identifier of the position to delete
    /// @return true if successful, false if position not found
    bool deletePosition(const std::string& id);

    /// @brief Publish static transforms for all field positions
    /// This should be called periodically (e.g., from onTick) to ensure transforms are available
    void publishTransforms();

   private:
    /// @brief Create directory if it does not exist
    /// @param dir_path Path to the directory
    void createDirIfNotExists(const std::filesystem::path& dir_path);

    /// @brief Load configured positions from config files
    /// @return vector of FieldPositions
    std::vector<FieldPosition> loadConfiguredPositions() const;

    /// @brief Save a position to a config file
    /// @param position FieldPosition to save
    void savePositionToFile(const FieldPosition& position);

    std::map<std::string, FieldPosition> positions_;  ///< Map of field positions by ID

    FieldMapHandlerConfig config_;

    rclcpp::Logger logger_{rclcpp::get_logger("FieldMapHandler")};

    std::shared_ptr<tf2_ros::StaticTransformBroadcaster> tf_broadcaster_;  ///< Static TF broadcaster
};

}  // namespace camera_web::field_map
