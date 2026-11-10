#include "chat.hpp"

#include <cerrno>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

bool parse_port(const char* text, std::string& port) {
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1 || value > 65535) return false;
    port = std::to_string(value);
    return true;
}

void receive_messages(int fd, std::atomic<bool>& assigned) {
    chat::LineReader reader(fd);
    std::string line;
    std::string error;
    while (reader.read_line(line, error)) {
        const auto fields = chat::split_fields(line);
        if (fields.size() == 3 && fields[0] == "ASSIGN") {
            assigned.store(true);
            std::cout << "Assigned customer " << fields[2] << " (ID " << fields[1] << ")\n";
        } else if (line == "SESSION_ENDED" || line.rfind("SESSION_ENDED|", 0) == 0) {
            assigned.store(false);
            std::cout << "Session ended.\n";
        } else if (line == "QUEUE_EMPTY") {
            assigned.store(false);
            std::cout << "No customers are waiting.\n";
        } else if (line == "BUSY") {
            assigned.store(true);
            std::cout << "A customer session is already active.\n";
        } else if (fields.size() >= 2 && fields[0] == "MESSAGE") {
            std::cout << "Customer: " << line.substr(8) << '\n';
        } else if (fields.size() >= 2 && fields[0] == "ERROR") {
            if (line.find("assignment failed") != std::string::npos) assigned.store(false);
            std::cout << "Error: " << line.substr(6) << '\n';
        } else {
            std::cout << "Server: " << line << '\n';
        }
    }
    if (error != "peer disconnected") {
        std::cerr << "connection closed: " << error << '\n';
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> [name]\n";
        return 1;
    }
    std::string port;
    if (!parse_port(argv[2], port)) {
        std::cerr << "Port must be a number from 1 to 65535\n";
        return 1;
    }
    const std::string name = argc == 4 ? argv[3] : "Technician";
    if (!chat::valid_name(name)) {
        std::cerr << "Name must be 1 to 64 characters and cannot contain |\n";
        return 1;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const int lookup = getaddrinfo(argv[1], port.c_str(), &hints, &addresses);
    if (lookup != 0) {
        std::cerr << "getaddrinfo failed: " << gai_strerror(lookup) << '\n';
        return 1;
    }
    int fd = -1;
    int last_error = 0;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            last_error = errno;
            continue;
        }
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        last_error = errno;
        if (close(fd) < 0) std::cerr << "close failed: " << std::strerror(errno) << '\n';
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) {
        std::cerr << "could not connect: " << std::strerror(last_error) << '\n';
        return 1;
    }
    if (!chat::send_all(fd, "LOGIN|TECHNICIAN|" + name + "\n")) {
        std::cerr << "login send failed: " << std::strerror(errno) << '\n';
        if (close(fd) < 0) std::cerr << "close failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    std::cout << "Connected as " << name
              << ". Commands: next, end, /status, /quit. While assigned, other text is sent to the customer.\n";
    std::atomic<bool> assigned{false};
    std::thread receiver(receive_messages, fd, std::ref(assigned));
    std::string input;
    while (std::getline(std::cin, input)) {
        if (input == "/quit") {
            if (!chat::send_all(fd, "DISCONNECT\n")) {
                std::cerr << "disconnect send failed: " << std::strerror(errno) << '\n';
            }
            break;
        }
        if (input == "/status") {
            std::cout << (assigned.load() ? "Status: active customer session.\n"
                                          : "Status: available for the next customer.\n");
            continue;
        }
        if (input == "next") {
            if (!chat::send_all(fd, "REQUEST_NEXT\n")) {
                std::cerr << "request send failed: " << std::strerror(errno) << '\n';
                break;
            }
        } else if (input == "end") {
            if (!chat::send_all(fd, "END_SESSION\n")) {
                std::cerr << "end-session send failed: " << std::strerror(errno) << '\n';
                break;
            }
        } else if (assigned.load()) {
            if (!chat::send_all(fd, "MESSAGE|" + input + "\n")) {
                std::cerr << "message send failed: " << std::strerror(errno) << '\n';
                break;
            }
        } else {
            std::cout << "Use next or /quit.\n";
        }
    }
    if (shutdown(fd, SHUT_RDWR) < 0 && errno != ENOTCONN) {
        std::cerr << "shutdown failed: " << std::strerror(errno) << '\n';
    }
    receiver.join();
    if (close(fd) < 0) {
        std::cerr << "close failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    return 0;
}
