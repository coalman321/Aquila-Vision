#include "chassis_proxy_client/udp_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include <system_error>

namespace proxy_client {
UdpServer::UdpServer(int local_port, const std::string &remote_addr, int remote_port) :
    remote_addr_(remote_addr), remote_port_(remote_port), local_port_(local_port) {
    // open the socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ <= 0) {
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
    if (bind(socket_fd_, (const struct sockaddr *)&local_addr_cfg_, sizeof(local_addr_cfg_)) > 0) {
        throw std::system_error(errno, std::system_category(), "Failed to bind local socket for communication");
    }

    // make socket non blocking if blocking
    int socket_flags = fcntl(socket_fd_, F_GETFL);
    if (!(socket_flags & O_NONBLOCK)) {
        if (fcntl(socket_fd_, F_SETFL, socket_flags | O_NONBLOCK) < 0) {
            throw std::system_error(errno, std::system_category(), "unable to set nonblocking socket");
        }
    }
}

int UdpServer::processPackets() {
    // get the data
    std::vector<uint8_t> data;
    data.resize(1024);

    struct sockaddr_in cliaddr;
    memset(&cliaddr, 0, sizeof(cliaddr));
    unsigned int cli_len = sizeof(cliaddr);

    int received = recvfrom(socket_fd_, data.data(), data.size(), MSG_WAITALL, (struct sockaddr *)&cliaddr, &cli_len);

    // return 0 if timeout
    if (received < 0 && errno == EWOULDBLOCK) {
        return 0;
    } else if (received < 0) {
        throw std::system_error(errno, std::system_category(), "socket read failed");
    } else {
        // we received a packet.
    }

    // Pair down the size of the vector to what we received
    data.resize(received);

    // fire the correct callback
    sub_map_t::iterator it = sub_map_.find(data.at(0));
    if (it != sub_map_.end()) {
        it->second->invokeCallback(data);
    }

    return 1;
}

void UdpServer::sendPacket(const std::vector<uint8_t> &byte_buffer) {
    // request to send datagram
    // no need to specify server address in sendto
    // connect stores the peers IP and port
    int err = sendto(socket_fd_, byte_buffer.data(), byte_buffer.size(), 0, (struct sockaddr *)&remote_addr_cfg_,
                     sizeof(remote_addr_cfg_));
    if (err < 0) {
        throw std::system_error(errno, std::system_category(), "Failed to send message to server");
    }
}

}  // namespace proxy_client
