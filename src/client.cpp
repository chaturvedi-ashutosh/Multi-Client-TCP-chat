#include "chat.hpp"

#include <cerrno>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

std::mutex output_mutex;

void print_event(const std::string& event) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << event << '\n';
}

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
        if (fields.size() == 2 && fields[0] == "WAITING") {
            assigned.store(false);
            if (fields[1] == "SESSION_ENDED") {
                print_event("[STATUS] Session ended. You are back in the waiting queue.");
            } else if (fields[1] == "TECHNICIAN_DISCONNECTED") {
                print_event("[STATUS] Technician disconnected. You are back at the front of the queue.");
            } else if (fields[1] == "ASSIGNMENT_FAILED") {
                print_event("[STATUS] Assignment failed. You are waiting for another technician request.");
            } else {
                print_event("[STATUS] Waiting for a technician. Customer ID: " + fields[1]);
            }
        } else if (fields.size() == 3 && fields[0] == "ASSIGN") {
            assigned.store(true);
            print_event("[STATUS] Chatting with technician " + fields[2] +
                        ". Customer ID: " + fields[1]);
        } else if (fields.size() >= 2 && fields[0] == "MESSAGE") {
            print_event("[TECHNICIAN] " + line.substr(8));
        } else if (fields.size() >= 2 && fields[0] == "BROADCAST") {
            print_event("[PUBLIC NOTICE] " + line.substr(10));
        } else if (fields.size() >= 2 && fields[0] == "ERROR") {
            print_event("[ERROR] " + line.substr(6));
        } else {
            print_event("[SERVER] " + line);
        }
    }
    if (error != "peer disconnected") {
        print_event("[CONNECTION] Closed: " + error);
    } else {
        print_event("[CONNECTION] Server disconnected.");
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
    const std::string name = argc == 4 ? argv[3] : "Customer";
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
    if (!chat::send_all(fd, "LOGIN|CUSTOMER|" + name + "\n")) {
        std::cerr << "login send failed: " << std::strerror(errno) << '\n';
        if (close(fd) < 0) std::cerr << "close failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(output_mutex);
        std::cout << "========================================\n"
                  << "       CUSTOMER SUPPORT CHAT\n"
                  << "========================================\n"
                  << "Name: " << name << "\n"
                  << "Server: " << argv[1] << ':' << port << "\n"
                  << "Status: Connecting...\n"
                  << "----------------------------------------\n"
                  << "Messages and status updates appear below.\n"
                  << "Commands: /status  /quit\n"
                  << "========================================\n";
    }
    std::atomic<bool> assigned{false};
    std::thread receiver(receive_messages, fd, std::ref(assigned));
    std::string input;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::cout << "\n> " << std::flush;
        }
        if (!std::getline(std::cin, input)) break;
        if (input == "/quit") {
            if (!chat::send_all(fd, "DISCONNECT\n")) {
                std::cerr << "disconnect send failed: " << std::strerror(errno) << '\n';
            }
            break;
        }
        if (input == "/status") {
            print_event(assigned.load() ? "[STATUS] Chatting with a technician."
                                        : "[STATUS] Waiting for a technician.");
            continue;
        }
        if (assigned.load()) {
            if (!chat::send_all(fd, "MESSAGE|" + input + "\n")) {
                std::cerr << "message send failed: " << std::strerror(errno) << '\n';
                break;
            }
        } else {
            print_event("[STATUS] Your message was not sent; wait for a technician to join.");
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
