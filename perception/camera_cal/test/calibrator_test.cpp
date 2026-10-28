// End-to-end checks of the calibrators: renders a ChArUco board and a chessboard as seen by a pinhole
// camera with known intrinsics at a spread of poses, feeds the views through detectAndCapture and
// checks that calibrate() recovers those intrinsics. Also covers board validation, motion
// deduplication and the YAML output.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "camera_cal/charuco_calibrator.hpp"
#include "camera_cal/chessboard_calibrator.hpp"

namespace camera_cal {
namespace {

// Wider than the calibrator's 1280 px detection limit, so the downscale path is exercised too.
const cv::Size kImageSize(1920, 1200);
constexpr double kFocalLength = 1400.0;
constexpr double kCx = 960.0;
constexpr double kCy = 600.0;
constexpr double kSquareM = 0.03;

cv::Mat TrueCameraMatrix() { return (cv::Mat_<double>(3, 3) << kFocalLength, 0, kCx, 0, kFocalLength, kCy, 0, 0, 1); }

// A rendered board: the image plus its scale, so board-frame metres map to board-image pixels.
struct BoardImage {
    cv::Mat image;
    double px_per_m;
    double width_m;
    double height_m;
};

// ChArUco board, 7 rows x 5 cols of 30 mm squares with 22 mm DICT_5X5_100 markers.
CharucoCalibrator::Config CharucoConfig() { return { 7, 5, kSquareM, 0.022, "DICT_5X5_100", false }; }

BoardImage RenderCharuco() {
    const auto config = CharucoConfig();
    const cv::aruco::CharucoBoard board(cv::Size(config.cols, config.rows), static_cast<float>(config.square_size_m),
                                        static_cast<float>(config.marker_size_m),
                                        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_5X5_100));
    constexpr double kPxPerM = 4000.0;
    const double width_m = config.cols * config.square_size_m;
    const double height_m = config.rows * config.square_size_m;
    BoardImage out{ {}, kPxPerM, width_m, height_m };
    board.generateImage(cv::Size(static_cast<int>(width_m * kPxPerM), static_cast<int>(height_m * kPxPerM)),
                        out.image, 0, 1);
    return out;
}

// Chessboard with 6 x 9 internal corners (7 x 10 squares of 30 mm) on a one-square white margin.
ChessboardCalibrator::Config ChessboardConfig() { return { 6, 9, kSquareM }; }

BoardImage RenderChessboard() {
    constexpr int kSquarePx = 120;
    const auto config = ChessboardConfig();
    const int squares_y = config.rows + 1, squares_x = config.cols + 1;
    cv::Mat image((squares_y + 2) * kSquarePx, (squares_x + 2) * kSquarePx, CV_8U, cv::Scalar(255));
    for (int r = 0; r < squares_y; ++r) {
        for (int c = 0; c < squares_x; ++c) {
            if ((r + c) % 2 == 0) {
                image(cv::Rect((c + 1) * kSquarePx, (r + 1) * kSquarePx, kSquarePx, kSquarePx)).setTo(0);
            }
        }
    }
    return { image, kSquarePx / config.square_size_m, image.cols * config.square_size_m / kSquarePx,
             image.rows * config.square_size_m / kSquarePx };
}

// The board as seen by TrueCameraMatrix() with the board at pose rvec/tvec (board origin = its
// top-left corner, metres).
cv::Mat RenderView(const BoardImage& board, cv::Vec3d rvec, cv::Vec3d tvec) {
    cv::Mat R;
    cv::Rodrigues(rvec, R);
    const cv::Mat board_to_camera = (cv::Mat_<double>(3, 3) << R.at<double>(0, 0), R.at<double>(0, 1), tvec[0],
                                     R.at<double>(1, 0), R.at<double>(1, 1), tvec[1],
                                     R.at<double>(2, 0), R.at<double>(2, 1), tvec[2]);
    const cv::Mat px_to_m = (cv::Mat_<double>(3, 3) << 1.0 / board.px_per_m, 0, 0, 0, 1.0 / board.px_per_m, 0, 0, 0, 1);
    const cv::Mat homography = TrueCameraMatrix() * board_to_camera * px_to_m;
    cv::Mat view;
    cv::warpPerspective(board.image, view, homography, kImageSize, cv::INTER_LINEAR, cv::BORDER_CONSTANT, 128);
    return view;
}

// Feeds 18 tilted views at two distances to the calibrator and returns how many were captured. With
// partial set, the views tilted one way are shifted so the board runs off the right edge of the image.
int CaptureViews(CameraCalibrator& calibrator, const BoardImage& board, bool partial) {
    calibrator.setMotionThresholds(0.0f, 0.0f);
    int captured = 0;
    for (const double ax : { -0.4, 0.0, 0.4 }) {
        for (const double ay : { -0.4, 0.0, 0.4 }) {
            for (const double z : { 0.5, 0.8 }) {
                const double shift = (partial && ax > 0.0) ? 0.35 : 0.0;
                const cv::Vec3d tvec(-board.width_m / 2 + shift, -board.height_m / 2, z);
                captured += calibrator.detectAndCapture(RenderView(board, { ax, ay, 0.1 }, tvec)) ? 1 : 0;
            }
        }
    }
    return captured;
}

void ExpectTrueIntrinsics(CameraCalibrator& calibrator) {
    cv::Mat camera_matrix, dist_coeffs;
    const double rms = calibrator.calibrate(kImageSize, camera_matrix, dist_coeffs);
    EXPECT_GT(rms, 0.0);
    EXPECT_LT(rms, 0.5);
    EXPECT_NEAR(camera_matrix.at<double>(0, 0), kFocalLength, 0.01 * kFocalLength);
    EXPECT_NEAR(camera_matrix.at<double>(1, 1), kFocalLength, 0.01 * kFocalLength);
    EXPECT_NEAR(camera_matrix.at<double>(0, 2), kCx, 10.0);
    EXPECT_NEAR(camera_matrix.at<double>(1, 2), kCy, 10.0);
}

TEST(CharucoCalibratorTest, RecoversIntrinsicsFromPartialViews) {
    CharucoCalibrator calibrator(CharucoConfig());
    EXPECT_EQ(CaptureViews(calibrator, RenderCharuco(), /*partial=*/true), 18);
    ExpectTrueIntrinsics(calibrator);

    const auto coverage = calibrator.getCoverageStats(kImageSize);
    EXPECT_GT(coverage.max_skew, 0.0f);
    EXPECT_LT(coverage.max_skew, 1.0f);
    EXPECT_GT(coverage.max_scale, 0.0f);
}

TEST(ChessboardCalibratorTest, RecoversIntrinsics) {
    ChessboardCalibrator calibrator(ChessboardConfig());
    EXPECT_EQ(CaptureViews(calibrator, RenderChessboard(), /*partial=*/false), 18);
    ExpectTrueIntrinsics(calibrator);
}

TEST(CameraCalibratorTest, DropsDuplicatePoses) {
    CharucoCalibrator calibrator(CharucoConfig());
    const BoardImage board = RenderCharuco();
    const cv::Mat view = RenderView(board, { 0.2, 0.1, 0.0 }, { -board.width_m / 2, -board.height_m / 2, 0.6 });

    EXPECT_TRUE(calibrator.detectAndCapture(view));
    EXPECT_FALSE(calibrator.detectAndCapture(view));
    EXPECT_EQ(calibrator.getCaptureCount(), 1);
    EXPECT_FALSE(calibrator.getDebugImage().empty());

    calibrator.reset();
    EXPECT_EQ(calibrator.getCaptureCount(), 0);
    EXPECT_TRUE(calibrator.detectAndCapture(view));
}

TEST(CameraCalibratorTest, IgnoresFramesWithoutBoard) {
    const cv::Mat blank(kImageSize, CV_8U, cv::Scalar(128));
    CharucoCalibrator charuco(CharucoConfig());
    ChessboardCalibrator chessboard(ChessboardConfig());
    EXPECT_FALSE(charuco.detectAndCapture(blank));
    EXPECT_FALSE(chessboard.detectAndCapture(blank));
    EXPECT_EQ(charuco.getCaptureCount() + chessboard.getCaptureCount(), 0);

    cv::Mat camera_matrix, dist_coeffs;
    EXPECT_LT(charuco.calibrate(kImageSize, camera_matrix, dist_coeffs), 0.0);
}

TEST(CameraCalibratorTest, SavesCameraInfoYaml) {
    const char* tmp = std::getenv("TEST_TMPDIR");
    const std::filesystem::path dir = std::filesystem::path(tmp ? tmp : "/tmp") / "camera_cal_test";
    std::filesystem::remove_all(dir);

    ChessboardCalibrator calibrator(ChessboardConfig());
    const cv::Mat dist_coeffs = (cv::Mat_<double>(1, 5) << 0.1, -0.2, 0.0, 0.0, 0.05);
    ASSERT_TRUE(calibrator.saveCalibration(dir.string(), "cam0", TrueCameraMatrix(), dist_coeffs, kImageSize, 0.25));

    std::ifstream in(dir / "cam0.yaml");
    ASSERT_TRUE(in.is_open());
    std::stringstream yaml;
    yaml << in.rdbuf();
    EXPECT_NE(yaml.str().find("image_width: 1920"), std::string::npos);
    EXPECT_NE(yaml.str().find("camera_name: cam0"), std::string::npos);
    EXPECT_NE(yaml.str().find("distortion_model: plumb_bob"), std::string::npos);
    EXPECT_NE(yaml.str().find("1400.00000"), std::string::npos);
}

TEST(CharucoCalibratorTest, Validate) {
    EXPECT_FALSE(CharucoCalibrator::validate(CharucoConfig()).has_value());

    auto config = CharucoConfig();
    config.rows = 2;
    EXPECT_TRUE(CharucoCalibrator::validate(config).has_value());

    config = CharucoConfig();
    config.square_size_m = 0.0;
    EXPECT_TRUE(CharucoCalibrator::validate(config).has_value());

    config = CharucoConfig();
    config.marker_size_m = config.square_size_m;
    EXPECT_TRUE(CharucoCalibrator::validate(config).has_value());

    config = CharucoConfig();
    config.aruco_dict = "DICT_NOPE";
    EXPECT_TRUE(CharucoCalibrator::validate(config).has_value());

    // 20 x 20 squares needs 200 markers; DICT_4X4_50 has 50.
    config = CharucoConfig();
    config.rows = config.cols = 20;
    config.aruco_dict = "DICT_4X4_50";
    EXPECT_TRUE(CharucoCalibrator::validate(config).has_value());

    config.aruco_dict = "DICT_4X4_250";
    EXPECT_FALSE(CharucoCalibrator::validate(config).has_value());

    config.aruco_dict = "DICT_NOPE";
    EXPECT_THROW(CharucoCalibrator{ config }, std::invalid_argument);
}

TEST(ChessboardCalibratorTest, Validate) {
    EXPECT_FALSE(ChessboardCalibrator::validate(ChessboardConfig()).has_value());
    EXPECT_TRUE(ChessboardCalibrator::validate({ 1, 9, kSquareM }).has_value());
    EXPECT_TRUE(ChessboardCalibrator::validate({ 6, 9, 0.0 }).has_value());
}

}  // namespace
}  // namespace camera_cal
