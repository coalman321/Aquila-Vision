#include "camera_cal/calibrator.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef CAMERA_CAL_HAVE_CUDA
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudawarping.hpp>
#endif

namespace camera_cal {

CameraCalibrator::CameraCalibrator(int board_rows, int board_cols, double square_size_mm) :
    board_rows_(board_rows), board_cols_(board_cols), square_size_mm_(square_size_mm) {}

bool CameraCalibrator::detectAndCapture(const cv::Mat& image) {
    cv::Size board_size(board_cols_, board_rows_);
    std::vector<cv::Point2f> corners;

    // Downscale to at most 1280 px wide before running the corner detector.
    // findChessboardCorners is O(n^2) in pixel count and can take 10+ seconds
    // on a full-resolution Pylon/Basler frame without this.
    constexpr int kMaxDetectWidth = 1280;
    const double scale = (image.cols > kMaxDetectWidth) ? static_cast<double>(kMaxDetectWidth) / image.cols : 1.0;

    cv::Mat detect_image;
#ifdef CAMERA_CAL_HAVE_CUDA
    {
        // Upload once; resize, equalizeHist, and cvtColor all happen on the GPU.
        // equalizeHist corresponds to CALIB_CB_NORMALIZE_IMAGE so we omit that
        // flag when calling findChessboardCornersSB below.
        cv::cuda::GpuMat gpu_src;
        gpu_src.upload(image);

        cv::cuda::GpuMat gpu_work;
        if (scale < 1.0) {
            cv::cuda::resize(gpu_src, gpu_work, cv::Size(), scale, scale, cv::INTER_AREA);
        } else {
            gpu_work = gpu_src;
        }

        // Histogram equalisation on the downscaled image (replaces CALIB_CB_NORMALIZE_IMAGE)
        cv::cuda::GpuMat gpu_eq;
        cv::cuda::equalizeHist(gpu_work, gpu_eq);
        gpu_eq.download(detect_image);

        // Build BGR debug image on the GPU concurrently
        cv::cuda::GpuMat gpu_bgr;
        cv::cuda::cvtColor(gpu_src, gpu_bgr, cv::COLOR_GRAY2BGR);
        gpu_bgr.download(last_debug_image_);
    }
#else
    if (scale < 1.0) {
        cv::resize(image, detect_image, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        detect_image = image;  // no copy – same data
    }
    cv::cvtColor(image, last_debug_image_, cv::COLOR_GRAY2BGR);
#endif

    // findChessboardCornersSB (Saddle-point Based) is faster than the classic
    // detector and returns sub-pixel corners directly, so no cornerSubPix step
    // is needed.  CALIB_CB_NORMALIZE_IMAGE is omitted on the CUDA path because
    // equalizeHist was already applied on the GPU above.
#ifdef CAMERA_CAL_HAVE_CUDA
    constexpr int kDetectFlags = 0;  // normalization already done on GPU
#else
    constexpr int kDetectFlags = cv::CALIB_CB_NORMALIZE_IMAGE;
#endif
    const bool found = cv::findChessboardCornersSB(detect_image, board_size, corners, kDetectFlags);

    if (found) {
        // Scale corners back to original resolution
        if (scale < 1.0) {
            for (auto& pt : corners) {
                pt.x /= scale;
                pt.y /= scale;
            }
        }

        // findChessboardCornersSB already provides sub-pixel accuracy –
        // no separate cornerSubPix call required.

        // ---- Motion deduplication ----------------------------------------
        // Compute centroid and bounding-box diagonal of detected corners.
        cv::Point2f centroid(0.0f, 0.0f);
        float bx_min = corners[0].x, bx_max = corners[0].x;
        float by_min = corners[0].y, by_max = corners[0].y;
        for (const auto& pt : corners) {
            centroid.x += pt.x;
            centroid.y += pt.y;
            bx_min = std::min(bx_min, pt.x);
            bx_max = std::max(bx_max, pt.x);
            by_min = std::min(by_min, pt.y);
            by_max = std::max(by_max, pt.y);
        }
        centroid.x /= static_cast<float>(corners.size());
        centroid.y /= static_cast<float>(corners.size());
        const float span = std::sqrt((bx_max - bx_min) * (bx_max - bx_min) +
                                     (by_max - by_min) * (by_max - by_min));

        if (last_accepted_span_ >= 0.0f && span > 0.0f) {
            const float dx = centroid.x - last_accepted_centroid_.x;
            const float dy = centroid.y - last_accepted_centroid_.y;
            const float position_fraction = std::sqrt(dx * dx + dy * dy) / last_accepted_span_;
            const float scale_fraction = std::abs(span - last_accepted_span_) / last_accepted_span_;

            if (position_fraction < motion_position_threshold_ &&
                scale_fraction < motion_scale_threshold_) {
                // Board hasn't moved enough – treat as a duplicate and drop.
                cv::drawChessboardCorners(last_debug_image_, board_size, corners, found);
                return false;
            }
        }

        last_accepted_centroid_ = centroid;
        last_accepted_span_ = span;
        // -----------------------------------------------------------------

        image_points_.push_back(corners);
    }

    cv::drawChessboardCorners(last_debug_image_, board_size, corners, found);

    return found;
}

cv::Mat CameraCalibrator::getDebugImage() const { return last_debug_image_; }

int CameraCalibrator::getCaptureCount() const { return static_cast<int>(image_points_.size()); }

double CameraCalibrator::calibrate(cv::Size image_size, cv::Mat& camera_matrix, cv::Mat& dist_coeffs) {
    if (image_points_.empty()) {
        return -1.0;
    }

    const std::vector<std::vector<cv::Point3f>> object_points(image_points_.size(), getBoardObjectPoints());

    std::vector<cv::Mat> rvecs, tvecs;
    const double rms =
        cv::calibrateCamera(object_points, image_points_, image_size, camera_matrix, dist_coeffs, rvecs, tvecs);

    return rms;
}

bool CameraCalibrator::saveCalibration(const std::string& directory, const std::string& camera_name,
                                       const cv::Mat& camera_matrix, const cv::Mat& dist_coeffs,
                                       const cv::Size& image_size, double rms) {
    namespace fs = std::filesystem;

    const fs::path dir(directory);
    if (!fs::exists(dir)) {
        std::error_code ec;
        if (!fs::create_directories(dir, ec)) {
            return false;
        }
    }

    const std::string file_path = (dir / (camera_name + ".yaml")).string();
    std::ofstream out(file_path);
    if (!out.is_open()) {
        return false;
    }

    // Write a matrix block in ROS camera_info style:
    //   rows: R
    //   cols: C
    //   data: [v00, v01, ...,
    //          v10, v11, ...]
    auto write_matrix = [&](const cv::Mat& m, int rows, int cols) {
        out << "  rows: " << rows << "\n";
        out << "  cols: " << cols << "\n";
        out << "  data: [";
        const std::string pad("         ");  // aligns continuation rows under '['
        for (int r = 0; r < rows; ++r) {
            if (r > 0) {
                out << ",\n" << pad;
            }
            for (int c = 0; c < cols; ++c) {
                if (c > 0) {
                    out << ", ";
                }
                out << std::fixed << std::setprecision(5) << m.at<double>(r, c);
            }
        }
        out << "]\n";
    };

    // Projection matrix P = [K | 0] for a monocular camera
    cv::Mat P = cv::Mat::zeros(3, 4, CV_64F);
    camera_matrix.copyTo(P(cv::Rect(0, 0, 3, 3)));

    // Rectification matrix = identity for monocular
    const cv::Mat R = cv::Mat::eye(3, 3, CV_64F);

    // Distortion must be a flat row vector for the write helper
    const cv::Mat D = dist_coeffs.reshape(1, 1);

    out << "image_width: " << image_size.width << "\n";
    out << "image_height: " << image_size.height << "\n";
    out << "camera_name: " << camera_name << "\n";
    out << "camera_matrix:\n";
    write_matrix(camera_matrix, 3, 3);
    out << "distortion_model: plumb_bob\n";
    out << "distortion_coefficients:\n";
    write_matrix(D, 1, static_cast<int>(D.total()));
    out << "rectification_matrix:\n";
    write_matrix(R, 3, 3);
    out << "projection_matrix:\n";
    write_matrix(P, 3, 4);
    out << "# rms_reprojection_error: " << std::fixed << std::setprecision(6) << rms << "\n";

    out.flush();

    return out.good();
}

void CameraCalibrator::reset() {
    image_points_.clear();
    last_debug_image_.release();
    last_accepted_span_ = -1.0f;
    last_accepted_centroid_ = cv::Point2f(0.0f, 0.0f);
}

void CameraCalibrator::setMotionThresholds(float position_fraction, float scale_fraction) {
    motion_position_threshold_ = position_fraction;
    motion_scale_threshold_ = scale_fraction;
}

void CameraCalibrator::setBoard(int rows, int cols, double square_size_mm) {
    board_rows_ = rows;
    board_cols_ = cols;
    square_size_mm_ = square_size_mm;
}

CameraCalibrator::CoverageStats CameraCalibrator::getCoverageStats(cv::Size image_size) const {
    CoverageStats stats;
    if (image_points_.empty() || image_size.width <= 0 || image_size.height <= 0) {
        return stats;
    }

    const float inv_w = 2.0f / static_cast<float>(image_size.width);
    const float inv_h = 2.0f / static_cast<float>(image_size.height);
    const float diag = std::sqrt(static_cast<float>(image_size.width * image_size.width) +
                                 static_cast<float>(image_size.height * image_size.height));

    stats.min_x = 1.0f;
    stats.max_x = -1.0f;
    stats.min_y = 1.0f;
    stats.max_y = -1.0f;

    for (const auto& corners : image_points_) {
        // --- x/y coverage ---
        for (const auto& pt : corners) {
            const float nx = pt.x * inv_w - 1.0f;
            const float ny = pt.y * inv_h - 1.0f;
            stats.min_x = std::min(stats.min_x, nx);
            stats.max_x = std::max(stats.max_x, nx);
            stats.min_y = std::min(stats.min_y, ny);
            stats.max_y = std::max(stats.max_y, ny);
        }

        // --- skew: angle of board first row relative to horizontal ---
        if (static_cast<int>(corners.size()) >= board_cols_) {
            const cv::Point2f& p0 = corners[0];
            const cv::Point2f& p1 = corners[board_cols_ - 1];
            const float angle = std::abs(std::atan2(p1.y - p0.y, p1.x - p0.x));
            stats.max_skew = std::max(stats.max_skew, angle);
        }

        // --- scale: board span normalised by image diagonal ---
        if (!corners.empty()) {
            float bx_min = corners[0].x, bx_max = corners[0].x;
            float by_min = corners[0].y, by_max = corners[0].y;
            for (const auto& pt : corners) {
                bx_min = std::min(bx_min, pt.x);
                bx_max = std::max(bx_max, pt.x);
                by_min = std::min(by_min, pt.y);
                by_max = std::max(by_max, pt.y);
            }
            const float span = std::sqrt((bx_max - bx_min) * (bx_max - bx_min) + (by_max - by_min) * (by_max - by_min));
            stats.max_scale = std::max(stats.max_scale, span / diag);
        }
    }

    return stats;
}

std::vector<cv::Point3f> CameraCalibrator::getBoardObjectPoints() const {
    std::vector<cv::Point3f> points;
    points.reserve(board_rows_ * board_cols_);
    for (int r = 0; r < board_rows_; ++r) {
        for (int c = 0; c < board_cols_; ++c) {
            points.emplace_back(static_cast<float>(c) * static_cast<float>(square_size_mm_),
                                static_cast<float>(r) * static_cast<float>(square_size_mm_), 0.0f);
        }
    }
    return points;
}

}  // namespace camera_cal
