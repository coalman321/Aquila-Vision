#pragma once

#include <string>

namespace basler_driver::encodingconversions {

// ROS image encoding -> GenICam PixelFormat. 16-bit ROS encodings map to the 12-bit (unpacked) pixel
// formats unless the camera offers true 16-bit ones. False when there is no GenICam equivalent.
bool ros2GenAPI(const std::string& ros_enc, std::string& gen_api_enc, bool is_16bits_available);

// GenICam PixelFormat -> ROS image encoding. False when there is no ROS equivalent (packed formats).
bool genAPI2Ros(const std::string& gen_api_enc, std::string& ros_enc);

bool is_12_bit_gen_api_enc(const std::string& gen_api_enc);

bool is_12_bit_ros_enc(const std::string& ros_enc);

}  // namespace basler_driver::encodingconversions
