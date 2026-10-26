#pragma once

#include <basin/node_core/node_core.hpp>
#include <memory>
#include <process_lib/process_controller.hpp>
#include <rclcpp/rclcpp.hpp>
#include <timesync_msgs/msg/sync_status.hpp>

#include "timesync_ros/sync_node_params.hpp"

namespace timesync_ros {

enum SyncMode {
    GRANDMASTER = timesync_msgs::msg::SyncStatus::STATUS_GRANDMASTER,
    SLAVE_WAITING = timesync_msgs::msg::SyncStatus::STATUS_SLAVE_WAITING,
    SLAVE_SYNCING = timesync_msgs::msg::SyncStatus::STATUS_SLAVE_SYNCING,
    SLAVE_SYNCED = timesync_msgs::msg::SyncStatus::STATUS_SLAVE_SYNCED
};

class SyncNode : public basin::node_core::NodeCore {
   public:
    SyncNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

    void onTick() override;

    void slaveSubCallback(timesync_msgs::msg::SyncStatus::ConstSharedPtr msg);

    void shutdown() override;

   private:
    std::unique_ptr<SyncNodeParams> params_;

    rclcpp::Publisher<timesync_msgs::msg::SyncStatus>::SharedPtr sync_pub_;
    rclcpp::Subscription<timesync_msgs::msg::SyncStatus>::SharedPtr sync_sub_;

    std::unique_ptr<basin::process::ProcessController> ptp4l_proc_;
    std::unique_ptr<basin::process::ProcessController> phc2sys_proc_;

    timesync_msgs::msg::SyncStatus status_;
    int out_of_sync_count_{ -1 };
    SyncMode mode_{ SyncMode::SLAVE_WAITING };
};
}  // namespace timesync_ros
