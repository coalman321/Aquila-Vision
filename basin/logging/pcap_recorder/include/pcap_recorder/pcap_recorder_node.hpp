#pragma once

#include <atomic>
#include <basin/node_core/node_core.hpp>
#include <chrono>
#include <deque>
#include <logging_msgs/msg/pcap_capture.hpp>
#include <memory>
#include <mutex>
#include <process_lib/process_controller.hpp>
#include <rclcpp/rclcpp.hpp>
#include <status_msgs/msg/fault_report.hpp>
#include <string>
#include <thread>
#include <unordered_map>

#include "pcap_recorder/pcap_recorder_params.hpp"
#include "pcap_recorder/pcap_stream.hpp"

namespace basin::pcap_recorder {

/// Keeps tcpdump running and the last few seconds of its capture in memory. When a matching fault is raised it
/// publishes that buffer, then everything captured for capture.duration more, on pcap/capture, and goes back to
/// buffering.
class PcapRecorderNode : public basin::node_core::NodeCore {
   public:
    explicit PcapRecorderNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    ~PcapRecorderNode() override;

    void onTick() override;

    void shutdown() override;

    /// @brief Starts a capture labelled `trigger`, or, if one is already running, extends it to capture.duration
    /// from now (the extension keeps the first trigger's label).
    void trigger(const std::string& trigger);

   private:
    void faultCallback(status_msgs::msg::FaultReport::ConstSharedPtr msg);

    /// Reader thread: reads tcpdump's stdout and stderr pipes until both close.
    void readTcpdumpOutput(int stdout_fd, int stderr_fd);

    /// Reader-thread side: buffers each packet, and queues it for publishing while a capture runs. Returns false
    /// on output that isn't pcap.
    bool onTcpdumpOutput(const uint8_t* data, std::size_t size);

    /// Reader-thread side: logs a line of tcpdump's stderr, and keeps it for the fault raised if tcpdump exits.
    void onTcpdumpStderr(const std::string& line);

    void startTcpdump();

    /// Asks tcpdump to exit (SIGTERM, so it flushes), SIGKILLs it if it hasn't within a grace period, and joins the
    /// reader thread.
    void stopTcpdump();

    std::unique_ptr<PcapRecorderParams> params_;
    std::vector<std::string> tcpdump_args_;

    rclcpp::Publisher<logging_msgs::msg::PcapCapture>::SharedPtr capture_pub_;
    rclcpp::Subscription<status_msgs::msg::FaultReport>::SharedPtr fault_sub_;

    // Only touched from the executor thread: ProcessController isn't thread-safe
    std::unique_ptr<basin::process::ProcessController> tcpdump_;
    std::chrono::steady_clock::time_point next_start_{};

    // Only touched from the reader thread (or while it isn't running)
    std::thread reader_;
    PcapStreamParser parser_;
    std::atomic<bool> bad_output_{ false };  // set by the reader, acted on by onTick()
    bool shut_down_{ false };

    // Raise time last seen per fault key, so a fault that is re-reported every tick triggers once per period
    std::unordered_map<status_msgs::msg::FaultKey, rclcpp::Time, basin::node_core::detail::FaultKeyHash>
        seen_faults_;

    // Shared between the reader thread and the executor
    std::mutex mutex_;
    PacketBuffer buffer_{ 0, 0 };
    bool capture_active_{ false };
    std::chrono::steady_clock::time_point capture_end_{};
    std::deque<PcapPacketPtr> pending_;
    std::string last_stderr_line_;

    // Executor side: the capture being published
    logging_msgs::msg::PcapCapture capture_template_;
    uint32_t sequence_{ 0 };
    uint64_t capture_count_{ 0 };
    std::shared_ptr<const std::vector<uint8_t>> last_file_header_;
};

}  // namespace basin::pcap_recorder
