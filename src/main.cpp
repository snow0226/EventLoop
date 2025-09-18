#include "configmgr.hpp"
#include "server.hpp"
#include <csignal>

static Server* g_server = nullptr;

void signal_handler(int signal) {
    if (g_server) {
        std::cout << "\nReceived signal " << signal << ", stopping server...\n";
        g_server->stop();
    }
}

int main() {
    ConfigMgr& cfg = ConfigMgr::Inst();
    if (!cfg.loadFromFile("ini/config.ini")) {
        std::cerr << "Failed to load config.ini\n";
        return 1;
    }
    int port = cfg.get<int>("server.port", 12345);
    std::cout << "server.port= " << port << std::endl;
    Server server(port);
    g_server = &server;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    server.run();
    return 0;
}