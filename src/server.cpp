#include "chat.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace {

struct Customer {
    std::string name;
    int id;
};

struct ServerState {
    std::mutex mutex;
    std::unordered_map<int, Customer> customers;
    std::queue<int> waiting;
    int technician_fd = -1;
    std::string technician_name;
    int active_customer_fd = -1;
    int next_customer_id = 1;
};

ServerState state;
constexpr int kBacklog = 16;

void close_socket(int fd, const char* description) {
    if (close(fd) < 0) {
        std::cerr << "close " << description << " failed: " << std::strerror(errno) << '\n';
    }
}

bool send_line(int fd, const std::string& line) {
    if (chat::send_all(fd, line + "\n")) return true;
    std::cerr << "send failed: " << std::strerror(errno) << '\n';
    return false;
}

// Call only while holding state.mutex. dup() is local fd bookkeeping, not network I/O.
int duplicate_for_send(int fd) {
    if (fd < 0) return -1;
    int duplicate_fd;
    do {
        duplicate_fd = dup(fd);
    } while (duplicate_fd < 0 && errno == EINTR);
    if (duplicate_fd < 0) {
        std::cerr << "dup failed: " << std::strerror(errno) << '\n';
    }
    return duplicate_fd;
}

bool send_line_and_close(int duplicate_fd, const std::string& line) {
    if (duplicate_fd < 0) return false;
    const bool sent = send_line(duplicate_fd, line);
    close_socket(duplicate_fd, "temporary socket duplicate");
    return sent;
}

bool read_line(chat::LineReader& reader, std::string& line) {
    std::string error;
    if (reader.read_line(line, error)) return true;
    if (error != "peer disconnected") {
        std::cerr << "connection read ended: " << error << '\n';
    }
    return false;
}

void remove_waiting_customer(int customer_fd) {
    std::queue<int> remaining;
    while (!state.waiting.empty()) {
        const int queued_fd = state.waiting.front();
        state.waiting.pop();
        if (queued_fd != customer_fd) remaining.push(queued_fd);
    }
    state.waiting.swap(remaining);
}

void push_waiting_front(int customer_fd) {
    std::queue<int> remaining;
    remaining.push(customer_fd);
    while (!state.waiting.empty()) {
        remaining.push(state.waiting.front());
        state.waiting.pop();
    }
    state.waiting.swap(remaining);
}

void cleanup_customer(int fd) {
    int technician_send_fd = -1;
    bool ended_active_session = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        remove_waiting_customer(fd);
        state.customers.erase(fd);
        if (state.active_customer_fd == fd) {
            state.active_customer_fd = -1;
            technician_send_fd = duplicate_for_send(state.technician_fd);
            ended_active_session = true;
        }
    }
    if (ended_active_session && technician_send_fd >= 0) {
        send_line_and_close(technician_send_fd, "SESSION_ENDED|CUSTOMER_DISCONNECTED");
    }
}

void handle_customer(int fd, chat::LineReader& reader, const std::string& name) {
    int customer_id = 0;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        customer_id = state.next_customer_id++;
        state.customers.emplace(fd, Customer{name, customer_id});
        state.waiting.push(fd);
    }
    if (!send_line(fd, "WAITING|" + std::to_string(customer_id))) return;

    std::string line;
    while (read_line(reader, line)) {
        const auto fields = chat::split_fields(line);
        if (fields.size() == 1 && fields[0] == "DISCONNECT") return;
        if (line.rfind("MESSAGE|", 0) == 0) {
            if (line.size() == 8) {
                send_line(fd, "ERROR|message text cannot be empty");
                return;
            }
            int technician_send_fd = -1;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (state.active_customer_fd == fd) {
                    technician_send_fd = duplicate_for_send(state.technician_fd);
                }
            }
            if (technician_send_fd < 0) {
                if (!send_line(fd, "ERROR|customer is not in an active session")) return;
            } else if (!send_line_and_close(technician_send_fd, line)) {
                send_line(fd, "ERROR|message delivery failed");
                return;
            }
            continue;
        }
        send_line(fd, "ERROR|unexpected customer command");
        return;
    }
}

void handle_technician(int fd, chat::LineReader& reader) {
    std::string line;
    while (read_line(reader, line)) {
        const auto fields = chat::split_fields(line);
        if (fields.size() == 1 && fields[0] == "DISCONNECT") return;
        if (line.rfind("MESSAGE|", 0) == 0) {
            if (line.size() == 8) {
                send_line(fd, "ERROR|message text cannot be empty");
                return;
            }
            int customer_send_fd = -1;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (state.technician_fd == fd) {
                    customer_send_fd = duplicate_for_send(state.active_customer_fd);
                }
            }
            if (customer_send_fd < 0) {
                if (!send_line(fd, "ERROR|no active customer session")) return;
            } else if (!send_line_and_close(customer_send_fd, line)) {
                send_line(fd, "ERROR|message delivery failed");
                return;
            }
            continue;
        }
        if (line.rfind("BROADCAST|", 0) == 0) {
            if (line.size() == 10) {
                if (!send_line(fd, "ERROR|broadcast text cannot be empty")) return;
                continue;
            }

            std::vector<int> customer_send_fds;
            bool technician_registered = false;
            std::size_t duplicate_failures = 0;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                technician_registered = state.technician_fd == fd;
                if (technician_registered) {
                    customer_send_fds.reserve(state.customers.size());
                    for (const auto& entry : state.customers) {
                        const int duplicate_fd = duplicate_for_send(entry.first);
                        if (duplicate_fd >= 0) customer_send_fds.push_back(duplicate_fd);
                        else ++duplicate_failures;
                    }
                }
            }
            if (!technician_registered) {
                if (!send_line(fd, "ERROR|technician is not registered")) return;
                continue;
            }

            std::size_t delivered = 0;
            for (const int customer_send_fd : customer_send_fds) {
                if (send_line_and_close(customer_send_fd, line)) ++delivered;
            }
            if (delivered != customer_send_fds.size() || duplicate_failures != 0) {
                if (!send_line(fd, "ERROR|broadcast delivery failed for one or more customers")) return;
            } else if (!send_line(fd, "BROADCAST_SENT|" + std::to_string(delivered))) {
                return;
            }
            continue;
        }
        if (fields.size() == 1 && fields[0] == "REQUEST_NEXT") {
            int customer_fd = -1;
            int customer_send_fd = -1;
            int customer_id = 0;
            std::string customer_name;
            std::string technician_name;
            bool busy = false;
            bool duplicate_failed = false;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (state.active_customer_fd >= 0) {
                    busy = true;
                } else {
                    while (!state.waiting.empty()) {
                        const int candidate = state.waiting.front();
                        state.waiting.pop();
                        const auto found = state.customers.find(candidate);
                        if (found != state.customers.end()) {
                            customer_fd = candidate;
                            customer_id = found->second.id;
                            customer_name = found->second.name;
                            technician_name = state.technician_name;
                            state.active_customer_fd = candidate;
                            customer_send_fd = duplicate_for_send(candidate);
                            if (customer_send_fd < 0) {
                                state.active_customer_fd = -1;
                                push_waiting_front(candidate);
                                duplicate_failed = true;
                                customer_fd = -1;
                            }
                            break;
                        }
                    }
                }
            }
            if (busy) {
                if (!send_line(fd, "BUSY")) return;
            } else if (duplicate_failed) {
                if (!send_line(fd, "ERROR|could not prepare customer assignment")) return;
            } else if (customer_fd < 0) {
                if (!send_line(fd, "QUEUE_EMPTY")) return;
            } else {
                const std::string customer_assignment =
                    "ASSIGN|" + std::to_string(customer_id) + "|" + technician_name;
                const std::string technician_assignment =
                    "ASSIGN|" + std::to_string(customer_id) + "|" + customer_name;
                const bool customer_notified = send_line_and_close(customer_send_fd, customer_assignment);
                const bool technician_notified = send_line(fd, technician_assignment);
                if (!customer_notified || !technician_notified) {
                    bool returned_to_queue = false;
                    int customer_status_fd = -1;
                    {
                        std::lock_guard<std::mutex> lock(state.mutex);
                        if (state.active_customer_fd == customer_fd) {
                            state.active_customer_fd = -1;
                            if (state.customers.count(customer_fd) != 0) {
                                push_waiting_front(customer_fd);
                                customer_status_fd = duplicate_for_send(customer_fd);
                                returned_to_queue = true;
                            }
                        }
                    }
                    if (returned_to_queue) {
                        send_line_and_close(customer_status_fd, "WAITING|ASSIGNMENT_FAILED");
                    }
                    if (!technician_notified) return;
                    if (!send_line(fd, "ERROR|assignment failed; customer returned to queue")) return;
                }
            }
            continue;
        }
        if (fields.size() == 1 && fields[0] == "END_SESSION") {
            int customer_fd = -1;
            int customer_send_fd = -1;
            bool had_active_session = false;
            bool could_not_prepare_notification = false;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                customer_fd = state.active_customer_fd;
                if (customer_fd >= 0 && state.customers.count(customer_fd) != 0) {
                    customer_send_fd = duplicate_for_send(customer_fd);
                    if (customer_send_fd >= 0) {
                        state.active_customer_fd = -1;
                        state.waiting.push(customer_fd);
                        had_active_session = true;
                    } else {
                        customer_fd = -1;
                        could_not_prepare_notification = true;
                    }
                } else {
                    customer_fd = -1;
                    state.active_customer_fd = -1;
                }
            }
            if (could_not_prepare_notification) {
                if (!send_line(fd, "ERROR|could not notify customer; session remains active")) return;
                continue;
            }
            if (!had_active_session) {
                if (!send_line(fd, "ERROR|no active customer session")) return;
                continue;
            }
            if (customer_fd >= 0 && !send_line_and_close(customer_send_fd, "WAITING|SESSION_ENDED")) {
                // The customer's handler will remove its stale queue entry after noticing closure.
            }
            if (!send_line(fd, "SESSION_ENDED")) return;
            continue;
        }
        send_line(fd, "ERROR|unexpected technician command");
        return;
    }
}

void handle_client(int fd) {
    chat::LineReader reader(fd);
    std::string login;
    if (!read_line(reader, login)) {
        close_socket(fd, "client socket");
        return;
    }
    const auto fields = chat::split_fields(login);
    if (fields.size() != 3 || fields[0] != "LOGIN" || !chat::valid_name(fields[2])) {
        send_line(fd, "ERROR|invalid login");
        close_socket(fd, "client socket");
        return;
    }

    if (fields[1] == "CUSTOMER") {
        handle_customer(fd, reader, fields[2]);
        cleanup_customer(fd);
    } else if (fields[1] == "TECHNICIAN") {
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.technician_fd < 0) {
                state.technician_fd = fd;
                state.technician_name = fields[2];
                accepted = true;
            }
        }
        if (!accepted) {
            send_line(fd, "ERROR|technician already connected");
            close_socket(fd, "client socket");
            return;
        }
        if (send_line(fd, "TECHNICIAN_READY")) {
            handle_technician(fd, reader);
        }

        int customer_to_requeue = -1;
        int customer_status_fd = -1;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.technician_fd == fd) {
                state.technician_fd = -1;
                state.technician_name.clear();
            }
            customer_to_requeue = state.active_customer_fd;
            if (customer_to_requeue >= 0) {
                state.active_customer_fd = -1;
                if (state.customers.count(customer_to_requeue) != 0) {
                    push_waiting_front(customer_to_requeue);
                    customer_status_fd = duplicate_for_send(customer_to_requeue);
                } else {
                    customer_to_requeue = -1;
                }
            }
        }
        if (customer_to_requeue >= 0 && customer_status_fd >= 0) {
            send_line_and_close(customer_status_fd, "WAITING|TECHNICIAN_DISCONNECTED");
        }
    } else {
        send_line(fd, "ERROR|unknown role");
    }
    close_socket(fd, "client socket");
}

bool parse_port(const char* text, unsigned short& port) {
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1 || value > 65535) return false;
    port = static_cast<unsigned short>(value);
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>\n";
        return 1;
    }
    unsigned short port = 0;
    if (!parse_port(argv[1], port)) {
        std::cerr << "Port must be a number from 1 to 65535\n";
        return 1;
    }

    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed: " << std::strerror(errno) << '\n';
        close_socket(server_fd, "listening socket");
        return 1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << '\n';
        close_socket(server_fd, "listening socket");
        return 1;
    }
    if (listen(server_fd, kBacklog) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << '\n';
        close_socket(server_fd, "listening socket");
        return 1;
    }
    std::cout << "Server listening on port " << port << '\n';

    while (true) {
        sockaddr_in peer{};
        socklen_t peer_size = sizeof(peer);
        int client_fd;
        do {
            client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&peer), &peer_size);
        } while (client_fd < 0 && errno == EINTR);
        if (client_fd < 0) {
            std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            close_socket(server_fd, "listening socket");
            return 1;
        }
        std::thread handler;
        try {
            handler = std::thread(handle_client, client_fd);
        } catch (const std::exception& error) {
            std::cerr << "could not start handler: " << error.what() << '\n';
            close_socket(client_fd, "client socket");
            continue;
        }
        try {
            handler.detach();
        } catch (const std::exception& error) {
            std::cerr << "could not detach handler; waiting for it to finish: "
                      << error.what() << '\n';
            if (handler.joinable()) handler.join();
        }
    }
}
