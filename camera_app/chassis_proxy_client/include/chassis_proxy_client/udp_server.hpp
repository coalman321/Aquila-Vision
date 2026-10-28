#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "chassis_proxy_client/proxy_subscription.hpp"

namespace proxy_client {

using message_map_t = std::unordered_map<uint8_t, std::deque<std::vector<uint8_t>>>;
using sub_map_t = std::unordered_map<uint8_t, std::unique_ptr<ProxySubscriptionBase>>;

class UdpServer {
   public:
    UdpServer(int local_port, const std::string& remote_addr, int remote_port);

    int processPackets();

    template <typename T>
    void registerType(std::function<void(T result)> callback) {
        sub_map_.emplace(T().msg_id, new ProxySubscription<T>(callback));
    }

    template <typename T>
    void sendMsg(const T& msg) {
        sendPacket(T::serialize(msg));
    }

   protected:
    void sendPacket(const std::vector<uint8_t>& byte_buffer);

   private:
    std::string remote_addr_;
    int remote_port_;
    struct sockaddr_in remote_addr_cfg_;  ///< remote address config

    int local_port_;
    struct sockaddr_in local_addr_cfg_;  ///< Local bind address config

    sub_map_t sub_map_;  ///< map of message id to subscription callback

    int socket_fd_ = -1;  ///< file descriptor for the socket
};
}  // namespace proxy_client
