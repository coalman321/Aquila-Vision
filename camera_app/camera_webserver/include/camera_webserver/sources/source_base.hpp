#pragma once

#include <iterator>
#include <nlohmann/json.hpp>
#include <set>

using json = nlohmann::json;

namespace camera_web::sources {
enum class CameraType { 
    BASLER, 
    UNKNOWN, 
};

struct VideoSourceKey {
    CameraType camera_type;   ///< type of camera
    std::string camera_name;  ///< unique name for the camera

    // define less than operator for use in maps
    bool operator<(const VideoSourceKey& other) const {
        if (camera_type != other.camera_type) {
            return camera_type < other.camera_type;
        }
        return camera_name < other.camera_name;
    }

    std::string getFullName() const {
        return (camera_type == CameraType::BASLER ? "basler_" : "unknown_") + camera_name;
    }

    std::string getFileName() const {
        return getFullName() + ".json";
    }

    json toJson() const {
        json j;
        j["camera_type"] = (camera_type == CameraType::BASLER) ? "BASLER" : "UNKNOWN";
        j["camera_name"] = camera_name;
        return j;
    }

    void fromJson(const json& j) {
        if (j.contains("camera_type")) {
            std::string type_str = j["camera_type"];
            camera_type = (type_str == "BASLER") ? CameraType::BASLER : CameraType::UNKNOWN;
        }
        if (j.contains("camera_name")) {
            camera_name = j["camera_name"];
        }
    }

    bool operator==(const VideoSourceKey& other) const {
        return (camera_type == other.camera_type) && (camera_name == other.camera_name);
    }

    static std::string getTypeString(CameraType type) {
        switch (type) {
            case CameraType::BASLER:
                return "BASLER";
            default:
                return "UNKNOWN";
        }
    }

    static CameraType getTypeFromString(const std::string& str) {
        std::string conv_name;
        std::transform(str.begin(), str.end(), std::back_inserter(conv_name), ::toupper);
        if (conv_name == "BASLER") {
            return CameraType::BASLER;
        } else {
            return CameraType::UNKNOWN;
        }
    }

    static VideoSourceKey fromString(const std::string & str) {
        VideoSourceKey key;
        size_t underscore_pos = str.find('_');
        if (underscore_pos != std::string::npos) {
            std::string type_str = str.substr(0, underscore_pos);
            key.camera_type = getTypeFromString(type_str);
            key.camera_name = str.substr(underscore_pos + 1);
        } else {
            key.camera_type = CameraType::UNKNOWN;
            key.camera_name = str;
        }
        return key;
    }

};

const VideoSourceKey INVALID_KEY{ CameraType::UNKNOWN, "invalid"};

struct VideoSourceConfig {
    CameraType camera_type{ CameraType::UNKNOWN };  ///< type of camera
    std::string camera_name;                        ///< unique name for the camera

    bool source_enabled{ false };    ///< is the source currently enabled
    bool source_connected{ false };  ///< is the source currently connected

    int frame_rate{ 30 };    ///< desired frame rate
    int binning{ 1 };        ///< desired binning factor
    int base_width{ 1280 };  ///< base width (in pixels) of the camera
    int base_height{ 960 };  ///< base height (in pixels) of the camera

    std::filesystem::path calibration_file;  ///< path to calibration file

    // define less than operator for use in sets
    bool operator<(const VideoSourceConfig& other) const {
        if (camera_type != other.camera_type) {
            return camera_type < other.camera_type;
        }
        return camera_name < other.camera_name;
    }

    /// @brief Check if the source is active (connected and enabled)
    /// @return true if active, false otherwise
    bool isActive() const { return source_connected && source_enabled; }

    std::string getResolutionStr() const {
        return std::to_string(base_width / binning) + "x" + std::to_string(base_height / binning);
    }

    json toJson() const {
        json j;
        j["camera_type"] = (camera_type == CameraType::BASLER) ? "BASLER" : "UNKNOWN";
        j["camera_name"] = camera_name;
        j["source_enabled"] = source_enabled;
        j["frame_rate"] = frame_rate;
        j["binning"] = binning;
        j["calibration_file"] = calibration_file.filename();
        return j;
    }

    void fromJson(const json& j, std::filesystem::path base_path) {
        if (j.contains("camera_type")) {
            std::string type_str = j["camera_type"];
            camera_type = (type_str == "BASLER") ? CameraType::BASLER : CameraType::UNKNOWN;
        }
        if (j.contains("camera_name")) {
            camera_name = j["camera_name"];
        }
        if (j.contains("source_enabled")) {
            source_enabled = j["source_enabled"];
        }
        if (j.contains("frame_rate")) {
            frame_rate = j["frame_rate"];
        }
        if (j.contains("binning")) {
            binning = j["binning"];
        }
        if (j.contains("calibration_file")) {
            calibration_file = base_path / j["calibration_file"];
        }
    }
};

class CameraSource {
   public:
    virtual std::set<VideoSourceConfig> listConnectedSources() = 0;

    virtual bool isSourceConnected(const std::string & name) = 0;

    virtual std::string getSourceTypeName() const = 0;
};

}  // namespace camera_web::sources
