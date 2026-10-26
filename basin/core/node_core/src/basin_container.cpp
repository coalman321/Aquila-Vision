#include <memory>
#include <rclcpp/executor.hpp>
#include <rclcpp/executors/multi_threaded_executor.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp_components/component_manager.hpp>

#include "basin/node_core/node_params.hpp"

namespace basin::node_core {

struct ComponentConfig : public NodeParams {
    std::string library_name;
    std::string package_name;
    std::vector<std::string> args;
    std::string node_name;
    std::string node_namespace;
    std::string log_level;

    ComponentConfig(const std::string prefix, rclcpp::Node* node) : NodeParams(node) {
        createParam(prefix + "library_name", library_name);
        createParam(prefix + "package_name", package_name);
        createParam(prefix + "args", args);
        createParam(prefix + "node_name", node_name);
        createParam(prefix + "node_namespace", node_namespace);
        createParam(prefix + "log_level", log_level);
    }
};

class BasinContainer : public rclcpp_components::ComponentManager {
   public:
    BasinContainer(std::weak_ptr<rclcpp::Executor> exec, const rclcpp::NodeOptions& options = rclcpp::NodeOptions()) :
        ComponentManager(exec, "basin_container",
                         rclcpp::NodeOptions(options).automatically_declare_parameters_from_overrides(true)) {
        // Auto-detect components by checking for parameters
        int i = 0;
        while (true) {
            std::string prefix = "components." + std::to_string(i);

            // Check if library_name parameter exists
            std::string library_name;
            try {
                library_name = this->get_parameter(prefix + ".library_name").as_string();
            } catch (const rclcpp::exceptions::ParameterNotDeclaredException& e) {
                break;  // No more components to load
            }

            // If parameter wasn't set or is empty, we've reached the end of the components
            if (!library_name.empty()) {
                ComponentConfig config(prefix + ".", this);
                component_configs_.push_back(config);

                RCLCPP_INFO(this->get_logger(), "Loaded component %d: library=%s, package=%s, args_count=%zu", i,
                            config.library_name.c_str(), config.package_name.c_str(), config.args.size());

                i++;
            }
        }

        RCLCPP_INFO(this->get_logger(), "Auto-detected %d components", static_cast<int>(component_configs_.size()));

        for (const auto& config : component_configs_) {
            load_component(config);
        }
    }

   private:
    std::vector<ComponentConfig> component_configs_;

    void load_component(const ComponentConfig& config) {
        try {
            auto resources = get_component_resources(config.package_name);
            for (const auto& resource : resources) {
                if (resource.first != config.library_name) {
                    continue;
                }
                auto factory = create_component_factory(resource);

                std::vector<std::string> arguments = config.args;
                if (!config.node_name.empty() || !config.node_namespace.empty() || !config.log_level.empty()) {
                    arguments.push_back("--ros-args");
                    if (!config.node_name.empty()) {
                        arguments.push_back("-r");
                        arguments.push_back("__node:=" + config.node_name);
                    }
                    if (!config.node_namespace.empty()) {
                        arguments.push_back("-r");
                        arguments.push_back("__ns:=" + config.node_namespace);
                    }
                    if (!config.log_level.empty()) {
                        arguments.push_back("--log-level");
                        arguments.push_back(config.log_level);
                    }
                }

                auto options = rclcpp::NodeOptions().arguments(arguments);
                uint64_t node_id = unique_id_++;

                try {
                    node_wrappers_[node_id] = factory->create_node_instance(options);
                } catch (const std::exception& ex) {
                    RCLCPP_ERROR(this->get_logger(), "Component constructor threw an exception: %s", ex.what());
                    return;
                }

                add_node_to_executor(node_id);
                return;
            }
            RCLCPP_ERROR(this->get_logger(),
                         "Failed to find class with the requested plugin name '%s' in the loaded library",
                         config.library_name.c_str());
        } catch (const std::exception& ex) {
            RCLCPP_ERROR(this->get_logger(), "%s", ex.what());
        }
    }
};

}  // namespace basin::node_core

int main(int argc, char* argv[]) {
    // Initialize ROS 2
    rclcpp::init(argc, argv);

    // Create executor and container
    auto executor = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();
    auto container = std::make_shared<basin::node_core::BasinContainer>(executor);

    // Add container to executor
    executor->add_node(container);

    // Spin the executor
    executor->spin();

    // Shutdown ROS 2
    rclcpp::shutdown();

    return 0;
}
