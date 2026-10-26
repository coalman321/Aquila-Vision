#include "basin/node_core/determinism/deterministic_executor.hpp"

#include <rclcpp/utilities.hpp>
#include <stdexcept>

namespace basin::node_core::determinism {

namespace {
EntityKind classify(const rclcpp::AnyExecutable& any_exec) {
    if (any_exec.timer) {
        return EntityKind::kTimer;
    }
    if (any_exec.subscription) {
        return EntityKind::kSubscription;
    }
    if (any_exec.service) {
        return EntityKind::kService;
    }
    if (any_exec.client) {
        return EntityKind::kClient;
    }
    return EntityKind::kWaitable;
}

std::string entityName(const rclcpp::AnyExecutable& any_exec, ExecutionRecorder& recorder) {
    if (any_exec.timer) {
        return recorder.lookupTimerName(any_exec.timer.get());
    }
    if (any_exec.subscription) {
        return any_exec.subscription->get_topic_name();
    }
    if (any_exec.service) {
        return any_exec.service->get_service_name();
    }
    if (any_exec.client) {
        return any_exec.client->get_service_name();
    }
    return "waitable";
}
}  // namespace

DeterministicExecutor::DeterministicExecutor(std::shared_ptr<ExecutionRecorder> recorder,
                                             const rclcpp::ExecutorOptions& options) :
    SingleThreadedExecutor(options), recorder_(std::move(recorder)) {
    if (!recorder_) {
        throw std::invalid_argument("DeterministicExecutor requires a non-null ExecutionRecorder");
    }
}

void DeterministicExecutor::spin() {
    if (!clock_) {
        throw std::runtime_error("DeterministicExecutor::setClock() must be called before spin()");
    }
    if (spinning.exchange(true)) {
        throw std::runtime_error("spin() called while already spinning");
    }
    struct ResetSpinning {
        std::atomic_bool& flag;
        ~ResetSpinning() { flag.store(false); }
    } reset_spinning{ spinning };

    while (rclcpp::ok(this->context_) && spinning.load()) {
        rclcpp::AnyExecutable any_executable;
        if (!get_next_executable(any_executable)) {
            continue;
        }

        EntityKind kind = classify(any_executable);
        std::string name = entityName(any_executable, *recorder_);
        recorder_->recordDispatch(kind, name, clock_->now());

        execute_any_executable(any_executable);
    }
}

}  // namespace basin::node_core::determinism
