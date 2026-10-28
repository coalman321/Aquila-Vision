#pragma once

#include <optional>
#include <string>
#include <vector>

#include "camera_cal/calibrator.hpp"

namespace camera_cal {

/// @brief Calibrator for a standard chessboard target.
class ChessboardCalibrator : public CameraCalibrator {
   public:
    struct Config {
        /// Number of internal corner rows on the calibration board
        int rows{ 0 };
        /// Number of internal corner columns on the calibration board
        int cols{ 0 };
        /// Physical side length of one square in metres
        double square_size_m{ 0.0 };
    };

    /// @brief Check that @p config describes a usable board.
    /// @return An error message, or std::nullopt if the config is valid
    static std::optional<std::string> validate(const Config& config);

    explicit ChessboardCalibrator(const Config& config);

   protected:
    bool detect(const cv::Mat& detect_image, const cv::Mat& full_image, double scale, cv::Mat& debug,
                Detection& out) override;

   private:
    Config config_;
    /// 3-D corner grid for one board pose; every detection sees the whole board.
    std::vector<cv::Point3f> object_points_;
};

}  // namespace camera_cal
