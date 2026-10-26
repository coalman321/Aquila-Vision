#pragma once

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace basin::jsonable {

struct Pose3d {
    float x{ 0 };      ///< X position
    float y{ 0 };      ///< Y position
    float z{ 0 };      ///< Z position
    float roll{ 0 };   ///< Roll angle in radians
    float pitch{ 0 };  ///< Pitch angle in radians
    float yaw{ 0 };    ///< Yaw angle in radians

    nlohmann::json toJson() const {
        json j;
        j["x"] = x;
        j["y"] = y;
        j["z"] = z;
        j["roll"] = roll;
        j["pitch"] = pitch;
        j["yaw"] = yaw;
        return j;
    }

    void fromJson(const nlohmann::json& j) {
        if (j.contains("x")) {
            x = j["x"].get<float>();
        }
        if (j.contains("y")) {
            y = j["y"].get<float>();
        }
        if (j.contains("z")) {
            z = j["z"].get<float>();
        }
        if (j.contains("roll")) {
            roll = j["roll"].get<float>();
        }
        if (j.contains("pitch")) {
            pitch = j["pitch"].get<float>();
        }
        if (j.contains("yaw")) {
            yaw = j["yaw"].get<float>();
        }
    }

    bool operator==(const Pose3d& other) const {
        return x == other.x && y == other.y && z == other.z && roll == other.roll && pitch == other.pitch &&
               yaw == other.yaw;
    }
};

}  // namespace basin::jsonable
