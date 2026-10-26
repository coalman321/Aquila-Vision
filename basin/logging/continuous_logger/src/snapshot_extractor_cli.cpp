#include <basin/log_util/snapshot_extractor.hpp>
#include <cstring>
#include <iostream>
#include <string>

void print_usage(const char* program_name) {
    std::cout << "Continuous Logger Snapshot Extractor\n";
    std::cout << "=====================================\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << program_name << " <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  list <log_dir>\n";
    std::cout << "      List all snapshots in the log directory\n\n";
    std::cout << "  extract <log_dir> <snapshot_id> <output_dir>\n";
    std::cout << "      Extract a specific snapshot to a rosbag directory\n\n";
    std::cout << "  extract-all <log_dir> <output_dir>\n";
    std::cout << "      Extract all snapshots to separate rosbag directories\n\n";
    std::cout << "  validate <log_dir>\n";
    std::cout << "      Validate that a log directory has required files\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " list /data/logs\n";
    std::cout << "  " << program_name << " extract /data/logs event_collision_0 /data/extracted\n";
    std::cout << "  " << program_name << " extract-all /data/logs /data/extracted\n";
    std::cout << "  " << program_name << " validate /data/logs\n\n";
}

void format_timestamp(int64_t timestamp) {
    time_t time = static_cast<time_t>(timestamp);
    struct tm* tm_info = localtime(&time);
    char buffer[26];
    strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    std::cout << buffer;
}

int cmd_list(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Error: Missing log directory\n";
        std::cerr << "Usage: " << argv[0] << " list <log_dir>\n";
        return 1;
    }

    std::string log_dir = argv[2];
    basin::log_util::SnapshotExtractor extractor;

    // Validate directory
    std::string error_msg;
    if (!extractor.validate_log_directory(log_dir, error_msg)) {
        std::cerr << "Error: " << error_msg << "\n";
        return 1;
    }

    // List snapshots
    auto snapshots = extractor.list_snapshots(log_dir);

    if (snapshots.empty()) {
        std::cout << "No snapshots found in: " << log_dir << "\n";
        return 0;
    }

    std::cout << "Found " << snapshots.size() << " snapshot(s) in: " << log_dir << "\n\n";

    for (size_t i = 0; i < snapshots.size(); ++i) {
        const auto& snapshot = snapshots[i];

        std::cout << "Snapshot " << (i + 1) << ":\n";
        std::cout << "  ID: " << snapshot.id << "\n";
        std::cout << "  Start Time: ";
        format_timestamp(snapshot.start_time);
        std::cout << " (" << snapshot.start_time << ")\n";
        std::cout << "  End Time: ";
        format_timestamp(snapshot.end_time);
        std::cout << " (" << snapshot.end_time << ")\n";
        std::cout << "  Duration: " << (snapshot.end_time - snapshot.start_time) << " seconds\n";
        std::cout << "  Trigger: " << snapshot.trigger << "\n";
        std::cout << "  Description: " << snapshot.description << "\n";

        if (!snapshot.tags.empty()) {
            std::cout << "  Tags: ";
            for (size_t j = 0; j < snapshot.tags.size(); ++j) {
                std::cout << snapshot.tags[j];
                if (j < snapshot.tags.size() - 1) {
                    std::cout << ", ";
                }
            }
            std::cout << "\n";
        }

        std::cout << "\n";
    }

    return 0;
}

int cmd_extract(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "Error: Missing arguments\n";
        std::cerr << "Usage: " << argv[0] << " extract <log_dir> <snapshot_id> <output_dir>\n";
        return 1;
    }

    std::string log_dir = argv[2];
    std::string snapshot_id = argv[3];
    std::string output_dir = argv[4];

    basin::log_util::SnapshotExtractor extractor;

    if (extractor.extract_snapshot(log_dir, snapshot_id, output_dir)) {
        return 0;
    } else {
        return 1;
    }
}

int cmd_extract_all(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Error: Missing arguments\n";
        std::cerr << "Usage: " << argv[0] << " extract-all <log_dir> <output_dir>\n";
        return 1;
    }

    std::string log_dir = argv[2];
    std::string output_dir = argv[3];

    basin::log_util::SnapshotExtractor extractor;

    int count = extractor.extract_all_snapshots(log_dir, output_dir);
    return (count > 0) ? 0 : 1;
}

int cmd_validate(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Error: Missing log directory\n";
        std::cerr << "Usage: " << argv[0] << " validate <log_dir>\n";
        return 1;
    }

    std::string log_dir = argv[2];
    basin::log_util::SnapshotExtractor extractor;

    std::string error_msg;
    if (extractor.validate_log_directory(log_dir, error_msg)) {
        std::cout << "✓ Log directory is valid: " << log_dir << "\n";

        // Also show snapshot count
        auto snapshots = extractor.list_snapshots(log_dir);
        std::cout << "✓ Found " << snapshots.size() << " snapshot(s)\n";

        return 0;
    } else {
        std::cerr << "✗ Validation failed: " << error_msg << "\n";
        return 1;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "list") {
        return cmd_list(argc, argv);
    } else if (command == "extract") {
        return cmd_extract(argc, argv);
    } else if (command == "extract-all") {
        return cmd_extract_all(argc, argv);
    } else if (command == "validate") {
        return cmd_validate(argc, argv);
    } else if (command == "help" || command == "--help" || command == "-h") {
        print_usage(argv[0]);
        return 0;
    } else {
        std::cerr << "Error: Unknown command '" << command << "'\n\n";
        print_usage(argv[0]);
        return 1;
    }
}
