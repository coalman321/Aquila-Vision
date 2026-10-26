#include "timesync_ros/sync_node.hpp"

#include <chrono>

using namespace std::chrono_literals;
using namespace std::placeholders;

namespace timesync_ros {
SyncNode::SyncNode(const rclcpp::NodeOptions& options) : basin::node_core::NodeCore("sync_node", "", options) {
    params_ = std::make_unique<SyncNodeParams>(this);

    // load the PTP process args
    std::vector<std::string> ptp4l_args{ "-m", "--tx_timestamp_timeout", "200" };
    std::vector<std::string> phc2sys_args{ "-m", "-w" };
    if (params_->is_grandmaster) {
        ptp4l_args.push_back("--masterOnly");
        ptp4l_args.push_back("1");

        // Force the realtime clock to be the grandmaster source
        phc2sys_args.push_back("-s");
        phc2sys_args.push_back("CLOCK_REALTIME");
        mode_ = SyncMode::GRANDMASTER;
    } else {
        ptp4l_args.push_back("--slaveOnly");
        ptp4l_args.push_back("1");

        // update the realtime clock from the ptp clock
        phc2sys_args.push_back("-c");
        phc2sys_args.push_back("CLOCK_REALTIME");
        mode_ = SyncMode::SLAVE_WAITING;
    }
    for (const std::string& interface : params_->interfaces) {
        ptp4l_args.push_back("-i");
        ptp4l_args.push_back(interface);

        // make sure we add each interface in the right mode
        phc2sys_args.push_back((params_->is_grandmaster ? "-c" : "-s"));
        phc2sys_args.push_back(interface);
    }

    // force the process to ignore sigchld so we dont get zombies
    basin::process::ProcessController::setIgnoreSigchild(true);

    // load the PTP4L process
    std::string ptp4l_proc_name{ "ptp4l_" + std::string(params_->is_grandmaster ? "gm" : "sl") };
    ptp4l_proc_ = std::make_unique<basin::process::ProcessController>(params_->ptp4l_path, ptp4l_args, ptp4l_proc_name);

    // Load the phc2sys process
    std::string phc2sys_proc_name{ "phc2sys_" + std::string(params_->is_grandmaster ? "gm" : "sl") };
    phc2sys_proc_ =
        std::make_unique<basin::process::ProcessController>(params_->phc2sys_path, phc2sys_args, phc2sys_proc_name);

    // Load the correct ros_contexts
    if (params_->is_grandmaster) {
        sync_sub_ = nullptr;
        sync_pub_ = create_publisher<timesync_msgs::msg::SyncStatus>("sync/grandmaster", rclcpp::SystemDefaultsQoS());
    } else {
        sync_sub_ = create_subscription<timesync_msgs::msg::SyncStatus>(
            "sync/grandmaster", rclcpp::SystemDefaultsQoS(), std::bind(&SyncNode::slaveSubCallback, this, _1));
        sync_pub_ = create_publisher<timesync_msgs::msg::SyncStatus>("sync/slave", rclcpp::SystemDefaultsQoS());
    }

    // always start the phc2sys process
    phc2sys_proc_->launchProcess();
    status_.status = static_cast<uint8_t>(mode_);

    // If we are a GM load ptp4l process right away
    if (params_->is_grandmaster) {
        RCLCPP_INFO(get_logger(), "Starting PTP Grandmaster");
        ptp4l_proc_->launchProcess();
    }

    RCLCPP_INFO(get_logger(), "timesync node started");
}

void SyncNode::onTick() {
    status_.stamp = get_clock()->now();
    status_.hostname = getHostname();

    // poll the ptp4l process status
    ptp4l_proc_->getProcessStatus();

    if (ptp4l_proc_->isDead()) {
        // if ptp4 is down we want to restart it
        RCLCPP_WARN(get_logger(), "PTP Died, Starting new PTP instance");

        // restart the ptp4l process
        ptp4l_proc_->launchProcess();

        // clear the counter so we have to restart the timeout
        out_of_sync_count_ = 0;
    } else if (mode_ == SyncMode::SLAVE_SYNCING && out_of_sync_count_ <= params_->out_of_sync_ticks) {
        // we are now synced
        mode_ = SyncMode::SLAVE_SYNCED;

    } else if (mode_ == SyncMode::SLAVE_SYNCED && out_of_sync_count_ > params_->out_of_sync_ticks) {
        // otherwise if we are out of sync and ptp4l is running, halt it
        ptp4l_proc_->signalProccess(basin::process::SignalType::KILL);
        mode_ = SyncMode::SLAVE_SYNCING;
    }

    status_.status = static_cast<uint8_t>(mode_);
    sync_pub_->publish(status_);
}

void SyncNode::slaveSubCallback(timesync_msgs::msg::SyncStatus::ConstSharedPtr msg) {
    if (msg->status == msg->STATUS_GRANDMASTER) {
        // if we are waiting for startup, set into syncing
        if (mode_ == SyncMode::SLAVE_WAITING) {
            mode_ = SyncMode::SLAVE_SYNCING;
        }

        // determine if we are out of sync
        rclcpp::Duration time_diff = rclcpp::Time(msg->stamp) - get_clock()->now();
        if (std::abs(time_diff.nanoseconds()) > params_->max_sync_diff_ns) {
            RCLCPP_INFO_STREAM(get_logger(), "Out of sync by " << std::abs(time_diff.nanoseconds()) << "ns");
            out_of_sync_count_++;
        } else if (out_of_sync_count_ > 0) {
            out_of_sync_count_--;
        }
    } else {
        RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 1000, "Received non-grandmaster status on grandmaster topic");
    }
}

void SyncNode::shutdown() {
    heartbeat_->sendBeat(basin::monitor_core::ProcessState::HALTING);

    try {
        ptp4l_proc_->signalProccess(basin::process::SignalType::KILL);
    } catch (const std::runtime_error& e) {
    }

    try {
        phc2sys_proc_->signalProccess(basin::process::SignalType::KILL);
    } catch (const std::runtime_error& e) {
    }
}

}  // namespace timesync_ros

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<timesync_ros::SyncNode>();
    rclcpp::spin(node);
    node->shutdown();
    rclcpp::shutdown();
    return 0;
}
