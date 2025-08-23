#include "WebServer.hpp"
#include "utils.hpp"

int main(int argc, char* argv[]) {
    std::string config_file = "config/default.conf";
    
    if (argc > 1) {
        config_file = argv[1];
    }
    
    WebServer server;
    
    if (!server.initialize(config_file)) {
        log_error("Failed to initialize server");
        return 1;
    }
    
    log_info("starting webserver...");
    server.run();
    
    return 0;
}