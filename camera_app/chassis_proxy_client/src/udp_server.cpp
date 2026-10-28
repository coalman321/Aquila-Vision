#include "chassis_proxy_client/udp_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <system_error>

namespace proxy_client {

namespace {
// Largest datagram we accept; envelopes are far smaller than one Ethernet frame
constexpr size_t kMaxPacketSize = 1500;
}  // namespace

UdpServer::UdpServer(int local_port, const std::string &remote_addr, int remote_port) :
    remote_addr_(remote_addr), remote_port_(remote_port), local_port_(local_port) {
    // open the socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to open socket for communication");
    }

    // Translate the local hostname to an address
    memset(&local_addr_cfg_, 0, sizeof(local_addr_cfg_));
    local_addr_cfg_.sin_addr.s_addr = INADDR_ANY;
    local_addr_cfg_.sin_port = htons(local_port);
    local_addr_cfg_.sin_family = AF_INET;

    // Translate the remote hostname to an address
    memset(&remote_addr_cfg_, 0, sizeof(remote_addr_cfg_));
    remote_addr_cfg_.sin_addr.s_addr = inet_addr(remote_addr_.c_str());
    remote_addr_cfg_.sin_port = htons(remote_port_);
    remote_addr_cfg_.sin_family = AF_INET;

    // bind the local port
    if (bind(socket_fd_, (const struct sockaddr *)&local_addr_cfg_, sizeof(local_addr_cfg_)) < 0) {
        const int bind_errno = errno;
        close(socket_fd_);
        throw std::system_error(bind_errno, std::system_category(), "Failed to bind local socket for communication");
    }

    // make socket non blocking if blocking
    int socket_flags = fcntl(socket_fd_, F_GETFL);
    if (!(socket_flags & O_NONBLOCK)) {
        if (fcntl(socket_fd_, F_SETFL, socket_flags | O_NONBLOCK) < 0) {
            const int fcntl_errno = errno;
            close(socket_fd_);
            throw std::system_error(fcntl_errno, std::system_category(), "unable to set nonblocking socket");
        }
    }
}

UdpServer::~UdpServer() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
    }
}

UdpServer::ReceiveResult UdpServer::processPackets() {
    std::array<char, kMaxPacketSize> data;

    struct sockaddr_in cliaddr;
    memset(&cliaddr, 0, sizeof(cliaddr));
    socklen_t cli_len = sizeof(cliaddr);

    ssize_t received = recvfrom(socket_fd_, data.data(), data.size(), 0, (struct sockaddr *)&cliaddr, &cli_len);

    if (received < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
        return ReceiveResult::kNoPacket;
    } else if (received < 0) {
        throw std::system_error(errno, std::system_category(), "socket read failed");
    }

    v1::Envelope envelope;
    if (!envelope.ParseFromArray(data.data(), static_cast<int>(received))) {
        return ReceiveResult::kMalformed;
    }

    if (envelope.protocol_version() != v1::PROTOCOL_VERSION_CURRENT) {
        return ReceiveResult::kVersionMismatch;
    }

    // fire the correct callback
    auto it = handlers_.find(envelope.payload_case());
    if (it == handlers_.end()) {
        return ReceiveResult::kUnhandled;
    }
    it->second(envelope);

    return ReceiveResult::kDispatched;
}

void UdpServer::registerHandler(v1::Envelope::PayloadCase payload, handler_t handler) {
    handlers_[payload] = std::move(handler);
}

void UdpServer::send(v1::Envelope envelope) {
    envelope.set_protocol_version(v1::PROTOCOL_VERSION_CURRENT);
    const std::string byte_buffer = envelope.SerializeAsString();

    int err = sendto(socket_fd_, byte_buffer.data(), byte_buffer.size(), 0, (struct sockaddr *)&remote_addr_cfg_,
                     sizeof(remote_addr_cfg_));
    if (err < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to send message to server");
    }
}

}  // namespace proxy_client
