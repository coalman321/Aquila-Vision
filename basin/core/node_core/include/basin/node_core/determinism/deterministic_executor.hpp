#pragma once

#include <basin/node_core/determinism/execution_recorder.hpp>
#include <memory>
#include <rclcpp/clock.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>

namespace basin::node_core::determinism {

/// @brief A `SingleThreadedExecutor` that records the executor's real dispatch order before
/// running each callback.
///
/// Single-threaded is required: determinism and the multi-threaded executor are mutually
/// exclusive (there's no single dispatch order to record if callbacks can run concurrently).
///
/// Implementation note vs. the design doc: `rclcpp::Executor::execute_any_executable` is not
/// virtual in rclcpp Jazzy, so it can't be overridden the way the doc's sketch shows. This
/// overrides `spin()` instead (mirroring `SingleThreadedExecutor::spin()`'s own
/// get_next_executable/execute_any_executable loop) and wraps the same call — same effect,
/// different hook.
class DeterministicExecutor : public rclcpp::executors::SingleThreadedExecutor {
   public:
    explicit DeterministicExecutor(std::shared_ptr<ExecutionRecorder> recorder,
                                   const rclcpp::ExecutorOptions& options = rclcpp::ExecutorOptions());

    /// @brief The clock dispatch entries are stamped with. Phase 1 is single-node, so this is
    /// set once to the clock of the node being recorded/replayed (e.g. `node->get_clock()`),
    /// rather than derived per-executable — there's only one clock to derive it from. Must be
    /// called before `spin()`.
    void setClock(rclcpp::Clock::SharedPtr clock) { clock_ = std::move(clock); }

    /// @brief Spin, recording each dispatch's {sequence, entity_kind, entity_name, sim_time}
    /// before running the real callback. Observes the executor's real dispatch order rather
    /// than trying to change it, so recording perturbs live timing as little as possible.
    void spin() override;

   private:
    std::shared_ptr<ExecutionRecorder> recorder_;
    rclcpp::Clock::SharedPtr clock_;
};

}  // namespace basin::node_core::determinism
