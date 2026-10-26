#include "pcap_recorder/pcap_recorder_node.hpp"

#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <sstream>

using namespace std::placeholders;

namespace basin::pcap_recorder {

// Faults this node raises itself. Its own reports are never treated as triggers.
static constexpr const char* FAULT_SYSTEM = "pcap_recorder";
static constexpr const char* FAULT_TCPDUMP_DOWN = "tcpdump_not_running";
static constexpr std::chrono::duration<double> FAULT_TOV{ 1.0 };

namespace {
int64_t systemNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

builtin_interfaces::msg::Time toStamp(int64_t ns) { return rclcpp::Time(ns, RCL_SYSTEM_TIME); }
}  // namespace

PcapRecorderNode::PcapRecorderNode(const rclcpp::NodeOptions& options) :
    basin::node_core::NodeCore("pcap_recorder", "", options) {
    params_ = std::make_unique<PcapRecorderParams>(this);
    tcpdump_args_ = buildTcpdumpArgs(*params_);
    buffer_ = PacketBuffer(static_cast<std::size_t>(params_->buffer_max_bytes),
                           static_cast<int64_t>(params_->buffer_max_age * 1e9));

    capture_pub_ = create_publisher<logging_msgs::msg::PcapCapture>(
        "pcap/capture", rclcpp::QoS(static_cast<std::size_t>(params_->queue_depth)).reliable());
    fault_sub_ = create_subscription<status_msgs::msg::FaultReport>(
        "/state/fault_report", rclcpp::QoS(20).reliable(), std::bind(&PcapRecorderNode::faultCallback, this, _1));

    tcpdump_ = std::make_unique<basin::process::ProcessController>(params_->tcpdump_path, tcpdump_args_, "tcpdump");
    tcpdump_->setOutputMode(basin::process::OutputMode::PIPE);
    // Don't leave it capturing into a pipe nobody reads if the node dies
    tcpdump_->setParentDeathSignal(basin::process::SignalType::TERM);
    startTcpdump();

    std::ostringstream command;
    command << params_->tcpdump_path;
    for (const std::string& arg : tcpdump_args_) {
        command << " " << arg;
    }
    RCLCPP_INFO_STREAM(get_logger(), "pcap recorder started: " << command.str());
}

PcapRecorderNode::~PcapRecorderNode() { shutdown(); }

void PcapRecorderNode::startTcpdump() {
    next_start_ = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                         std::chrono::duration<double>(params_->restart_delay));
    // The reader has to be done with the last run's pipes (and the parser) before launchProcess() replaces them
    stopTcpdump();
    parser_.reset();
    bad_output_ = false;
    try {
        tcpdump_->launchProcess();
    } catch (const std::runtime_error& e) {
        RCLCPP_ERROR_STREAM(get_logger(), "Failed to start tcpdump: " << e.what());
        return;
    }
    reader_ = std::thread(&PcapRecorderNode::readTcpdumpOutput, this, tcpdump_->getStdoutFd(), tcpdump_->getStderrFd());
}

void PcapRecorderNode::stopTcpdump() {
    static constexpr auto GRACE = std::chrono::seconds(2);

    if (tcpdump_->getProcessStatus().process_alive) {
        try {
            tcpdump_->signalProccess(basin::process::SignalType::TERM);
        } catch (const std::runtime_error&) {
        }
        const auto deadline = std::chrono::steady_clock::now() + GRACE;
        while (tcpdump_->getProcessStatus().process_alive && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (tcpdump_->getProcessStatus().process_alive) {
            try {
                tcpdump_->signalProccess(basin::process::SignalType::KILL);
            } catch (const std::runtime_error&) {
            }
        }
    }
    // Its pipes close with it, which ends the reader
    if (reader_.joinable()) {
        reader_.join();
    }
}

void PcapRecorderNode::readTcpdumpOutput(int stdout_fd, int stderr_fd) {
    pollfd fds[2] = { { stdout_fd, POLLIN, 0 }, { stderr_fd, POLLIN, 0 } };
    std::vector<uint8_t> buffer(64 * 1024);
    std::string stderr_line;

    // The controller owns the descriptors, so they are only dropped from the poll set here, not closed
    while (fds[0].fd >= 0 || fds[1].fd >= 0) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            RCLCPP_ERROR_STREAM(get_logger(), "Polling tcpdump's output failed: " << std::strerror(errno));
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || fds[i].revents == 0) {
                continue;
            }
            const ssize_t count = read(fds[i].fd, buffer.data(), buffer.size());
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                fds[i].fd = -1;  // poll ignores negative descriptors
                continue;
            }

            if (i == 0) {
                // After bad output, keep draining (so tcpdump never blocks on a full pipe) until onTick() stops it
                if (!bad_output_ && !onTcpdumpOutput(buffer.data(), static_cast<std::size_t>(count))) {
                    bad_output_ = true;
                }
            } else {
                stderr_line.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(count));
                std::size_t newline;
                while ((newline = stderr_line.find('\n')) != std::string::npos) {
                    onTcpdumpStderr(stderr_line.substr(0, newline));
                    stderr_line.erase(0, newline + 1);
                }
            }
        }
    }
    if (!stderr_line.empty()) {
        onTcpdumpStderr(stderr_line);
    }
}

void PcapRecorderNode::onTcpdumpStderr(const std::string& line) {
    RCLCPP_INFO_STREAM(get_logger(), "tcpdump: " << line);
    std::lock_guard<std::mutex> lock(mutex_);
    last_stderr_line_ = line;
}

void PcapRecorderNode::onTick() {
    if (shut_down_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (bad_output_.exchange(false)) {
        try {
            tcpdump_->signalProccess(basin::process::SignalType::TERM);
        } catch (const std::runtime_error&) {
        }
    }
    const basin::process::ControlStatus status = tcpdump_->getProcessStatus();
    if (!status.process_alive) {
        std::string reason;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            reason = last_stderr_line_;
        }
        std::string exit = "is not running";
        if (status.exit_code >= 0) {
            exit = "exited with status " + std::to_string(status.exit_code);
        } else if (status.exit_signal != 0) {
            exit = "was killed by signal " + std::to_string(status.exit_signal);
        }
        sendFaultReport(FAULT_SYSTEM, FAULT_TCPDUMP_DOWN, FAULT_TOV,
                        "tcpdump " + exit + (reason.empty() ? "" : ": " + reason));
        if (now >= next_start_) {
            RCLCPP_WARN_STREAM(get_logger(), "tcpdump " << exit << ", restarting it");
            startTcpdump();
        }
    }

    std::deque<PcapPacketPtr> packets;
    bool was_active = false;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.evictExpired(systemNowNs());
        was_active = capture_active_;
        packets.swap(pending_);
        if (capture_active_ && now >= capture_end_) {
            capture_active_ = false;
            finished = true;
        }
    }
    if (!was_active) {
        return;
    }

    std::vector<PcapChunk> chunks = chunkPackets(packets, static_cast<std::size_t>(params_->max_message_bytes));
    if (!packets.empty()) {
        last_file_header_ = packets.back()->file_header;
    }
    if (finished && chunks.empty()) {
        // Close the capture with a message of its own, an empty pcap file if any header was ever seen
        PcapChunk& last = chunks.emplace_back();
        if (last_file_header_) {
            last.data.assign(last_file_header_->begin(), last_file_header_->end());
        }
    }

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        logging_msgs::msg::PcapCapture msg = capture_template_;
        msg.header.stamp = get_clock()->now();
        msg.sequence = sequence_++;
        msg.is_last = finished && i + 1 == chunks.size();
        msg.packet_count = chunks[i].packet_count;
        if (chunks[i].packet_count > 0) {
            msg.first_packet_time = toStamp(chunks[i].first_timestamp_ns);
            msg.last_packet_time = toStamp(chunks[i].last_timestamp_ns);
        }
        msg.data = std::move(chunks[i].data);
        capture_pub_->publish(msg);
    }

    if (finished) {
        RCLCPP_INFO_STREAM(get_logger(), "Capture " << capture_template_.capture_id << " finished after "
                                                    << sequence_ << " messages");
    }
}

void PcapRecorderNode::trigger(const std::string& trigger) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                            std::chrono::duration<double>(params_->capture_duration));
    std::size_t buffered_packets = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capture_active_) {
            capture_end_ = end;
            RCLCPP_INFO_STREAM(get_logger(), "'" << trigger << "' extended capture " << capture_template_.capture_id);
            return;
        }
        buffer_.evictExpired(systemNowNs());
        pending_ = buffer_.drain();
        buffered_packets = pending_.size();
        capture_active_ = true;
        capture_end_ = end;
    }

    // Executor-side state, only read by onTick()
    const rclcpp::Time now = get_clock()->now();
    const std::time_t wall = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&wall, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", &tm);

    capture_template_ = logging_msgs::msg::PcapCapture();
    capture_template_.capture_id = getHostname() + "_" + stamp + "_" + std::to_string(capture_count_++);
    capture_template_.trigger = trigger;
    capture_template_.trigger_time = now;
    capture_template_.interface = params_->interface;
    sequence_ = 0;

    RCLCPP_WARN_STREAM(get_logger(), "'" << trigger << "' started capture " << capture_template_.capture_id << " with "
                                         << buffered_packets << " buffered packets, publishing for "
                                         << params_->capture_duration << " s");
}

void PcapRecorderNode::faultCallback(status_msgs::msg::FaultReport::ConstSharedPtr msg) {
    if (msg->source == get_fully_qualified_name() ||
        !faultMatches(params_->trigger_faults, msg->key.fault_system, msg->key.fault_name)) {
        return;
    }
    // An already-expired report is history (a late-joining replay, say), not a fault happening now
    if (rclcpp::Time(msg->expiration_time) < get_clock()->now()) {
        return;
    }
    // Reporters repeat an active fault with its original raise time; only a new raise is a new fault
    const rclcpp::Time raise_time(msg->raise_time);
    auto seen = seen_faults_.find(msg->key);
    if (seen != seen_faults_.end() && seen->second == raise_time) {
        return;
    }
    seen_faults_.insert_or_assign(msg->key, raise_time);

    trigger(msg->key.fault_system + "/" + msg->key.fault_name);
}

bool PcapRecorderNode::onTcpdumpOutput(const uint8_t* data, std::size_t size) {
    std::vector<PcapPacketPtr> packets;
    try {
        parser_.feed(data, size, [&packets](PcapPacketPtr packet) { packets.push_back(std::move(packet)); });
    } catch (const std::runtime_error& e) {
        RCLCPP_ERROR_STREAM(get_logger(), "Bad output from tcpdump, restarting it: " << e.what());
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (PcapPacketPtr& packet : packets) {
        if (capture_active_) {
            pending_.push_back(packet);
        }
        buffer_.push(std::move(packet));
    }
    return true;
}

void PcapRecorderNode::shutdown() {
    if (shut_down_) {
        return;
    }
    shut_down_ = true;
    heartbeat_->sendBeat(basin::monitor_core::ProcessState::HALTING);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capture_active_) {
            RCLCPP_WARN_STREAM(get_logger(), "Shutting down mid-capture; " << capture_template_.capture_id
                                                                           << " ends without its last message");
        }
    }
    stopTcpdump();
}

}  // namespace basin::pcap_recorder

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(basin::pcap_recorder::PcapRecorderNode)
