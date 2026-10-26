#pragma once

#include <httplib.h>

#include <atomic>
#include <basin/node_core/node_core.hpp>
#include <filesystem>
#include <thread>

namespace basin::web_core {

class WebNode : public basin::node_core::NodeCore {
   public:
    WebNode(const std::string & name, const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    ~WebNode() override;

    /// @brief Tick function called periodically. Not used by web_core.
    void onTick() override {};

    /// @brief Shutdown the node and stop the server
    /// Overloaded to run stopServer automatically
    void shutdown() override;

    /// @brief Start the HTTP server in a separate thread.
    /// This must be called before the server can handle requests
    void startServer();

    /// @brief Stop the HTTP server
    void stopServer();

    /// @brief Get reference to the HTTP server for route registration
    /// @return Reference to the httplib::Server instance
    httplib::Server& getServer() { return svr_; }

   protected:
    httplib::Server svr_;  ///< HTTP server instance

   private:
    std::thread server_thread_;  ///< Thread used for running the server
    std::atomic<bool> running_;  ///< Flag indicating if the server thread should keep running
};

}  // namespace basin::web_core
