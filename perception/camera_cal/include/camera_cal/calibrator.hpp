#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace camera_cal {

/// @brief Calibration board detection and camera calibration library.
///
///        This base class owns everything that is independent of the target:
///        frame collection, motion deduplication, coverage statistics,
///        cv::calibrateCamera and YAML output.  Each supported board type
///        (see ChessboardCalibrator, CharucoCalibrator) derives from it and
///        implements detect().
class CameraCalibrator {
   public:
    virtual ~CameraCalibrator() = default;

    /// @brief Attempt to detect the calibration board in an image and, if found,
    ///        store the refined corner positions for later calibration.
    ///
    ///        A frame is silently dropped (returns false) even when the board is
    ///        detected if its centroid has moved less than
    ///        @p position_threshold (fraction of the board's own bounding-box
    ///        diagonal) AND its scale has changed less than @p scale_threshold
    ///        relative to the previously accepted frame.  This prevents
    ///        near-duplicate poses from inflating the capture count.
    /// @param image  Mono8 input image
    /// @return true if the board was detected AND the frame was accepted
    bool detectAndCapture(const cv::Mat& image);

    /// @brief Return the last image annotated with the detected (or missing) corners.
    ///        Returns an empty Mat when no image has been processed yet.
    cv::Mat getDebugImage() const;

    /// @brief Return the number of frames that have been successfully captured.
    int getCaptureCount() const;

    /// @brief Run OpenCV camera calibration over all collected frames.
    /// @param image_size  Resolution of the captured images
    /// @param camera_matrix  Output 3x3 camera intrinsic matrix
    /// @param dist_coeffs    Output distortion coefficients vector
    /// @return RMS reprojection error, or a negative value on failure
    double calibrate(cv::Size image_size, cv::Mat& camera_matrix, cv::Mat& dist_coeffs);

    /// @brief Serialise calibration results to a YAML file inside @p directory
    ///        in ROS camera_info format (compatible with camera_info_manager).
    ///        The directory is created if it does not already exist.
    /// @return true on success
    bool saveCalibration(const std::string& directory, const std::string& camera_name, const cv::Mat& camera_matrix,
                         const cv::Mat& dist_coeffs, const cv::Size& image_size, double rms);

    /// @brief Set the minimum motion required to accept a frame.
    ///
    ///   @p position_fraction  – centroid must move at least this fraction of
    ///                           the board's bounding-box diagonal.  Default 0.15.
    ///   @p scale_fraction     – board span must change by at least this fraction
    ///                           of the previous span.  Default 0.15.
    ///
    /// A frame is accepted when EITHER threshold is exceeded (i.e. enough
    /// positional movement OR enough scale change).
    void setMotionThresholds(float position_fraction, float scale_fraction);

    /// @brief Discard all previously captured frames.
    void reset();

    /// @brief Per-frame and per-session coverage statistics derived from
    ///        the image points captured so far.
    struct CoverageStats {
        /// Normalised x/y extents of captured corners in [-1, 1] space
        float min_x{ 0.0f }, max_x{ 0.0f };
        float min_y{ 0.0f }, max_y{ 0.0f };
        /// Maximum board tilt angle (radians) seen across all captures
        float max_skew{ 0.0f };
        /// Maximum fraction of the image diagonal covered by the board
        float max_scale{ 0.0f };
    };

    /// @brief Compute coverage statistics from all frames captured so far.
    /// @param image_size  Resolution of the source images (pixels)
    /// @return Populated CoverageStats, or a zeroed struct if no frames exist
    CoverageStats getCoverageStats(cv::Size image_size) const;

   protected:
    CameraCalibrator() = default;

    /// @brief The board as seen in a single frame.
    struct Detection {
        /// Detected corners in full-resolution pixel coordinates
        std::vector<cv::Point2f> image_points;
        /// Matching corner positions in the board frame (metres, z = 0)
        std::vector<cv::Point3f> object_points;
        /// Absolute angle (radians) of the board x-axis relative to the image horizontal
        float skew{ 0.0f };
    };

    /// @brief Find the board in one frame.
    /// @param detect_image  Mono8 image to run the detector on, downscaled by @p scale
    /// @param full_image    The original mono8 image, for sub-pixel refinement
    /// @param scale         Factor @p full_image was resized by to give @p detect_image (<= 1)
    /// @param debug         Full-resolution BGR image to draw the detection result onto
    /// @param out           Filled in when the board is found
    /// @return true if the board was found
    virtual bool detect(const cv::Mat& detect_image, const cv::Mat& full_image, double scale, cv::Mat& debug,
                        Detection& out) = 0;

   private:
    std::vector<std::vector<cv::Point2f>> image_points_;
    std::vector<std::vector<cv::Point3f>> object_points_;
    std::vector<float> frame_skews_;
    cv::Mat last_debug_image_;

    // ----- Motion deduplication state -----
    /// Centroid of the last accepted board detection (pixel coords).
    cv::Point2f last_accepted_centroid_{ 0.0f, 0.0f };
    /// Bounding-box diagonal of the last accepted board (pixels).
    /// Negative when no frame has been accepted yet.
    float last_accepted_span_{ -1.0f };
    /// Minimum centroid displacement as a fraction of the board span.
    float motion_position_threshold_{ 0.15f };
    /// Minimum span change as a fraction of the previous span.
    float motion_scale_threshold_{ 0.15f };

    /// @brief Apply motion deduplication to a detection, updating the last-accepted
    ///        pose when it passes.
    /// @return true if the board moved enough relative to the last accepted frame
    bool acceptMotion(const std::vector<cv::Point2f>& corners);
};

}  // namespace camera_cal
