#include "basin/web_core/file_manager_component.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace basin::web_core {

// JSON string escaping is handled by nlohmann::json when needed; no manual escape required.

static bool isPathInside(const std::filesystem::path& dir, const std::filesystem::path& path) {
    try {
        auto canon_dir = std::filesystem::canonical(dir);
        auto canon_path = std::filesystem::weakly_canonical(path);
        auto dir_str = canon_dir.string();
        auto path_str = canon_path.string();
        if (!dir_str.empty() && dir_str.back() != std::filesystem::path::preferred_separator) {
            dir_str += std::filesystem::path::preferred_separator;
        }
        return path_str.rfind(dir_str, 0) == 0;
    } catch (const std::exception&) {
        return false;
    }
}

FileManagerComponent::FileManagerComponent(std::shared_ptr<WebNode> web_node) :
    FileManagerComponent(web_node, FileManagerComponentConfig{}) {}

FileManagerComponent::FileManagerComponent(std::shared_ptr<WebNode> web_node, const FileManagerComponentConfig& config) :
    web_node_(web_node), config_(config) {
    if (!web_node_) {
        throw std::invalid_argument("WebNode pointer cannot be null");
    }

    try {
        std::filesystem::create_directories(config_.files_dir);
    } catch (const std::exception& e) {
        RCLCPP_ERROR(web_node_->get_logger(), "Failed to create files directory: %s", e.what());
    }

    registerListRoute();
    registerDownloadRoute();
    registerDeleteRoute();
    registerMoveRoute();
    if (config_.enable_put_upload) registerPutUploadRoute();
    registerMultipartUploadRoute();

    RCLCPP_INFO(web_node_->get_logger(), "FileManagerComponent initialized on route %s serving directory %s", config_.base_route.c_str(), config_.files_dir.c_str());
}

void FileManagerComponent::registerMultipartUploadRoute() {
    web_node_->getServer().Post(config_.base_route, [this](const httplib::Request& req, httplib::Response& res) {
        if (!req.is_multipart_form_data()) {
            res.status = 400;
            res.set_content("{\"error\": \"Content-Type must be multipart/form-data\"}", "application/json");
            return;
        }
        int count = 0;
        for (const auto& item : req.form.files) {
            const auto& file = item.second;
            auto filename = file.filename;
            if (filename.empty()) continue;
            auto dest = std::filesystem::path(config_.files_dir) / filename;
            if (!isPathInside(config_.files_dir, dest)) {
                RCLCPP_WARN(web_node_->get_logger(), "Rejected multipart filename for traversal: %s", filename.c_str());
                continue;
            }
            try {
                std::filesystem::create_directories(dest.parent_path());
                std::ofstream ofs(dest, std::ios::binary);
                ofs.write(file.content.data(), file.content.size());
                ofs.close();
                count++;
            } catch (const std::exception& e) {
                RCLCPP_ERROR(web_node_->get_logger(), "Failed to save file: %s", e.what());
            }
        }
        if (count > 0) {
            json j;
            j["message"] = "Files uploaded successfully";
            j["count"] = count;
            res.set_content(j.dump(), "application/json");
        } else {
            res.status = 400;
            res.set_content("{\"error\": \"No valid files found in request\"}", "application/json");
        }
    });
}

void FileManagerComponent::registerPutUploadRoute() {
    web_node_->getServer().Put(config_.base_route + "/(.*)", [this](const httplib::Request& req, httplib::Response& res) {
        auto path = req.path;
        std::string filename;
        if (path.size() > config_.base_route.size() + 1) {
            filename = path.substr(config_.base_route.size() + 1);
        }
        if (filename.empty()) {
            res.status = 400;
            res.set_content("{\"error\": \"Filename required in URL\"}", "application/json");
            return;
        }
        auto dest = std::filesystem::path(config_.files_dir) / filename;
        if (!isPathInside(config_.files_dir, dest)) {
            res.status = 403;
            res.set_content("{\"error\": \"Invalid filename or path\"}", "application/json");
            return;
        }
        try {
            std::filesystem::create_directories(dest.parent_path());
            std::ofstream ofs(dest, std::ios::binary);
            ofs.write(req.body.data(), req.body.size());
            ofs.close();
            json j;
            j["message"] = "File uploaded successfully";
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "Failed to save file via PUT: %s", e.what());
            res.status = 500;
            res.set_content("{\"error\": \"Failed to save file\"}", "application/json");
        }
    });
}

void FileManagerComponent::registerListRoute() {
    web_node_->getServer().Get(config_.base_route, [this](const httplib::Request&, httplib::Response& res) {
        json response_json = json::object();
        json files_arr = json::array();
        try {
            for (const auto& entry : std::filesystem::directory_iterator(config_.files_dir)) {
                if (!entry.is_regular_file()) continue;
                json f;
                f["name"] = entry.path().filename().string();
                f["size"] = entry.file_size();
                auto ftime = std::filesystem::last_write_time(entry.path());
                using namespace std::chrono;
                auto sctp = time_point_cast<system_clock::duration>(ftime - decltype(ftime)::clock::now() + system_clock::now());
                std::time_t cftime = system_clock::to_time_t(sctp);
                f["mtime"] = cftime;
                files_arr.push_back(f);
            }
            response_json["files"] = files_arr;
            res.set_content(response_json.dump(), "application/json");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "Failed to list files: %s", e.what());
            res.status = 500;
            res.set_content("{\"error\": \"Failed to list files\"}", "application/json");
        }
    });
}

void FileManagerComponent::registerDownloadRoute() {
    web_node_->getServer().Get(config_.base_route + "/(.*)", [this](const httplib::Request& req, httplib::Response& res) {
        auto path = req.path;
        std::string filename;
        if (path.size() > config_.base_route.size() + 1) {
            filename = path.substr(config_.base_route.size() + 1);
        }
        if (filename.empty()) {
            res.status = 400;
            res.set_content("{\"error\": \"Filename required\"}", "application/json");
            return;
        }
        auto dest = std::filesystem::path(config_.files_dir) / filename;
        if (!isPathInside(config_.files_dir, dest) || !std::filesystem::exists(dest) || !std::filesystem::is_regular_file(dest)) {
            res.status = 404;
            res.set_content("{\"error\": \"File not found\"}", "application/json");
            return;
        }
        try {
            std::ifstream ifs(dest, std::ios::binary);
            std::ostringstream out;
            out << ifs.rdbuf();
            res.set_content(out.str(), "application/octet-stream");
            res.set_header("Content-Disposition", std::string("attachment; filename=\"") + dest.filename().string() + std::string("\""));
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "Failed to read file: %s : %s", dest.c_str(), e.what());
            res.status = 500;
            res.set_content("{\"error\": \"Failed to read file\"}", "application/json");
        }
    });
}

void FileManagerComponent::registerMoveRoute() {
    web_node_->getServer().Patch(config_.base_route + "/(.*)", [this](const httplib::Request& req, httplib::Response& res) {
        auto path = req.path;
        std::string filename;
        if (path.size() > config_.base_route.size() + 1) {
            filename = path.substr(config_.base_route.size() + 1);
        }
        if (filename.empty()) {
            res.status = 400;
            res.set_content("{\"error\": \"Source filename required in URL\"}", "application/json");
            return;
        }
        json body;
        try {
            body = json::parse(req.body);
        } catch (const json::exception&) {
            res.status = 400;
            res.set_content("{\"error\": \"Invalid JSON body\"}", "application/json");
            return;
        }
        if (!body.contains("name") || !body["name"].is_string()) {
            res.status = 400;
            res.set_content("{\"error\": \"Body must contain \\\"name\\\" field\"}", "application/json");
            return;
        }
        std::string new_name = body["name"].get<std::string>();
        auto src = std::filesystem::path(config_.files_dir) / filename;
        auto dst = std::filesystem::path(config_.files_dir) / new_name;
        if (!isPathInside(config_.files_dir, src) || !std::filesystem::exists(src) || !std::filesystem::is_regular_file(src)) {
            res.status = 404;
            res.set_content("{\"error\": \"File not found\"}", "application/json");
            return;
        }
        if (!isPathInside(config_.files_dir, dst)) {
            res.status = 403;
            res.set_content("{\"error\": \"Invalid destination path\"}", "application/json");
            return;
        }
        try {
            std::filesystem::create_directories(dst.parent_path());
            std::filesystem::rename(src, dst);
            json j;
            j["message"] = "File moved successfully";
            j["name"] = dst.filename().string();
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "Failed to move file: %s -> %s : %s", src.c_str(), dst.c_str(), e.what());
            res.status = 500;
            res.set_content("{\"error\": \"Failed to move file\"}", "application/json");
        }
    });
}

void FileManagerComponent::registerDeleteRoute() {
    web_node_->getServer().Delete(config_.base_route + "/(.*)", [this](const httplib::Request& req, httplib::Response& res) {
        auto path = req.path;
        std::string filename;
        if (path.size() > config_.base_route.size() + 1) {
            filename = path.substr(config_.base_route.size() + 1);
        }
        if (filename.empty()) {
            res.status = 400;
            res.set_content("{\"error\": \"Filename required\"}", "application/json");
            return;
        }
        auto dest = std::filesystem::path(config_.files_dir) / filename;
        if (!isPathInside(config_.files_dir, dest) || !std::filesystem::exists(dest)) {
            res.status = 404;
            res.set_content("{\"error\": \"File not found\"}", "application/json");
            return;
        }
        try {
            std::filesystem::remove(dest);
            json j;
            j["message"] = "File deleted";
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), "Failed to delete file: %s : %s", dest.c_str(), e.what());
            res.status = 500;
            res.set_content("{\"error\": \"Failed to delete file\"}", "application/json");
        }
    });
}

std::vector<std::string> FileManagerComponent::listFiles(){
    std::vector<std::string> file_list;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(config_.files_dir)) {
            if (entry.is_regular_file()) {
                file_list.push_back(entry.path().filename().string());
            }
        }
    } catch (const std::exception& e) {
        RCLCPP_ERROR(web_node_->get_logger(), "Failed to list files: %s", e.what());
    }
    return file_list;
}

}  // namespace basin::web_core
