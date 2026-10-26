#pragma once

#include <basin/web_core/web_node.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

namespace basin::web_core {

/// @brief Configuration for basic file manager component
struct FileManagerComponentConfig {
    std::string base_route = "/api/files";           ///< Base route for file endpoints
    std::filesystem::path files_dir = "/tmp/basin_web_files";  ///< Directory for stored files
    bool enable_put_upload = true;                   ///< Allow raw PUT upload to route/<file>
};

/// @brief Component that registers file management API endpoints on a WebNode
class FileManagerComponent {
   public:
    explicit FileManagerComponent(std::shared_ptr<WebNode> web_node);
    FileManagerComponent(std::shared_ptr<WebNode> web_node, const FileManagerComponentConfig& config);
    ~FileManagerComponent() = default;

    /// @brief List files in the managed directory
    std::vector<std::string> listFiles();

   private:
    void registerListRoute();
    void registerDownloadRoute();
    void registerDeleteRoute();
    void registerMoveRoute();
    void registerMultipartUploadRoute();
    void registerPutUploadRoute();

    std::shared_ptr<WebNode> web_node_;
    FileManagerComponentConfig config_;
    std::mutex file_mutex_;
};

}  // namespace basin::web_core
