#pragma once

#include <basin/node_core/determinism/deterministic_node.hpp>

namespace basin::node_core::test {

/// @brief Minimal `DeterministicNode` subclass for exercising the determinism hooks in isolation.
class TestNode : public basin::node_core::determinism::DeterministicNode {
   public:
    explicit TestNode(const std::string& name, const rclcpp::NodeOptions& options = rclcpp::NodeOptions()) :
        DeterministicNode(name, "", options) {}

    void onTick() override { ++tick_count_; }
    void shutdown() override {}

    int tickCount() const { return tick_count_; }

    int saveCount() const { return save_count_; }
    int restoreCount() const { return restore_count_; }
    int counterValue() const { return counter_; }
    void setCounter(int value) { counter_ = value; }

   protected:
    void saveState(determinism::StateArchive& archive) override {
        ++save_count_;
        archive.put("counter", counter_);
    }

    void restoreState(const determinism::StateArchive& archive) override {
        ++restore_count_;
        counter_ = archive.getOr<int>("counter", 0);
    }

   private:
    int tick_count_{ 0 };
    int counter_{ 0 };
    int save_count_{ 0 };
    int restore_count_{ 0 };
};

}  // namespace basin::node_core::test
