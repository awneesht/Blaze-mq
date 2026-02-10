#pragma once
#include "core/types.h"
#include "protocol/kafka_codec.h"
#include "utils/endian.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef BLAZE_PLATFORM_MACOS
#include <sys/event.h>
#else
#include <sys/epoll.h>
#endif

namespace blaze {
namespace network {

struct Connection {
    int fd = -1;
    std::vector<uint8_t> read_buf;
    std::vector<uint8_t> write_buf;
    size_t write_offset = 0;

    Connection() { read_buf.reserve(65536); }

    void queue_write(const uint8_t* data, size_t len) {
        write_buf.insert(write_buf.end(), data, data + len);
    }

    bool has_pending_write() const {
        return write_offset < write_buf.size();
    }

    void compact_write_buf() {
        if (write_offset > 0) {
            write_buf.erase(write_buf.begin(),
                            write_buf.begin() + static_cast<ptrdiff_t>(write_offset));
            write_offset = 0;
        }
    }
};

class TcpServer {
public:
    explicit TcpServer(protocol::RequestRouter& router);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool bind_and_listen(const std::string& address, uint16_t port);
    void run();
    void stop();

private:
    void accept_connections();
    void handle_read(int fd);
    void handle_write(int fd);
    void process_frames(Connection& conn);
    void close_connection(int fd);
    void enable_write(int fd);
    void disable_write(int fd);

    static bool set_nonblocking(int fd);
    static bool set_nodelay(int fd);

    protocol::RequestRouter& router_;
    int listen_fd_ = -1;
    int event_fd_ = -1;  // kqueue fd or epoll fd
    std::atomic<bool> running_{false};
    std::unordered_map<int, Connection> connections_;

    static constexpr size_t kReadChunk = 65536;
    static constexpr size_t kMaxFrameSize = 104857600; // 100MB
    static constexpr int kMaxEvents = 64;
    static constexpr int kListenBacklog = 128;
};

} // namespace network
} // namespace blaze
