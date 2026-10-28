#include "camera_cal/chessboard_calibrator.hpp"

namespace camera_cal {

std::optional<std::string> ChessboardCalibrator::validate(const Config& config) {
    if (config.rows < 2 || config.cols < 2) {
        return "Chessboard needs at least 2x2 internal corners, got " + std::to_string(config.rows) + "x" +
               std::to_string(config.cols);
    }
    if (config.square_size_m <= 0.0) {
        return "Chessboard square size must be greater than 0";
    }
    return std::nullopt;
}

ChessboardCalibrator::ChessboardCalibrator(const Config& config) : config_(config) {
    object_points_.reserve(config_.rows * config_.cols);
    for (int r = 0; r < config_.rows; ++r) {
        for (int c = 0; c < config_.cols; ++c) {
            object_points_.emplace_back(static_cast<float>(c) * static_cast<float>(config_.square_size_m),
                                        static_cast<float>(r) * static_cast<float>(config_.square_size_m), 0.0f);
        }
    }
}

bool ChessboardCalibrator::detect(const cv::Mat& detect_image, const cv::Mat& /*full_image*/, double scale,
                                  cv::Mat& debug, Detection& out) {
    const cv::Size board_size(config_.cols, config_.rows);
    std::vector<cv::Point2f> corners;

    // findChessboardCornersSB (Saddle-point Based) is faster than the classic
    // detector and returns sub-pixel corners directly, so no cornerSubPix step
    // is needed.  CALIB_CB_NORMALIZE_IMAGE is omitted on the CUDA path because
    // equalizeHist was already applied on the GPU.
#ifdef CAMERA_CAL_HAVE_CUDA
    constexpr int kDetectFlags = 0;  // normalization already done on GPU
#else
    constexpr int kDetectFlags = cv::CALIB_CB_NORMALIZE_IMAGE;
#endif
    const bool found = cv::findChessboardCornersSB(detect_image, board_size, corners, kDetectFlags);

    if (found && scale < 1.0) {
        // Scale corners back to original resolution
        for (auto& pt : corners) {
            pt.x /= scale;
            pt.y /= scale;
        }
    }

    cv::drawChessboardCorners(debug, board_size, corners, found);

    if (!found) {
        return false;
    }

    // Skew: angle of the board's first row relative to horizontal
    const cv::Point2f& p0 = corners[0];
    const cv::Point2f& p1 = corners[config_.cols - 1];
    out.skew = std::abs(std::atan2(p1.y - p0.y, p1.x - p0.x));
    out.image_points = std::move(corners);
    out.object_points = object_points_;
    return true;
}

}  // namespace camera_cal
