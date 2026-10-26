#include "basin/web_core/parameter_api_component.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <rcl_interfaces/msg/parameter_descriptor.hpp>
#include <rclcpp/parameter_client.hpp>

using json = nlohmann::json;

namespace basin::web_core {

namespace {
json parameter_to_json(const rclcpp::Parameter& param) {
    json param_json;
    param_json["type"] = rclcpp::to_string(param.get_type());

    switch (param.get_type()) {
        case rclcpp::ParameterType::PARAMETER_BOOL:
            param_json["value"] = param.as_bool();
            break;
        case rclcpp::ParameterType::PARAMETER_INTEGER:
            param_json["value"] = param.as_int();
            break;
        case rclcpp::ParameterType::PARAMETER_DOUBLE:
            param_json["value"] = param.as_double();
            break;
        case rclcpp::ParameterType::PARAMETER_STRING:
            param_json["value"] = param.as_string();
            break;
        case rclcpp::ParameterType::PARAMETER_BYTE_ARRAY:
            param_json["value"] = param.as_byte_array();
            break;
        case rclcpp::ParameterType::PARAMETER_BOOL_ARRAY:
            param_json["value"] = param.as_bool_array();
            break;
        case rclcpp::ParameterType::PARAMETER_INTEGER_ARRAY:
            param_json["value"] = param.as_integer_array();
            break;
        case rclcpp::ParameterType::PARAMETER_DOUBLE_ARRAY:
            param_json["value"] = param.as_double_array();
            break;
        case rclcpp::ParameterType::PARAMETER_STRING_ARRAY:
            param_json["value"] = param.as_string_array();
            break;
        case rclcpp::ParameterType::PARAMETER_NOT_SET:
        default:
            param_json["value"] = nullptr;
            break;
    }

    return param_json;
}

json descriptor_to_json(const rcl_interfaces::msg::ParameterDescriptor& desc) {
    json response_json;
    response_json["parameter_name"] = desc.name;
    response_json["type"] = rclcpp::to_string(static_cast<rclcpp::ParameterType>(desc.type));
    response_json["description"] = desc.description;
    response_json["read_only"] = desc.read_only;
    response_json["dynamic_typing"] = desc.dynamic_typing;

    if (!desc.additional_constraints.empty()) {
        response_json["additional_constraints"] = desc.additional_constraints;
    }

    if (desc.floating_point_range.size() == 1) {
        json range;
        range["from_value"] = desc.floating_point_range[0].from_value;
        range["to_value"] = desc.floating_point_range[0].to_value;
        range["step"] = desc.floating_point_range[0].step;
        response_json["floating_point_range"] = range;
    }

    if (desc.integer_range.size() == 1) {
        json range;
        range["from_value"] = desc.integer_range[0].from_value;
        range["to_value"] = desc.integer_range[0].to_value;
        range["step"] = desc.integer_range[0].step;
        response_json["integer_range"] = range;
    }

    return response_json;
}

rclcpp::Parameter build_parameter_from_json(const std::string& name, const std::string& type_str,
                                            const json& value_json) {
    if (type_str == "bool" || type_str == "boolean") {
        return rclcpp::Parameter(name, value_json.get<bool>());
    } else if (type_str == "integer" || type_str == "int") {
        return rclcpp::Parameter(name, value_json.get<int64_t>());
    } else if (type_str == "double" || type_str == "float") {
        return rclcpp::Parameter(name, value_json.get<double>());
    } else if (type_str == "string") {
        return rclcpp::Parameter(name, value_json.get<std::string>());
    } else if (type_str == "byte_array") {
        return rclcpp::Parameter(name, value_json.get<std::vector<uint8_t>>());
    } else if (type_str == "bool_array") {
        return rclcpp::Parameter(name, value_json.get<std::vector<bool>>());
    } else if (type_str == "integer_array") {
        return rclcpp::Parameter(name, value_json.get<std::vector<int64_t>>());
    } else if (type_str == "double_array") {
        return rclcpp::Parameter(name, value_json.get<std::vector<double>>());
    } else if (type_str == "string_array") {
        return rclcpp::Parameter(name, value_json.get<std::vector<std::string>>());
    }

    throw std::invalid_argument("Unsupported parameter type: " + type_str);
}

}  // namespace

ParameterApiComponent::ParameterApiComponent(std::shared_ptr<WebNode> web_node) :
    ParameterApiComponent(web_node, ParameterApiComponentConfig{}) {}

ParameterApiComponent::ParameterApiComponent(std::shared_ptr<WebNode> web_node,
                                             const ParameterApiComponentConfig& config) :
    web_node_(web_node), config_(config) {
    if (!web_node_) {
        throw std::invalid_argument("WebNode pointer cannot be null");
    }

    registerRoutes();

    RCLCPP_INFO(web_node_->get_logger(), "ParameterApiComponent initialized on route '%s'", config_.base_route.c_str());
}

void ParameterApiComponent::registerRoutes() {
    registerListParametersRoute();
    registerGetParameterRoute();
    registerSetParameterRoute();
    registerDescribeParametersRoute();
}

void ParameterApiComponent::registerListParametersRoute() {
    // GET /api/parameters/:node_name - List all parameters on a node
    std::string route = config_.base_route + "/([^/]+)";

    web_node_->getServer().Get(route, [this](const httplib::Request& req, httplib::Response& res) {
        // Extract node name from URL
        std::string node_name = req.matches[1];

        try {
            // Use AsyncParametersClient for both local and remote nodes to avoid duplication
            auto param_client = std::make_shared<rclcpp::AsyncParametersClient>(web_node_, node_name);

            // Wait for service to be available
            if (!param_client->wait_for_service(std::chrono::milliseconds(config_.timeout_ms))) {
                res.status = 503;
                json error_json;
                error_json["error"] = "Parameter service not available for node: " + node_name;
                res.set_content(error_json.dump(), "application/json");
                return;
            }

            // List all parameters
            auto param_names_future = param_client->list_parameters({}, 0);
            auto param_names = param_names_future.get();

            // Get all parameter values (may be empty)
            auto params_future = param_client->get_parameters(param_names.names);
            auto params = params_future.get();

            json response_json;
            response_json["node_name"] = node_name;
            response_json["parameters"] = json::object();

            for (const auto& param : params) {
                response_json["parameters"][param.get_name()] = parameter_to_json(param);
            }

            res.set_content(response_json.dump(), "application/json");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "Error listing parameters: %s", e.what());
            res.status = 500;
            json error_json;
            error_json["error"] = std::string("Failed to list parameters: ") + e.what();
            res.set_content(error_json.dump(), "application/json");
        }
    });
}

void ParameterApiComponent::registerGetParameterRoute() {
    // GET /api/parameters/:node_name/:param_name - Get a specific parameter value
    std::string route = config_.base_route + "/([^/]+)/([^/]+)";

    web_node_->getServer().Get(route, [this](const httplib::Request& req, httplib::Response& res) {
        std::string node_name = req.matches[1];
        std::string param_name = req.matches[2];

        try {
            auto param_client = std::make_shared<rclcpp::AsyncParametersClient>(web_node_, node_name);

            if (!param_client->wait_for_service(std::chrono::milliseconds(config_.timeout_ms))) {
                res.status = 503;
                json error_json;
                error_json["error"] = "Parameter service not available for node: " + node_name;
                res.set_content(error_json.dump(), "application/json");
                return;
            }

            // Verify existence
            auto param_names_future = param_client->list_parameters({}, 0);
            auto param_names = param_names_future.get();
            bool has_param =
                std::find(param_names.names.begin(), param_names.names.end(), param_name) != param_names.names.end();
            if (!has_param) {
                res.status = 404;
                json error_json;
                error_json["error"] = "Parameter '" + param_name + "' not found on node '" + node_name + "'";
                res.set_content(error_json.dump(), "application/json");
                return;
            }

            auto params_future = param_client->get_parameters({ param_name });
            auto params = params_future.get();
            auto param = params[0];

            json response_json;
            response_json["node_name"] = node_name;
            response_json["parameter_name"] = param_name;
            // Merge type & value from helper
            auto pjson = parameter_to_json(param);
            response_json["type"] = pjson["type"];
            response_json["value"] = pjson["value"];

            res.set_content(response_json.dump(), "application/json");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "Error getting parameter: %s", e.what());
            res.status = 500;
            json error_json;
            error_json["error"] = std::string("Failed to get parameter: ") + e.what();
            res.set_content(error_json.dump(), "application/json");
        }
    });
}

void ParameterApiComponent::registerSetParameterRoute() {
    // PUT /api/parameters/:node_name/:param_name - Set a parameter value
    std::string route = config_.base_route + "/([^/]+)/([^/]+)";

    web_node_->getServer().Put(route, [this](const httplib::Request& req, httplib::Response& res) {
        std::string node_name = req.matches[1];
        std::string param_name = req.matches[2];

        try {
            // Parse JSON body
            auto request_json = json::parse(req.body);

            if (!request_json.contains("value") || !request_json.contains("type")) {
                res.status = 400;
                json error_json;
                error_json["error"] = "Request must contain 'value' and 'type' fields";
                res.set_content(error_json.dump(), "application/json");
                return;
            }

            std::string type_str = request_json["type"];

            // Build parameter using helper (throws on unsupported type)
            rclcpp::Parameter param = build_parameter_from_json(param_name, type_str, request_json["value"]);

            // Use AsyncParametersClient for both local and remote nodes
            auto param_client = std::make_shared<rclcpp::AsyncParametersClient>(web_node_, node_name);

            if (!param_client->wait_for_service(std::chrono::milliseconds(config_.timeout_ms))) {
                res.status = 503;
                json error_json;
                error_json["error"] = "Parameter service not available for node: " + node_name;
                res.set_content(error_json.dump(), "application/json");
                return;
            }

            // Set the parameter
            auto results_future = param_client->set_parameters({ param });
            auto results = results_future.get();

            if (results.empty() || !results[0].successful) {
                res.status = 400;
                json error_json;
                error_json["error"] = "Failed to set parameter";
                if (!results.empty() && !results[0].reason.empty()) {
                    error_json["reason"] = results[0].reason;
                }
                res.set_content(error_json.dump(), "application/json");
                return;
            }

            json response_json;
            response_json["success"] = true;
            response_json["node_name"] = node_name;
            response_json["parameter_name"] = param_name;
            response_json["type"] = type_str;
            response_json["value"] = request_json["value"];

            res.set_content(response_json.dump(), "application/json");
        } catch (const json::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "JSON parse error: %s", e.what());
            res.status = 400;
            json error_json;
            error_json["error"] = std::string("Invalid JSON: ") + e.what();
            res.set_content(error_json.dump(), "application/json");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "Error setting parameter: %s", e.what());
            res.status = 500;
            json error_json;
            error_json["error"] = std::string("Failed to set parameter: ") + e.what();
            res.set_content(error_json.dump(), "application/json");
        }
    });
}

void ParameterApiComponent::registerDescribeParametersRoute() {
    // GET /api/parameters/:node_name/:param_name/describe - Get parameter description
    std::string route = config_.base_route + "/([^/]+)/([^/]+)/describe";

    web_node_->getServer().Get(route, [this](const httplib::Request& req, httplib::Response& res) {
        std::string node_name = req.matches[1];
        std::string param_name = req.matches[2];

        try {
            auto param_client = std::make_shared<rclcpp::AsyncParametersClient>(web_node_, node_name);

            if (!param_client->wait_for_service(std::chrono::milliseconds(config_.timeout_ms))) {
                res.status = 503;
                json error_json;
                error_json["error"] = "Parameter service not available for node: " + node_name;
                res.set_content(error_json.dump(), "application/json");
                return;
            }

            auto descriptors_future = param_client->describe_parameters({ param_name });
            auto descriptors = descriptors_future.get();

            if (descriptors.empty()) {
                res.status = 404;
                json error_json;
                error_json["error"] = "Parameter '" + param_name + "' not found on node '" + node_name + "'";
                res.set_content(error_json.dump(), "application/json");
                return;
            }

            const auto& desc = descriptors[0];
            json response_json = descriptor_to_json(desc);
            response_json["node_name"] = node_name;

            res.set_content(response_json.dump(), "application/json");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "Error describing parameter: %s", e.what());
            res.status = 500;
            json error_json;
            error_json["error"] = std::string("Failed to describe parameter: ") + e.what();
            res.set_content(error_json.dump(), "application/json");
        }
    });
}

}  // namespace basin::web_core
