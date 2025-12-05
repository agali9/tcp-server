#include "protocol.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int connect_to(const std::string& host, std::uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const std::vector<std::uint8_t>& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool recv_frame(int fd, std::vector<std::uint8_t>& buffer, tds::Message& out) {
    for (;;) {
        if (buffer.size() >= tds::kHeaderSize) {
            const tds::FrameParseResult parsed = tds::try_parse_frame(buffer);
            if (parsed.complete) {
                buffer.erase(buffer.begin(),
                             buffer.begin() + static_cast<std::ptrdiff_t>(parsed.bytes_consumed));
                out = std::move(parsed.message);
                return true;
            }
        }

        std::uint8_t chunk[4096];
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return false;
        }
        buffer.insert(buffer.end(), chunk, chunk + n);
    }
}

void print_usage() {
    std::cout << "Usage: tds_client [--host HOST] [--port PORT] <command>\n"
              << "Commands:\n"
              << "  ping\n"
              << "  echo <text>\n"
              << "  put <key> <value>\n"
              << "  get <key>\n"
              << "  stats\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
    int argi = 1;

    while (argi < argc) {
        const std::string arg = argv[argi];
        if (arg == "--host" && argi + 1 < argc) {
            host = argv[++argi];
        } else if (arg == "--port" && argi + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++argi]));
        } else {
            break;
        }
        ++argi;
    }

    if (argi >= argc) {
        print_usage();
        return 1;
    }

    const std::string command = argv[argi++];
    tds::MessageType type = tds::MessageType::Ping;
    std::vector<std::uint8_t> payload;

    if (command == "ping") {
        type = tds::MessageType::Ping;
    } else if (command == "echo" && argi < argc) {
        type = tds::MessageType::Echo;
        const std::string text = argv[argi++];
        payload.assign(text.begin(), text.end());
    } else if (command == "put" && argi + 1 < argc) {
        type = tds::MessageType::Put;
        payload = tds::encode_put_request(argv[argi], argv[argi + 1]);
        argi += 2;
    } else if (command == "get" && argi < argc) {
        type = tds::MessageType::Get;
        payload = tds::encode_get_request(argv[argi++]);
    } else if (command == "stats") {
        type = tds::MessageType::Stats;
    } else {
        print_usage();
        return 1;
    }

    const int fd = connect_to(host, port);
    if (fd < 0) {
        std::cerr << "connect failed\n";
        return 1;
    }

    const auto frame = tds::encode_message(type, payload);
    if (!send_all(fd, frame)) {
        std::cerr << "send failed\n";
        close(fd);
        return 1;
    }

    std::vector<std::uint8_t> buffer;
    tds::Message response;
    if (!recv_frame(fd, buffer, response)) {
        std::cerr << "recv failed\n";
        close(fd);
        return 1;
    }

    std::cout << "response type=" << static_cast<int>(response.type)
              << " payload_size=" << response.payload.size() << "\n";

    if (!response.payload.empty()) {
        std::cout.write(reinterpret_cast<const char*>(response.payload.data()),
                        static_cast<std::streamsize>(response.payload.size()));
        std::cout << "\n";
    }

    close(fd);
    return 0;
}
