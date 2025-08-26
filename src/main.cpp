#include "WebServer.hpp"
#include "utils.hpp"
#include <signal.h>

WebServer* g_server_instance = NULL;

void signal_handler(int sig) {
    if (g_server_instance && (sig == SIGINT || sig == SIGTERM)) {
        LOG_INFO("Received signal " + size_t_to_string(sig) + ", shutting down gracefully...");
        g_server_instance->cleanup();
        exit(0);
    }
}
int main(int argc, char* argv[]) {
    if (argc != 2){
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        std::cerr << "Example: " << argv[0] << " config/default.conf" << std::endl;
        return 1;
    }
    std::string config_file = argv[1];
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    WebServer server;
    g_server_instance = &server;

    if (!server.initialize(config_file)) {
        LOG_ERROR("Server initialization failed");
        return 1;
    }

    server.run();

    server.cleanup();
    return 0;
}