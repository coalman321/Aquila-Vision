#include <arv.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <string>

#include "basler_driver/basler_driver_node.hpp"

using namespace std::chrono_literals;
using sensor_msgs::msg::Image;

namespace {

constexpr auto TIMEOUT = 10s;

// Runs the real node against Aravis' simulated camera (the "Fake" interface, serial "1"), with a probe
// node subscribed to its images.
class BaslerDriverNodeTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // The BCR build of Aravis doesn't embed the simulated camera's GenICam description, so hand it
        // a copy (test/testdata/arv-fake-camera.xml, from Aravis itself).
        arv_set_fake_camera_genicam_filename("drivers/basler_driver/test/testdata/arv-fake-camera.xml");
        arv_enable_interface("Fake");
        // Bazel runs tests concurrently, so take a DDS domain of our own.
        rclcpp::InitOptions init_options;
        init_options.set_domain_id(static_cast<size_t>(::getpid()) % 102);
        rclcpp::init(0, nullptr, init_options);

        rclcpp::NodeOptions options;
        options.append_parameter_override("device_serial", std::string("1"));
        options.append_parameter_override("camera_frame", std::string("fake_camera"));
        options.append_parameter_override("image_encoding", std::string("mono8"));
        options.append_parameter_override("frame_rate", 20.0);
        // An integer, as a launch file reading JSON may pass it.
        options.append_parameter_override("exposure", int64_t{ 2000 });
        options.append_parameter_override("gain", 0.5);
        options.append_parameter_override("roi.width", int64_t{ 320 });
        options.append_parameter_override("roi.height", int64_t{ 240 });
        node_ = std::make_shared<basler_driver::BaslerDriverNode>(options);

        probe_ = std::make_shared<rclcpp::Node>("basler_probe");
        image_sub_ = probe_->create_subscription<Image>("/basler_driver/image_raw", rclcpp::QoS(10),
                                                        [this](Image::ConstSharedPtr msg) { last_image_ = *msg; });
        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(node_);
        executor_->add_node(probe_);
    }

    void TearDown() override {
        image_sub_.reset();
        executor_.reset();
        probe_.reset();
        node_.reset();
        rclcpp::shutdown();
        arv_disable_interface("Fake");
    }

    template <typename Predicate>
    bool spinUntil(Predicate done) {
        const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
        while (std::chrono::steady_clock::now() < deadline) {
            executor_->spin_some(20ms);
            if (done()) {
                return true;
            }
        }
        return false;
    }

    void spinFor(std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) {
            executor_->spin_some(10ms);
        }
    }

    // Waits for an image published after this call.
    std::optional<Image> nextImage() {
        last_image_.reset();
        if (!spinUntil([this] { return last_image_.has_value(); })) {
            return std::nullopt;
        }
        return last_image_;
    }

    rcl_interfaces::msg::SetParametersResult set(const rclcpp::Parameter& parameter) {
        return node_->set_parameter(parameter);
    }

    std::shared_ptr<basler_driver::BaslerDriverNode> node_;
    std::shared_ptr<rclcpp::Node> probe_;
    rclcpp::Subscription<Image>::SharedPtr image_sub_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
    std::optional<Image> last_image_;
};

TEST_F(BaslerDriverNodeTest, PublishesSoftwareTriggeredFrames) {
    const std::optional<Image> image = nextImage();
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->encoding, "mono8");
    EXPECT_EQ(image->header.frame_id, "fake_camera");
    EXPECT_EQ(image->width, 320u);
    EXPECT_EQ(image->height, 240u);
    EXPECT_EQ(image->step, 320u);
    EXPECT_EQ(image->data.size(), 320u * 240u);
}

TEST_F(BaslerDriverNodeTest, AppliesParametersLive) {
    ASSERT_TRUE(nextImage().has_value());

    EXPECT_TRUE(set(rclcpp::Parameter("exposure", 5000.0)).successful);
    EXPECT_TRUE(set(rclcpp::Parameter("gain", 0.2)).successful);

    EXPECT_TRUE(set(rclcpp::Parameter("roi.width", int64_t{ 200 })).successful);
    std::optional<Image> image = nextImage();
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->width, 200u);
    EXPECT_EQ(image->height, 240u);

    EXPECT_TRUE(set(rclcpp::Parameter("image_encoding", std::string("mono16"))).successful);
    image = nextImage();
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->encoding, "mono16");
    EXPECT_EQ(image->step, 400u);
}

TEST_F(BaslerDriverNodeTest, RejectsWhatTheCameraCannotDo) {
    // The simulated camera has no BGR8 pixel format.
    const auto result = set(rclcpp::Parameter("image_encoding", std::string("bgr8")));
    EXPECT_FALSE(result.successful);
    EXPECT_EQ(node_->get_parameter("image_encoding").as_string(), "mono8");

    EXPECT_FALSE(set(rclcpp::Parameter("device_serial", std::string("2"))).successful);
    EXPECT_FALSE(set(rclcpp::Parameter("binning_x", int64_t{ 64 })).successful);
}

TEST_F(BaslerDriverNodeTest, DeclaresOnlyTheCamerasFeatures) {
    // The simulated camera has trigger features but no black level or PGI.
    ASSERT_TRUE(node_->has_parameter("trigger_mode"));
    EXPECT_TRUE(node_->get_parameter("trigger_mode").as_bool());
    EXPECT_EQ(node_->get_parameter("trigger_source").as_string(), "Software");
    EXPECT_FALSE(node_->has_parameter("black_level"));
    EXPECT_FALSE(node_->has_parameter("pgi_mode"));

    EXPECT_FALSE(set(rclcpp::Parameter("trigger_source", std::string("NoSuchLine"))).successful);
}

TEST_F(BaslerDriverNodeTest, SleepingStopsImages) {
    ASSERT_TRUE(nextImage().has_value());
    ASSERT_TRUE(set(rclcpp::Parameter("sleeping", true)).successful);
    // Let a frame already in flight arrive, then expect silence.
    spinFor(200ms);
    last_image_.reset();
    spinFor(500ms);
    EXPECT_FALSE(last_image_.has_value());

    ASSERT_TRUE(set(rclcpp::Parameter("sleeping", false)).successful);
    EXPECT_TRUE(nextImage().has_value());
}

}  // namespace
