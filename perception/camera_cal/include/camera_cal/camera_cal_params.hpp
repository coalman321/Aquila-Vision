#pragma once

#include <basin/node_core/node_params.hpp>
#include <rclcpp/node.hpp>
#include <string>

namespace camera_cal {

/// @brief ROS 2 parameter bag for the camera calibration node.
class CameraCalParams : public basin::node_core::NodeParams {
public:
    // ----- Board geometry -----

    /// Number of internal corner rows on the calibration board
    int board_rows{6};

    /// Number of internal corner columns on the calibration board
    int board_cols{9};

    // ----- Calibration settings -----

    /// Minimum number of accepted frames before calibration is allowed
    int min_captures{20};

    /// Directory in which the calibration YAML will be saved
    std::string output_dir{"/tmp/calibration"};

    // ----- Motion-deduplication thresholds -----

    /// Minimum centroid displacement (as a fraction of the board's own
    /// bounding-box diagonal) required to accept a frame.  Frames where
    /// the board hasn't moved this far are silently dropped.
    double motion_position_threshold{0.1};

    /// Minimum relative change in board span required to accept a frame.
    /// Frames where the board size hasn't changed by this fraction are
    /// silently dropped (unless the position threshold is already met).
    double motion_scale_threshold{0.6};

    explicit CameraCalParams(rclcpp::Node* node) : basin::node_core::NodeParams(node) {
        // Static params – read once at startup
        createParam("output_dir", output_dir);
        createParam("min_captures", min_captures);

        // Dynamic params – can be updated at runtime
        createDynParam("board_rows", board_rows);
        createDynParam("board_cols", board_cols);
        createDynParam("motion_position_threshold", motion_position_threshold);
        createDynParam("motion_scale_threshold", motion_scale_threshold);
    }
};

}  // namespace camera_cal
