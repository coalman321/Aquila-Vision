#include "camera_cal/charuco_calibrator.hpp"

#include <map>
#include <stdexcept>

namespace camera_cal {

namespace {

/// Fewest ChArUco corners a frame needs before it is captured.  Fewer than this
/// constrains the pose too weakly to help the calibration.
constexpr int kMinCharucoCorners = 6;

std::optional<cv::aruco::PredefinedDictionaryType> dictionaryFromName(const std::string& name) {
    using D = cv::aruco::PredefinedDictionaryType;
    static const std::map<std::string, D> kDictionaries = {
        { "DICT_4X4_50", D::DICT_4X4_50 },
        { "DICT_4X4_100", D::DICT_4X4_100 },
        { "DICT_4X4_250", D::DICT_4X4_250 },
        { "DICT_4X4_1000", D::DICT_4X4_1000 },
        { "DICT_5X5_50", D::DICT_5X5_50 },
        { "DICT_5X5_100", D::DICT_5X5_100 },
        { "DICT_5X5_250", D::DICT_5X5_250 },
        { "DICT_5X5_1000", D::DICT_5X5_1000 },
        { "DICT_6X6_50", D::DICT_6X6_50 },
        { "DICT_6X6_100", D::DICT_6X6_100 },
        { "DICT_6X6_250", D::DICT_6X6_250 },
        { "DICT_6X6_1000", D::DICT_6X6_1000 },
        { "DICT_7X7_50", D::DICT_7X7_50 },
        { "DICT_7X7_100", D::DICT_7X7_100 },
        { "DICT_7X7_250", D::DICT_7X7_250 },
        { "DICT_7X7_1000", D::DICT_7X7_1000 },
        { "DICT_ARUCO_ORIGINAL", D::DICT_ARUCO_ORIGINAL },
        { "DICT_APRILTAG_16h5", D::DICT_APRILTAG_16h5 },
        { "DICT_APRILTAG_25h9", D::DICT_APRILTAG_25h9 },
        { "DICT_APRILTAG_36h10", D::DICT_APRILTAG_36h10 },
        { "DICT_APRILTAG_36h11", D::DICT_APRILTAG_36h11 },
        { "DICT_ARUCO_MIP_36h12", D::DICT_ARUCO_MIP_36h12 },
    };
    const auto it = kDictionaries.find(name);
    if (it == kDictionaries.end()) {
        return std::nullopt;
    }
    return it->second;
}

cv::aruco::CharucoBoard makeBoard(const CharucoCalibrator::Config& config) {
    if (const auto err = CharucoCalibrator::validate(config)) {
        throw std::invalid_argument(*err);
    }
    cv::aruco::CharucoBoard board(cv::Size(config.cols, config.rows), static_cast<float>(config.square_size_m),
                                  static_cast<float>(config.marker_size_m),
                                  cv::aruco::getPredefinedDictionary(*dictionaryFromName(config.aruco_dict)));
    board.setLegacyPattern(config.legacy_pattern);
    return board;
}

}  // namespace

std::optional<std::string> CharucoCalibrator::validate(const Config& config) {
    if (config.rows < 3 || config.cols < 3) {
        return "ChArUco board needs at least 3x3 squares, got " + std::to_string(config.rows) + "x" +
               std::to_string(config.cols);
    }
    if (config.square_size_m <= 0.0) {
        return "ChArUco square size must be greater than 0";
    }
    if (config.marker_size_m <= 0.0 || config.marker_size_m >= config.square_size_m) {
        return "ChArUco marker size must be greater than 0 and less than the square size";
    }
    const auto dict_type = dictionaryFromName(config.aruco_dict);
    if (!dict_type) {
        return "Unknown ArUco dictionary '" + config.aruco_dict + "'";
    }
    const int markers_needed = (config.rows * config.cols) / 2;
    const int markers_available = cv::aruco::getPredefinedDictionary(*dict_type).bytesList.rows;
    if (markers_needed > markers_available) {
        return "ArUco dictionary '" + config.aruco_dict + "' has " + std::to_string(markers_available) +
               " markers but a " + std::to_string(config.rows) + "x" + std::to_string(config.cols) +
               " board needs " + std::to_string(markers_needed);
    }
    return std::nullopt;
}

CharucoCalibrator::CharucoCalibrator(const Config& config) :
    config_(config), board_(makeBoard(config)), detector_(board_) {}

bool CharucoCalibrator::detect(const cv::Mat& detect_image, const cv::Mat& full_image, double scale, cv::Mat& debug,
                               Detection& out) {
    std::vector<cv::Point2f> charuco_corners;
    std::vector<int> charuco_ids;
    std::vector<std::vector<cv::Point2f>> marker_corners;
    std::vector<int> marker_ids;
    detector_.detectBoard(detect_image, charuco_corners, charuco_ids, marker_corners, marker_ids);

    if (scale < 1.0) {
        // Scale everything back to original resolution
        for (auto& pt : charuco_corners) {
            pt.x /= scale;
            pt.y /= scale;
        }
        for (auto& marker : marker_corners) {
            for (auto& pt : marker) {
                pt.x /= scale;
                pt.y /= scale;
            }
        }
    }

    if (!marker_ids.empty()) {
        cv::aruco::drawDetectedMarkers(debug, marker_corners, marker_ids);
    }

    if (static_cast<int>(charuco_corners.size()) < kMinCharucoCorners) {
        return false;
    }

    // The detector refined the corners on the downscaled image; refine again at
    // full resolution to recover the accuracy lost to the resize.
    if (scale < 1.0) {
        cv::cornerSubPix(full_image, charuco_corners, cv::Size(5, 5), cv::Size(-1, -1),
                         cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.01));
    }

    cv::aruco::drawDetectedCornersCharuco(debug, charuco_corners, charuco_ids);

    board_.matchImagePoints(charuco_corners, charuco_ids, out.object_points, out.image_points);
    if (static_cast<int>(out.image_points.size()) < kMinCharucoCorners) {
        return false;
    }

    // Skew: project the board x-axis through the board-to-image homography,
    // since a partial detection need not contain a full row of corners.
    std::vector<cv::Point2f> board_xy;
    board_xy.reserve(out.object_points.size());
    for (const auto& pt : out.object_points) {
        board_xy.emplace_back(pt.x, pt.y);
    }
    const cv::Mat homography = cv::findHomography(board_xy, out.image_points);
    if (!homography.empty()) {
        const std::vector<cv::Point2f> axis_board = { { 0.0f, 0.0f },
                                                      { static_cast<float>(config_.square_size_m), 0.0f } };
        std::vector<cv::Point2f> axis_image;
        cv::perspectiveTransform(axis_board, axis_image, homography);
        out.skew = std::abs(std::atan2(axis_image[1].y - axis_image[0].y, axis_image[1].x - axis_image[0].x));
    }
    return true;
}

}  // namespace camera_cal
