// Loads real TensorRT engines through ObjectDetector. The models are synthetic (see
// testdata/make_test_models.py): they emit a fixed set of candidate boxes, so every test knows exactly
// which detections, in which image coordinates, should come out.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <opencv2/core.hpp>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "object_detector_lib/detection.hpp"
#include "object_detector_lib/detector.hpp"
#include "rules_cc/cc/runfiles/runfiles.h"
#include "tools/cuda_test/gpu_available.h"

namespace object_detector {
namespace {

namespace fs = std::filesystem;
using rules_cc::cc::runfiles::Runfiles;

// Environment variables holding each model's runfiles path, set by the BUILD target with $(rlocationpath).
constexpr char kDetectModel[] = "DETECT_MODEL";
constexpr char kSegmentModel[] = "SEGMENT_MODEL";
constexpr char kRfDetrDetectModel[] = "RFDETR_DETECT_MODEL";
constexpr char kRfDetrSegmentModel[] = "RFDETR_SEGMENT_MODEL";

struct Expected {
    int class_id;
    float confidence;
    cv::Rect box;
};

// Mirrors DETECT_BOXES in make_test_models.py, decoded for a 640x640 image (no letterboxing).
const Expected kAnchor0{ 1, 0.9f, cv::Rect(270, 295, 100, 50) };
const Expected kAnchor1{ 1, 0.8f, cv::Rect(274, 295, 100, 50) };
const Expected kAnchor2{ 2, 0.7f, cv::Rect(80, 80, 40, 40) };
const Expected kAnchor5{ 2, 0.6f, cv::Rect(270, 295, 100, 50) };
// The brightness box: scored with the mean of the input tensor.
const Expected kBrightAnchor{ 0, 1.0f, cv::Rect(470, 470, 60, 60) };

// Mirrors RF_DETECT_QUERIES in make_test_models.py, decoded for a 640x640 image.
const Expected kQuery0{ 1, 0.9f, cv::Rect(240, 280, 160, 80) };
const Expected kQuery1{ 1, 0.8f, cv::Rect(250, 280, 160, 80) };
const Expected kQuery2{ 2, 0.7f, cv::Rect(80, 80, 40, 40) };
const Expected kQuery5Class0{ 0, 0.65f, cv::Rect(280, 440, 80, 80) };
const Expected kQuery5Class2{ 2, 0.6f, cv::Rect(280, 440, 80, 80) };
// The red query: its logit is the mean of the input's first (red) channel, which for a pure red image is
// (1 - 0.485) / 0.229 after ImageNet normalization.
const Expected kRedQuery{ 0, 1.f / (1.f + std::exp(-(1.f - 0.485f) / 0.229f)), cv::Rect(470, 470, 60, 60) };

// Detections in a stable order (class, then descending confidence), since the detector's output order
// is an implementation detail.
std::vector<Detection> sorted(std::vector<Detection> detections) {
    std::sort(detections.begin(), detections.end(), [](const Detection &a, const Detection &b) {
        return std::make_tuple(a.classId(), -a.confidence()) < std::make_tuple(b.classId(), -b.confidence());
    });
    return detections;
}

void expectDetections(const std::vector<Detection> &actual, const std::vector<Expected> &expected) {
    const std::vector<Detection> detections = sorted(actual);
    ASSERT_EQ(detections.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        SCOPED_TRACE("detection " + std::to_string(i));
        EXPECT_EQ(detections[i].classId(), expected[i].class_id);
        EXPECT_NEAR(detections[i].confidence(), expected[i].confidence, 1e-5);
        EXPECT_EQ(detections[i].bbox(), expected[i].box);
    }
}

cv::Mat blackImage(int width, int height) { return cv::Mat::zeros(height, width, CV_8UC3); }
cv::Mat whiteImage(int width, int height) { return cv::Mat(height, width, CV_8UC3, cv::Scalar::all(255)); }
cv::Mat bgrImage(int width, int height, cv::Scalar bgr) { return cv::Mat(height, width, CV_8UC3, bgr); }

class DetectorModelTest : public testing::Test {
   protected:
    // Copies the models somewhere writable, since a .onnx is converted to a .trt next to it. Every test
    // shares these copies, so only the first to load each model pays for building its engine.
    static void SetUpTestSuite() {
        std::string error;
        runfiles_.reset(Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error));
        ASSERT_NE(runfiles_, nullptr) << error;

        model_dir_ = fs::path(testing::TempDir()) / "models";
        fs::create_directories(model_dir_);
        for (const char *model : { kDetectModel, kSegmentModel, kRfDetrDetectModel, kRfDetrSegmentModel }) {
            const fs::path source = sourceModel(model);
            fs::copy_file(source, model_dir_ / source.filename(), fs::copy_options::overwrite_existing);
        }
    }

    static void TearDownTestSuite() { runfiles_.reset(); }

    // The read-only original of a model in the runfiles, named by the environment variable holding its
    // runfiles path.
    static fs::path sourceModel(const char *env_var) {
        const char *rlocation = std::getenv(env_var);
        if (rlocation == nullptr) {
            throw std::runtime_error(std::string(env_var) + " is not set; run this test with bazel test");
        }
        return fs::path(runfiles_->Rlocation(rlocation));
    }

    void SetUp() override {
        std::string reason;
        if (!cuda_test::GpuAvailable(&reason)) {
            GTEST_SKIP() << reason;
        }
    }

    static fs::path detectModel() { return model_dir_ / "detect_3_classes.onnx"; }
    static fs::path segmentModel() { return model_dir_ / "segment_2_classes.onnx"; }
    static fs::path rfDetrDetectModel() { return model_dir_ / "rfdetr_detect_4_classes.onnx"; }
    static fs::path rfDetrSegmentModel() { return model_dir_ / "rfdetr_segment_2_classes.onnx"; }

    static inline std::unique_ptr<Runfiles> runfiles_;
    static inline fs::path model_dir_;
};

TEST_F(DetectorModelTest, BuildsAnEngineFromOnnxOnceAndReusesIt) {
    // A directory of its own, so the engine is built here rather than by an earlier test.
    const fs::path dir = fs::path(testing::TempDir()) / "fresh_engine";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path onnx = dir / "detect_3_classes.onnx";
    const fs::path engine = dir / "detect_3_classes.trt";
    fs::copy_file(sourceModel(kDetectModel), onnx);

    {
        ObjectDetector detector(onnx, DETECT, 0.5f, 0.5f);
        EXPECT_EQ(detector.numClasses(), 3);
    }
    ASSERT_TRUE(fs::exists(engine));
    EXPECT_GT(fs::file_size(engine), 0u);

    // Loading the .onnx again uses the engine already there instead of rebuilding it.
    const fs::file_time_type built = fs::last_write_time(engine);
    ObjectDetector reloaded(onnx, DETECT, 0.5f, 0.5f);
    EXPECT_EQ(fs::last_write_time(engine), built);
    EXPECT_EQ(reloaded.numClasses(), 3);
}

TEST_F(DetectorModelTest, LoadsASerializedEngineDirectly) {
    cv::Mat image = blackImage(640, 640);
    const std::vector<Detection> from_onnx = ObjectDetector(detectModel(), DETECT, 0.5f, 0.5f).runDetection(image);

    fs::path engine = detectModel();
    engine.replace_extension("trt");
    ObjectDetector detector(engine, DETECT, 0.5f, 0.5f);
    EXPECT_EQ(detector.numClasses(), 3);
    expectDetections(detector.runDetection(image), { kAnchor0, kAnchor2, kAnchor5 });
    EXPECT_EQ(from_onnx.size(), 3u);
}

TEST_F(DetectorModelTest, DecodesBoxesOnASquareImage) {
    // Anchor 1 overlaps anchor 0 (same class, IoU 0.92): suppressed. Anchor 3 scores 0.3: filtered.
    // Anchor 5 is anchor 0's box under another class, which NMS leaves alone.
    ObjectDetector detector(detectModel(), DETECT, 0.5f, 0.5f);
    cv::Mat image = blackImage(640, 640);
    expectDetections(detector.runDetection(image), { kAnchor0, kAnchor2, kAnchor5 });
}

TEST_F(DetectorModelTest, IouThresholdControlsSuppression) {
    ObjectDetector detector(detectModel(), DETECT, 0.5f, 0.95f);
    cv::Mat image = blackImage(640, 640);
    expectDetections(detector.runDetection(image), { kAnchor0, kAnchor1, kAnchor2, kAnchor5 });
}

TEST_F(DetectorModelTest, ConfidenceThresholdFiltersDetections) {
    ObjectDetector detector(detectModel(), DETECT, 0.75f, 0.5f);
    cv::Mat image = blackImage(640, 640);
    expectDetections(detector.runDetection(image), { kAnchor0 });
}

TEST_F(DetectorModelTest, ImageReachesTheEngine) {
    ObjectDetector detector(detectModel(), DETECT, 0.5f, 0.5f);
    cv::Mat black = blackImage(640, 640);
    cv::Mat white = whiteImage(640, 640);
    expectDetections(detector.runDetection(black), { kAnchor0, kAnchor2, kAnchor5 });
    expectDetections(detector.runDetection(white), { kBrightAnchor, kAnchor0, kAnchor2, kAnchor5 });
}

// Anchor 0 sits at the centre of the letterboxed input, so it has to land at the centre of any image.
TEST_F(DetectorModelTest, MapsLetterboxedBoxesBackToTheImage) {
    ObjectDetector detector(detectModel(), DETECT, 0.75f, 0.5f);

    // Wide: scaled by 3 to 640x360, with 140 rows of padding above and below.
    cv::Mat wide = blackImage(1920, 1080);
    expectDetections(detector.runDetection(wide), { { 1, 0.9f, cv::Rect(810, 465, 300, 150) } });

    // Tall: scaled by 2 to 320x640, with 160 columns of padding either side.
    cv::Mat tall = blackImage(640, 1280);
    expectDetections(detector.runDetection(tall), { { 1, 0.9f, cv::Rect(220, 590, 200, 100) } });
}

TEST_F(DetectorModelTest, OddPaddingStillFillsTheInput) {
    // 1000x333 scales to 640x213, leaving 427 rows of padding: 213 above and 214 below. The brightness
    // box's score is the input's mean, so it shows that 213 of 640 rows are image. (It can't show the
    // input is a full 640 rows: when it was 639, the copy to the GPU read past the end of the host
    // buffer, which only a memory checker catches.)
    ObjectDetector detector(detectModel(), DETECT, 0.2f, 0.5f);
    cv::Mat image = whiteImage(1000, 333);
    const std::vector<Detection> detections = sorted(detector.runDetection(image));
    ASSERT_FALSE(detections.empty());
    ASSERT_EQ(detections.front().classId(), 0);
    EXPECT_NEAR(detections.front().confidence(), 213. / 640., 1e-3);
}

TEST_F(DetectorModelTest, RejectsCorruptModels) {
    const fs::path dir = fs::path(testing::TempDir()) / "corrupt";
    fs::create_directories(dir);

    const fs::path onnx = dir / "corrupt.onnx";
    std::ofstream(onnx) << "not an onnx model";
    EXPECT_THROW(ObjectDetector(onnx, DETECT, 0.5f, 0.5f), std::runtime_error);
    EXPECT_FALSE(fs::exists(dir / "corrupt.trt")) << "a failed conversion must not leave an engine behind";

    const fs::path engine = dir / "garbage.trt";
    std::ofstream(engine) << "not a TensorRT engine";
    EXPECT_THROW(ObjectDetector(engine, DETECT, 0.5f, 0.5f), std::runtime_error);
}

TEST_F(DetectorModelTest, SegmentsASquareImage) {
    ObjectDetector detector(segmentModel(), SEGMENT, 0.5f, 0.5f);
    EXPECT_EQ(detector.numClasses(), 2);

    cv::Mat image = blackImage(640, 640);
    std::vector<Detection> detections = detector.runDetection(image);
    ASSERT_EQ(detections.size(), 1u);
    EXPECT_EQ(detections[0].classId(), 0);
    EXPECT_NEAR(detections[0].confidence(), 0.9f, 1e-5);
    EXPECT_EQ(detections[0].bbox(), cv::Rect(0, 0, 640, 640));

    // The mask is the top half of the input. Rows near the edge are interpolated, so check clear of it.
    cv::Mat mask = detections[0].mask();
    ASSERT_EQ(mask.size(), image.size());
    ASSERT_EQ(mask.type(), CV_8UC3);
    EXPECT_EQ(cv::countNonZero(mask.row(300).reshape(1)), 640 * 3);
    EXPECT_EQ(cv::countNonZero(mask.row(340).reshape(1)), 0);
}

TEST_F(DetectorModelTest, SegmentationMasksLineUpOnALetterboxedImage) {
    // 1280x640 is letterboxed with 160 rows of padding (320 at image scale) above and below, so the top
    // half of the input is still the top half of the image.
    ObjectDetector detector(segmentModel(), SEGMENT, 0.5f, 0.5f);
    cv::Mat image = blackImage(1280, 640);
    std::vector<Detection> detections = detector.runDetection(image);
    ASSERT_EQ(detections.size(), 1u);

    cv::Mat mask = detections[0].mask();
    ASSERT_EQ(mask.size(), image.size());
    EXPECT_EQ(cv::countNonZero(mask.row(0).reshape(1)), 1280 * 3);
    EXPECT_EQ(cv::countNonZero(mask.row(300).reshape(1)), 1280 * 3);
    EXPECT_EQ(cv::countNonZero(mask.row(340).reshape(1)), 0);
    EXPECT_EQ(cv::countNonZero(mask.row(639).reshape(1)), 0);
}

TEST_F(DetectorModelTest, BuildsAnRfDetrEngineFromOnnxOnceAndReusesIt) {
    const fs::path dir = fs::path(testing::TempDir()) / "fresh_rfdetr_engine";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path onnx = dir / "rfdetr_detect_4_classes.onnx";
    const fs::path engine = dir / "rfdetr_detect_4_classes.trt";
    fs::copy_file(sourceModel(kRfDetrDetectModel), onnx);

    {
        ObjectDetector detector(onnx, DETECT, 0.5f, 0.5f, ModelArchitecture::RF_DETR);
        EXPECT_EQ(detector.numClasses(), 4);
    }
    ASSERT_TRUE(fs::exists(engine));
    EXPECT_GT(fs::file_size(engine), 0u);

    const fs::file_time_type built = fs::last_write_time(engine);
    ObjectDetector reloaded(onnx, DETECT, 0.5f, 0.5f, ModelArchitecture::RF_DETR);
    EXPECT_EQ(fs::last_write_time(engine), built);

    // And the engine on its own gives the same detections.
    ObjectDetector fromEngine(engine, DETECT, 0.5f, 0.5f, ModelArchitecture::RF_DETR);
    cv::Mat image = blackImage(640, 640);
    expectDetections(fromEngine.runDetection(image), { kQuery5Class0, kQuery0, kQuery1, kQuery2, kQuery5Class2 });
}

TEST_F(DetectorModelTest, RfDetrScoresEveryClassOfEveryQueryWithoutNms) {
    // Query 1 overlaps query 0 with the same class, and both survive. Query 5 scores two classes, and
    // both come out. Query 3 scores 0.3: filtered.
    ObjectDetector detector(rfDetrDetectModel(), DETECT, 0.5f, 0.5f, ModelArchitecture::RF_DETR);
    cv::Mat image = blackImage(640, 640);
    expectDetections(detector.runDetection(image), { kQuery5Class0, kQuery0, kQuery1, kQuery2, kQuery5Class2 });
}

TEST_F(DetectorModelTest, RfDetrConfidenceThresholdFiltersDetections) {
    ObjectDetector detector(rfDetrDetectModel(), DETECT, 0.75f, 0.5f, ModelArchitecture::RF_DETR);
    cv::Mat image = blackImage(640, 640);
    expectDetections(detector.runDetection(image), { kQuery0, kQuery1 });
}

TEST_F(DetectorModelTest, RfDetrFeedsTheEngineNormalizedRgb) {
    ObjectDetector detector(rfDetrDetectModel(), DETECT, 0.75f, 0.5f, ModelArchitecture::RF_DETR);
    // OpenCV images are BGR, so red is the last channel; the model's first channel has to be red.
    cv::Mat red = bgrImage(640, 640, cv::Scalar(0, 0, 255));
    cv::Mat blue = bgrImage(640, 640, cv::Scalar(255, 0, 0));
    const std::vector<Detection> detections = sorted(detector.runDetection(red));
    ASSERT_EQ(detections.size(), 3u);
    EXPECT_EQ(detections[0].classId(), kRedQuery.class_id);
    EXPECT_NEAR(detections[0].confidence(), kRedQuery.confidence, 1e-4);
    EXPECT_EQ(detections[0].bbox(), kRedQuery.box);
    expectDetections(detector.runDetection(blue), { kQuery0, kQuery1 });
}

// RF-DETR stretches the image to its input rather than letterboxing it, so normalized boxes scale by
// the image's own width and height.
TEST_F(DetectorModelTest, RfDetrMapsBoxesBackToAStretchedImage) {
    ObjectDetector detector(rfDetrDetectModel(), DETECT, 0.85f, 0.5f, ModelArchitecture::RF_DETR);

    cv::Mat wide = blackImage(1280, 640);
    expectDetections(detector.runDetection(wide), { { 1, 0.9f, cv::Rect(480, 280, 320, 80) } });

    cv::Mat tall = blackImage(320, 960);
    expectDetections(detector.runDetection(tall), { { 1, 0.9f, cv::Rect(120, 420, 80, 120) } });
}

TEST_F(DetectorModelTest, RfDetrSegmentsEachQueryWithItsOwnMask) {
    ObjectDetector detector(rfDetrSegmentModel(), SEGMENT, 0.5f, 0.5f, ModelArchitecture::RF_DETR);
    EXPECT_EQ(detector.numClasses(), 2);

    cv::Mat image = blackImage(1280, 640);
    std::vector<Detection> detections = sorted(detector.runDetection(image));
    ASSERT_EQ(detections.size(), 2u);
    for (Detection &detection : detections) {
        EXPECT_EQ(detection.bbox(), cv::Rect(0, 0, 1280, 640));
        ASSERT_EQ(detection.mask().size(), image.size());
        ASSERT_EQ(detection.mask().type(), CV_8UC3);
    }

    // Query 0 (class 0) masks the top half, query 1 (class 1) the left half. Check clear of the edges,
    // which are interpolated.
    EXPECT_EQ(detections[0].classId(), 0);
    EXPECT_NEAR(detections[0].confidence(), 0.9f, 1e-5);
    cv::Mat top = detections[0].mask();
    EXPECT_EQ(cv::countNonZero(top.row(280).reshape(1)), 1280 * 3);
    EXPECT_EQ(cv::countNonZero(top.row(360).reshape(1)), 0);

    EXPECT_EQ(detections[1].classId(), 1);
    EXPECT_NEAR(detections[1].confidence(), 0.8f, 1e-5);
    cv::Mat left = detections[1].mask();
    EXPECT_EQ(cv::countNonZero(left.col(600).reshape(1)), 640 * 3);
    EXPECT_EQ(cv::countNonZero(left.col(680).reshape(1)), 0);
}

TEST_F(DetectorModelTest, RfDetrSegmentationModelCanJustDetect) {
    ObjectDetector detector(rfDetrSegmentModel(), DETECT, 0.5f, 0.5f, ModelArchitecture::RF_DETR);
    cv::Mat image = blackImage(640, 640);
    std::vector<Detection> detections = detector.runDetection(image);
    ASSERT_EQ(detections.size(), 2u);
    EXPECT_THROW(detections[0].mask(), std::runtime_error);
}

TEST_F(DetectorModelTest, RfDetrRejectsModelsWithTheWrongOutputs) {
    // A detection-only model can't segment.
    EXPECT_THROW(ObjectDetector(rfDetrDetectModel(), SEGMENT, 0.5f, 0.5f, ModelArchitecture::RF_DETR),
                 std::runtime_error);

    // A YOLOv8 model has none of RF-DETR's outputs. A copy of its own, so the engine it builds stays here.
    const fs::path dir = fs::path(testing::TempDir()) / "yolo_as_rfdetr";
    fs::create_directories(dir);
    fs::copy_file(sourceModel(kDetectModel), dir / "detect_3_classes.onnx",
                  fs::copy_options::overwrite_existing);
    EXPECT_THROW(ObjectDetector(dir / "detect_3_classes.onnx", DETECT, 0.5f, 0.5f, ModelArchitecture::RF_DETR),
                 std::runtime_error);
}

}  // namespace
}  // namespace object_detector
