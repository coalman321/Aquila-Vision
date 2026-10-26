#include "launch_core/launch_interpreter.hpp"

#include <iostream>
#include <rclcpp/logging.hpp>

#include <ament_index_cpp/get_package_share_path.hpp>
#include <stdexcept>

namespace basin::launch_core {

LaunchInterpreter::LaunchInterpreter() {
    // Init the interpreter
    pybind11::initialize_interpreter();

    // Redirect Python stdout/stderr to C++ stdout
    try {
        // Inject C++ callback functions directly into Python globals
        pybind11::globals()["_write_stdout"] = pybind11::cpp_function([this](const std::string& text) {
            // Filter out empty/whitespace-only strings (e.g., standalone newlines)
            if (!text.empty() && text.find_first_not_of(" \t\n\r") != std::string::npos) {
                RCLCPP_INFO(logger_, "%s", text.c_str());
            }
        });
        
        pybind11::globals()["_write_stderr"] = pybind11::cpp_function([this](const std::string& text) {
            // Filter out empty/whitespace-only strings (e.g., standalone newlines)
            if (!text.empty() && text.find_first_not_of(" \t\n\r") != std::string::npos) {
                RCLCPP_ERROR(logger_, "%s", text.c_str());
            }
        });
        
        pybind11::exec(R"(
import sys

class CppStdoutWriter:
    def write(self, text):
        if text:
            _write_stdout(text)
        return len(text) if text else 0
    
    def flush(self):
        pass

class CppStderrWriter:
    def write(self, text):
        if text:
            _write_stderr(text)
        return len(text) if text else 0
    
    def flush(self):
        pass

sys.stdout = CppStdoutWriter()
sys.stderr = CppStderrWriter()
)");
    } catch (pybind11::error_already_set& e) {
        throw std::invalid_argument("Unable to redirect Python output: " + std::string(e.what()));
    }

    // Prepare the environment
    try {
        pybind11::exec("from launch_api.loader import ProductionLoader");
        pybind11::exec("loader = ProductionLoader()");
    } catch (pybind11::error_already_set& e) {
        throw std::invalid_argument("Unable to load launch description: " + std::string(e.what()));
    }
}

LaunchInterpreter::~LaunchInterpreter() {
    // Shut down the interpreter
    pybind11::finalize_interpreter();
}

void LaunchInterpreter::loadDescription(std::filesystem::path launch_path, int id_offset) {
    if (!std::filesystem::is_regular_file(launch_path)) {
        throw std::invalid_argument("Launch file is not a regular file: " + launch_path.string());
    }

    launch_path_ = launch_path;

    buildDescription(id_offset);
}

void LaunchInterpreter::loadDescription(const std::string& package, const std::string& launch_file, int id_offset) {
    std::filesystem::path package_share_path = ament_index_cpp::get_package_share_path(package);

    std::filesystem::path launch_path = package_share_path / "launch" / launch_file;

    if (!std::filesystem::is_regular_file(launch_path)) {
        throw std::invalid_argument("Launch file is not a regular file: " + launch_path.string());
    }

    launch_path_ = launch_path;

    buildDescription(id_offset);
}

void LaunchInterpreter::buildDescription(int id_offset) {
    try {
        // create the python execution environment
        pybind11::dict locals = {};
        pybind11::dict globals = pybind11::globals();
        locals["file_path"] = launch_path_.string();

        // Execute the load
        pybind11::exec("loader.load_file(file_path)", globals, locals);
        pybind11::exec("desc = loader.get_flat_desc()", globals, locals);

        // work over each of the resulting objects
        launch_processes_.clear();
        uint32_t launch_id = id_offset;
        for (pybind11::handle process : locals["desc"]) {
            locals["item"] = process;

            // Get the information for the process description
            pybind11::exec("executable = item.get_executable()", globals, locals);
            pybind11::exec("name = item.get_name()", globals, locals);
            pybind11::exec("args = item.get_args()", globals, locals);
            pybind11::str process_executable = locals["executable"];
            pybind11::str process_name = locals["name"];
            pybind11::list process_args = locals["args"];

            // Configure the C++ process description
            ProcessDescription description;
            description.exe_path = process_executable.cast<std::string>();
            description.id = launch_id;
            description.process_name = process_name;
            for (pybind11::handle arg : process_args) {
                description.args.push_back(arg.cast<std::string>());
            }

            // Get the optional scheduler settings, validated by the Scheduling class
            pybind11::exec("scheduling = item.get_scheduling()", globals, locals);
            pybind11::object scheduling = locals["scheduling"];
            if (!scheduling.is_none()) {
                basin::process::SchedulingConfig config;
                config.policy = scheduling.attr("policy_id").cast<int>();
                config.priority = scheduling.attr("priority").cast<int>();
                pybind11::object nice = scheduling.attr("nice");
                if (!nice.is_none()) {
                    config.nice = nice.cast<int>();
                }
                description.scheduling = config;
            }

            launch_processes_.push_back(description);
            launch_id++;
        }

    } catch (pybind11::error_already_set& e) {
        throw std::invalid_argument("Unable to load launch description: " + std::string(e.what()));
    }
}

std::vector<ProcessDescription> LaunchInterpreter::getProcessDescription() { return launch_processes_; }

std::filesystem::path LaunchInterpreter::getLaunchPath() { return launch_path_; }

}  // namespace basin::launch_core
