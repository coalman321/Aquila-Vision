#pragma once

#include <functional>
#include <rclcpp/node.hpp>
#include <rclcpp/parameter.hpp>
#include <rclcpp/parameter_event_handler.hpp>
#include <set>
#include <string>
#include <vector>

namespace basin::node_core {

class NodeParams {
   public:
    NodeParams(rclcpp::Node* node);

    /// @brief Declare and read a parameter with the given name and default value.
    /// @param name The name of the parameter. including path syntax
    /// @param var The variable to store the parameter value in
    template <typename T>
    void createParam(const std::string& name, T& var) {
        if (!node_->has_parameter(name)) {
            node_->declare_parameter(name, var);
        }
        var = node_->get_parameter(name).get_value<T>();
    }

    /// @brief Declare, read, and subscribe to updates for a parameter with the given name and default value.
    /// @param name The name of the parameter. including path syntax
    /// @param var The variable to store the parameter value in. Will be updated on parameter changes.
    template <typename T>
    void createDynParam(const std::string& name, T& var) {
        if (!node_->has_parameter(name)) {
            node_->declare_parameter(name, var);
        }
        var = node_->get_parameter(name).get_value<T>();
        auto cb = [&var, name, this](const rclcpp::Parameter& p) {
            var = p.get_value<T>();
            RCLCPP_DEBUG(node_->get_logger(), "param update for %s", name.c_str());
        };
        param_callbacks_.push_back(update_handler_->add_parameter_callback(name, cb));
    }

    /// @brief A version of dyn_param that takes a callback to be called on parameter updates
    /// @param name The name of the parameter. including path syntax
    /// @param var The variable to store the parameter value in. Will be updated on parameter changes.
    /// @param callback The callback to be called on parameter updates
    template <typename T>
    void createDynParamCb(const std::string& name, T& var, std::function<void()> callback) {
        if (!node_->has_parameter(name)) {
            node_->declare_parameter(name, var);
        }
        var = node_->get_parameter(name).get_value<T>();
        auto cb = [&var, callback](const rclcpp::Parameter& p) {
            var = p.get_value<T>();
            callback();
        };
        param_callbacks_.push_back(update_handler_->add_parameter_callback(name, cb));
    }

    /// @brief function to get unique immediate subkeys from a master key in the params file
    /// @param node the node to get the parameters from
    /// @param prefix the master key to search under
    /// @return a vector of unique immediate subkeys under the master key
    inline std::vector<std::string> getSubkeys(const std::string& prefix) {
        const auto& overrides = node_->get_node_parameters_interface()->get_parameter_overrides();
        std::set<std::string> keys;
        const std::string search = prefix + ".";
        for (const auto& [param_name, value] : overrides) {
            if (param_name.rfind(search, 0) == 0) {
                std::string remainder = param_name.substr(search.size());
                auto dot_pos = remainder.find('.');
                keys.insert(dot_pos != std::string::npos ? remainder.substr(0, dot_pos) : remainder);
            }
        }
        return { keys.begin(), keys.end() };
    }

   protected:
    rclcpp::Node* node_;
    std::shared_ptr<rclcpp::ParameterEventHandler> update_handler_;
    std::vector<rclcpp::ParameterCallbackHandle::SharedPtr> param_callbacks_;
};
}  // namespace basin::node_core
