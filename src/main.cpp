#include "server.hpp"

#include <csignal>
#include <iostream>

namespace {

tds::TcpDataServer* g_server = nullptr;

void handle_signal(int) {
    if (g_server != nullptr) {
        g_server->stop();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    tds::ServerConfig config;
    config.port = 9000;
    config.worker_threads = std::thread::hardware_concurrency();

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--workers" && i + 1 < argc) {
            config.worker_threads = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--bind" && i + 1 < argc) {
            config.bind_address = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: tds_server [--port PORT] [--workers N] [--bind ADDR]\n";
            return 0;
        }
    }

    tds::TcpDataServer server(config);
    g_server = &server;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << std::endl;
        return 1;
    }

    std::cout << "shutdown complete\n";
    return 0;
}
