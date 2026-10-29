#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <basin/log_util/log_manager.hpp>
#include <basin/log_util/snapshot_extractor.hpp>
#include <basin/log_util/snapshot_writer.hpp>

namespace py = pybind11;
using namespace basin::log_util;

PYBIND11_MODULE(_log_util_py, m) {
    m.doc() = "Python bindings for log_util - query snapshots in ROS2 bag logs";

    // SnapshotInfo struct
    py::class_<SnapshotInfo>(m, "SnapshotInfo")
        .def(py::init<>())
        .def_readwrite("id", &SnapshotInfo::id, "Unique identifier for the snapshot")
        .def_readwrite("start_time", &SnapshotInfo::start_time, "Start time in epoch seconds")
        .def_readwrite("end_time", &SnapshotInfo::end_time, "End time in epoch seconds")
        .def_readwrite("trigger", &SnapshotInfo::trigger, "Trigger type (e.g., 'manual' or 'event-<trigger_id>')")
        .def_readwrite("description", &SnapshotInfo::description, "User-defined description")
        .def_readwrite("tags", &SnapshotInfo::tags, "List of tags for categorization")
        .def("__repr__", [](const SnapshotInfo& info) {
            return "<SnapshotInfo id='" + info.id + "' start=" + std::to_string(info.start_time) +
                   " end=" + std::to_string(info.end_time) + ">";
        });

    // SnapshotWriter class
    py::class_<SnapshotWriter>(m, "SnapshotWriter")
        .def(py::init<>())
        .def("set_recording_directory", &SnapshotWriter::set_recording_directory, py::arg("directory"),
             "Sets the recording directory where snapshots will be stored")
        .def("start_snapshot", &SnapshotWriter::start_snapshot, py::arg("snapshot_id"), py::arg("trigger") = "manual",
             py::arg("description") = "", py::arg("tags") = std::vector<std::string>(),
             py::arg("start_time_override") = -1, "Starts a new snapshot with the given ID and metadata")
        .def("stop_snapshot", &SnapshotWriter::stop_snapshot, py::arg("snapshot_id"), py::arg("end_time_override") = -1,
             "Stops an active snapshot")
        .def("has_snapshot", &SnapshotWriter::has_snapshot, py::arg("snapshot_id"),
             "Checks if a snapshot exists (running or completed)")
        .def("is_snapshot_running", &SnapshotWriter::is_snapshot_running, py::arg("snapshot_id"),
             "Checks if a snapshot is currently running")
        .def("get_snapshot", &SnapshotWriter::get_snapshot, py::arg("snapshot_id"),
             "Gets information about a specific snapshot")
        .def("get_all_snapshots", &SnapshotWriter::get_all_snapshots, "Gets all snapshots")
        .def("load_from_file", &SnapshotWriter::load_from_file, "Loads existing snapshots from the YAML file")
        .def("save_to_file", &SnapshotWriter::save_to_file, "Saves all snapshots to the YAML file");

    // SnapshotExtractor class
    py::class_<SnapshotExtractor>(m, "SnapshotExtractor")
        .def(py::init<>())
        .def("extract_snapshot", &SnapshotExtractor::extract_snapshot, py::arg("log_directory"), py::arg("snapshot_id"),
             py::arg("output_directory"), "Extracts a single snapshot to a new rosbag directory")
        .def("extract_all_snapshots", &SnapshotExtractor::extract_all_snapshots, py::arg("log_directory"),
             py::arg("output_base_directory"),
             "Extracts all snapshots from a log directory. Returns number of snapshots extracted.")
        .def("list_snapshots", &SnapshotExtractor::list_snapshots, py::arg("log_directory"),
             "Lists all available snapshots in a log directory")
        .def(
            "validate_log_directory",
            [](SnapshotExtractor& self, const std::string& log_directory) {
                std::string error_message;
                bool valid = self.validate_log_directory(log_directory, error_message);
                return py::make_tuple(valid, error_message);
            },
            py::arg("log_directory"),
            "Validates that a log directory has the required files. Returns (valid, error_message)")
        .def(
            "find_relevant_mcap_files",
            [](SnapshotExtractor& self, const std::string& log_directory, int64_t start_time, int64_t end_time) {
                // The C++ method returns std::filesystem::path, which pybind11 has no converter for
                // (calling it raised TypeError); hand back plain strings, as get_log_files does.
                std::vector<std::string> paths;
                for (const auto& path : self.find_relevant_mcap_files(log_directory, start_time, end_time)) {
                    paths.push_back(path.string());
                }
                return paths;
            },
            py::arg("log_directory"), py::arg("start_time"), py::arg("end_time"),
            "Finds all MCAP files in a directory that overlap with a time range");

    // LogManager class
    py::class_<LogManager>(m, "LogManager")
        .def(py::init<>())
        .def("set_log_directory", &LogManager::set_log_directory, py::arg("directory"),
             "Sets the log directory to manage")
        .def("set_disk_limits", &LogManager::set_disk_limits, py::arg("rotation_threshold_percent"),
             py::arg("warning_threshold_percent"), "Sets disk usage limits (both values should be 0-100)")
        .def("scan_log_directory", &LogManager::scan_log_directory,
             "Scans the log directory for MCAP files and snapshots. Returns number of MCAP files found.")
        .def("check_and_rotate_logs", &LogManager::check_and_rotate_logs,
             "Checks disk usage and rotates old logs if needed. Returns True if rotation was performed.")
        .def("get_log_files", &LogManager::get_log_files, "Gets the list of all MCAP file paths")
        .def("get_snapshot_log_files", &LogManager::get_snapshot_log_files, py::arg("snapshot_id"),
             "Gets MCAP files associated with a snapshot")
        .def("get_disk_usage_percent", &LogManager::get_disk_usage_percent,
             "Gets the current disk usage percentage (0-100)")
        .def("should_warn_disk_usage", &LogManager::should_warn_disk_usage,
             "Checks if a warning should be issued due to high disk usage")
        .def("get_snapshot_writer", &LogManager::get_snapshot_writer, py::return_value_policy::reference_internal,
             "Gets the snapshot writer for querying snapshots");
}
