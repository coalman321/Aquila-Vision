#include "basler_driver/encoding_conversions.hpp"

#include <map>
#include <sensor_msgs/image_encodings.hpp>

namespace basler_driver::encodingconversions {

namespace enc = sensor_msgs::image_encodings;

bool ros2GenAPI(const std::string& ros_enc, std::string& gen_api_enc, bool is_16bits_available) {
    // 16-bit ROS encodings: true 16-bit formats when the camera has them, else 12-bit in 16-bit containers.
    static const std::map<std::string, std::pair<std::string, std::string>> sixteen_bit = {
        { enc::MONO16, { "Mono12", "Mono16" } },
        { enc::BAYER_RGGB16, { "BayerRG12", "BayerRG16" } },
        { enc::BAYER_BGGR16, { "BayerBG12", "BayerBG16" } },
        { enc::BAYER_GBRG16, { "BayerGB12", "BayerGB16" } },
        { enc::BAYER_GRBG16, { "BayerGR12", "BayerGR16" } },
    };
    static const std::map<std::string, std::string> other = {
        { enc::MONO8, "Mono8" },
        { enc::BGR8, "BGR8" },
        { enc::RGB8, "RGB8" },
        { enc::BAYER_BGGR8, "BayerBG8" },
        { enc::BAYER_GBRG8, "BayerGB8" },
        { enc::BAYER_RGGB8, "BayerRG8" },
        { enc::BAYER_GRBG8, "BayerGR8" },
        { enc::YUV422, "YUV422Packed" },
    };

    if (auto it = sixteen_bit.find(ros_enc); it != sixteen_bit.end()) {
        gen_api_enc = is_16bits_available ? it->second.second : it->second.first;
        return true;
    }
    if (auto it = other.find(ros_enc); it != other.end()) {
        gen_api_enc = it->second;
        return true;
    }
    return false;
}

bool genAPI2Ros(const std::string& gen_api_enc, std::string& ros_enc) {
    static const std::map<std::string, std::string> conversions = {
        { "Mono8", enc::MONO8 },
        { "Mono12", enc::MONO16 },
        { "Mono16", enc::MONO16 },
        { "BGR8", enc::BGR8 },
        { "RGB8", enc::RGB8 },
        { "BayerBG8", enc::BAYER_BGGR8 },
        { "BayerGB8", enc::BAYER_GBRG8 },
        { "BayerRG8", enc::BAYER_RGGB8 },
        { "BayerGR8", enc::BAYER_GRBG8 },
        { "BayerRG12", enc::BAYER_RGGB16 },
        { "BayerBG12", enc::BAYER_BGGR16 },
        { "BayerGB12", enc::BAYER_GBRG16 },
        { "BayerGR12", enc::BAYER_GRBG16 },
        { "BayerRG16", enc::BAYER_RGGB16 },
        { "BayerBG16", enc::BAYER_BGGR16 },
        { "BayerGB16", enc::BAYER_GBRG16 },
        { "BayerGR16", enc::BAYER_GRBG16 },
        { "YUV422Packed", enc::YUV422 },
    };
    auto it = conversions.find(gen_api_enc);
    if (it == conversions.end()) {
        // Packed (Mono10p, Mono12p, Bayer*10/12p) and YCbCr422_8 have no ROS equivalent.
        return false;
    }
    ros_enc = it->second;
    return true;
}

bool is_12_bit_gen_api_enc(const std::string& gen_api_enc) {
    return gen_api_enc == "Mono12" || gen_api_enc == "BayerRG12" || gen_api_enc == "BayerBG12" ||
           gen_api_enc == "BayerGB12" || gen_api_enc == "BayerGR12";
}

bool is_12_bit_ros_enc(const std::string& ros_enc) {
    std::string gen_api_enc;
    return ros2GenAPI(ros_enc, gen_api_enc, false) && is_12_bit_gen_api_enc(gen_api_enc);
}

}  // namespace basler_driver::encodingconversions
