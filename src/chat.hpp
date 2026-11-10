#ifndef CHAT_HPP
#define CHAT_HPP

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace chat {

// Wire protocol (UTF-8 text, one command per newline, maximum 1024 bytes):
// Client -> server: LOGIN|CUSTOMER|name, LOGIN|TECHNICIAN|name,
//                   REQUEST_NEXT, MESSAGE|text, END_SESSION, DISCONNECT
// Server -> client: WAITING|customer_id or status, ASSIGN|customer_id|name,
//                   TECHNICIAN_READY, QUEUE_EMPTY, BUSY, MESSAGE|text,
//                   SESSION_ENDED[|reason], ERROR|reason
// DISCONNECT is valid for either role. Names are 1-64 bytes and cannot contain '|', CR, or LF. Message text can
// contain '|' but cannot contain LF. A trailing CR before LF is ignored.
constexpr std::size_t kMaxLineLength = 1024;

inline bool send_all(int socket_fd, const std::string& message) {
    std::size_t sent = 0;
    while (sent < message.size()) {
        const ssize_t result = send(socket_fd, message.data() + sent,
                                    message.size() - sent, MSG_NOSIGNAL);
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (result == 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

class LineReader {
public:
    explicit LineReader(int socket_fd) : socket_fd_(socket_fd) {}

    bool read_line(std::string& line, std::string& error) {
        while (true) {
            const std::size_t newline = buffer_.find('\n');
            if (newline != std::string::npos) {
                if (newline > kMaxLineLength) {
                    error = "line exceeds maximum length";
                    return false;
                }
                line.assign(buffer_, 0, newline);
                buffer_.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            if (buffer_.size() > kMaxLineLength) {
                error = "line exceeds maximum length";
                return false;
            }

            char chunk[512];
            const ssize_t received = recv(socket_fd_, chunk, sizeof(chunk), 0);
            if (received < 0) {
                if (errno == EINTR) continue;
                error = std::strerror(errno);
                return false;
            }
            if (received == 0) {
                error = buffer_.empty() ? "peer disconnected" : "incomplete final line";
                return false;
            }
            buffer_.append(chunk, static_cast<std::size_t>(received));
        }
    }

private:
    int socket_fd_;
    std::string buffer_;
};

inline std::vector<std::string> split_fields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t separator = line.find('|', start);
        if (separator == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, separator - start));
        start = separator + 1;
    }
    return fields;
}

inline bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char character : name) {
        if (character == '|' || character == '\n' || character == '\r') return false;
    }
    return true;
}

}  // namespace chat

#endif
