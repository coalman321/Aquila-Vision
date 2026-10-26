#include "basin/web_core/static_server_component.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <rclcpp/logging.hpp>

namespace basin::web_core {

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

StaticServerComponent::StaticServerComponent(std::shared_ptr<WebNode> web_node) :
    StaticServerComponent(web_node, StaticServerComponentConfig{}) {}

StaticServerComponent::StaticServerComponent(std::shared_ptr<WebNode> web_node, const StaticServerComponentConfig& config) :
    web_node_(web_node), config_(config) {
    if (!web_node_) {
        throw std::invalid_argument("WebNode pointer cannot be null");
    }

    // Ensure the static directory exists
    if (!std::filesystem::exists(config_.static_dir)) {
        RCLCPP_WARN(web_node_->get_logger(), 
                    "Static directory does not exist: %s. Creating it...", 
                    config_.static_dir.c_str());
        try {
            std::filesystem::create_directories(config_.static_dir);
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), 
                        "Failed to create static directory: %s", e.what());
        }
    }


    registerStaticRoutes();

    RCLCPP_INFO(web_node_->get_logger(), 
                "StaticServerComponent initialized on route '%s' serving directory '%s'", 
                config_.base_route.c_str(), 
                config_.static_dir.c_str());
}

void StaticServerComponent::registerStaticRoutes() {
    // Normalize base route to ensure it starts with / and doesn't end with /
    std::string base = config_.base_route;
    if (base.empty() || base[0] != '/') {
        base = "/" + base;
    }
    if (base.length() > 1 && base.back() == '/') {
        base.pop_back();
    }

    // Create the route pattern - match everything under the base route
    std::string route_pattern = base;
    if (base == "/") {
        route_pattern = "/(.*)";
    } else {
        route_pattern = base + "/?(.*)";
    }

    web_node_->getServer().Get(route_pattern, [this, base](const httplib::Request& req, httplib::Response& res) {
        // Extract the requested path relative to the base route
        std::string request_path = req.path;
        std::string relative_path;
        
        if (base == "/") {
            // Root mount - use full path minus leading slash
            relative_path = request_path.substr(1);
        } else {
            // Mounted on a subdirectory
            if (request_path == base || request_path == base + "/") {
                relative_path = "";
            } else if (request_path.size() > base.size() + 1) {
                relative_path = request_path.substr(base.size() + 1);
            }
        }

        // If the path is empty or ends with /, try to serve the index file
        if (relative_path.empty() || relative_path.back() == '/') {
            RCLCPP_DEBUG(web_node_->get_logger(), "Redirected to index file for path: %s", request_path.c_str());
            res.set_redirect(base + "/" + config_.index_file);
            return;
        }

        // Construct the full file path
        auto file_path = std::filesystem::path(config_.static_dir) / relative_path;

        // Security check: ensure the requested file is within the static directory
        if (!isPathInside(config_.static_dir, file_path)) {
            RCLCPP_WARN(web_node_->get_logger(), 
                       "Rejected path traversal attempt: %s", request_path.c_str());
            res.status = 403;
            res.set_content("Forbidden", "text/plain");
            return;
        }

        // Check if it's a directory
        if (std::filesystem::is_directory(file_path)) {
            // Try to serve index file from the directory
            auto index_path = file_path / config_.index_file;
            if (std::filesystem::exists(index_path) && std::filesystem::is_regular_file(index_path)) {
                file_path = index_path;
            } else if (config_.enable_directory_listing) {
                // Generate simple directory listing
                std::string listing = "<html><head><title>Directory listing</title></head><body>";
                listing += "<h1>Directory listing for " + request_path + "</h1><ul>";
                
                try {
                    for (const auto& entry : std::filesystem::directory_iterator(file_path)) {
                        std::string name = entry.path().filename().string();
                        std::string link = request_path;
                        if (link.back() != '/') link += "/";
                        link += name;
                        if (entry.is_directory()) name += "/";
                        listing += "<li><a href=\"" + link + "\">" + name + "</a></li>";
                    }
                } catch (const std::exception& e) {
                    RCLCPP_ERROR(web_node_->get_logger(), 
                                "Error reading directory: %s", e.what());
                }
                
                listing += "</ul></body></html>";
                res.set_content(listing, "text/html");
                return;
            } else {
                res.status = 404;
                res.set_content("Not Found", "text/plain");
                return;
            }
        }

        // Check if the file exists; also try with .html extension (for SvelteKit flat output)
        if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path)) {
            auto html_path = std::filesystem::path(file_path.string() + ".html");
            if (std::filesystem::exists(html_path) && std::filesystem::is_regular_file(html_path) &&
                isPathInside(config_.static_dir, html_path)) {
                file_path = html_path;
            } else {
                RCLCPP_DEBUG(web_node_->get_logger(), 
                            "File not found: %s", file_path.string().c_str());
                res.status = 404;
                res.set_content("Not Found", "text/plain");
                return;
            }
        }

        // Read and serve the file
        try {
            std::ifstream file(file_path, std::ios::binary);
            if (!file) {
                RCLCPP_ERROR(web_node_->get_logger(), 
                            "Failed to open file: %s", file_path.string().c_str());
                res.status = 500;
                res.set_content("Internal Server Error", "text/plain");
                return;
            }

            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            std::string mime_type = getMimeType(file_path.string());
            res.set_content(content, mime_type);
            
            RCLCPP_DEBUG(web_node_->get_logger(), 
                        "Served file: %s (%s, %zu bytes)", 
                        file_path.string().c_str(), 
                        mime_type.c_str(), 
                        content.size());
        } catch (const std::exception& e) {
            RCLCPP_ERROR(web_node_->get_logger(), 
                        "Error reading file: %s", e.what());
            res.status = 500;
            res.set_content("Internal Server Error", "text/plain");
        }
    });
}

std::string StaticServerComponent::getMimeType(const std::string& path) const {
    std::string ext;
    size_t pos = path.find_last_of('.');
    if (pos != std::string::npos) {
        ext = path.substr(pos + 1);
        // Convert to lowercase
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    // Common MIME types
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "xml") return "application/xml";
    if (ext == "txt") return "text/plain";
    
    // Images
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "webp") return "image/webp";
    
    // Fonts
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf") return "font/ttf";
    if (ext == "otf") return "font/otf";
    if (ext == "eot") return "application/vnd.ms-fontobject";
    
    // Media
    if (ext == "mp4") return "video/mp4";
    if (ext == "webm") return "video/webm";
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "wav") return "audio/wav";
    if (ext == "ogg") return "audio/ogg";
    
    // Archives
    if (ext == "zip") return "application/zip";
    if (ext == "tar") return "application/x-tar";
    if (ext == "gz") return "application/gzip";
    
    // Documents
    if (ext == "pdf") return "application/pdf";
    
    // Default
    return "application/octet-stream";
}

}  // namespace basin::web_core
