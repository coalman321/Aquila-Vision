#pragma once

#include <netinet/in.h>

#include <functional>
#include <string>
#include <unordered_map>

#include "chassis_proxy/v1/envelope.pb.h"

namespace proxy_client {

namespace v1 = chassis_proxy::v1;

class UdpServer {
   public:
    /// Outcome of one processPackets() call
    enum class ReceiveResult {
        kNoPacket,         ///< Nothing was waiting on the socket
        kDispatched,       ///< A packet was parsed and its payload's handler called
        kUnhandled,        ///< A packet was parsed but no handler is registered for its payload
        kMalformed,        ///< A packet was not a valid Envelope and was dropped
        kVersionMismatch,  ///< A packet's protocol_version is not ours and it was dropped
    };

    using handler_t = std::function<void(const v1::Envelope&)>;

    UdpServer(int local_port, const std::string& remote_addr, int remote_port);

    ~UdpServer();

    UdpServer(const UdpServer&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;

    /// Receives at most one packet and dispatches it to the handler registered for its payload.
    /// Throws std::system_error if the socket read itself fails.
    ReceiveResult processPackets();

    void registerHandler(v1::Envelope::PayloadCase payload, handler_t handler);

    /// Stamps the envelope with our protocol version and sends it to the remote address
    void send(v1::Envelope envelope);

   private:
    std::string remote_addr_;
    int remote_port_;
    struct sockaddr_in remote_addr_cfg_;  ///< remote address config

    int local_port_;
    struct sockaddr_in local_addr_cfg_;  ///< Local bind address config

    std::unordered_map<int, handler_t> handlers_;  ///< payload case to handler

    int socket_fd_ = -1;  ///< file descriptor for the socket
};
}  // namespace proxy_client
