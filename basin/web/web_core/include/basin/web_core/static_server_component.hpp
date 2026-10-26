#pragma once

#include <basin/web_core/web_node.hpp>
#include <memory>
#include <string>

namespace basin::web_core {

/// @brief Configuration for static file server component
struct StaticServerComponentConfig {
    std::string base_route = "/";                      ///< Base route for serving static files (default: root "/")
    std::string static_dir = "/tmp/basin_web_static";  ///< Directory containing static files to serve
    std::string index_file = "index.html";             ///< Default file to serve for directory requests
    bool enable_directory_listing = false;             ///< Allow directory listing when index file is not found
};

/// @brief Component that serves static files from a directory on a WebNode
/// This component mounts a static file directory to a base route, allowing
/// the web server to serve HTML, CSS, JavaScript, images, and other static assets.
class StaticServerComponent {
   public:
    explicit StaticServerComponent(std::shared_ptr<WebNode> web_node);
    StaticServerComponent(std::shared_ptr<WebNode> web_node, const StaticServerComponentConfig& config);
    ~StaticServerComponent() = default;

   private:
    void registerStaticRoutes();
    std::string getMimeType(const std::string& path) const;

    std::shared_ptr<WebNode> web_node_;
    StaticServerComponentConfig config_;
};

}  // namespace basin::web_core
