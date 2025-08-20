#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

#include "WebServer.hpp"

struct LocationConfig {
	std::string path;
	std::string root;
	std::vector<std::string> allowed_methods;
	std::string index;
	bool autoindex;
	std::string cgi_extension;
	std::string cgi_path;
	std::string upload_path;
	std::map<int, std::string> error_pages;
	std::string redirect;
	
	LocationConfig() : autoindex(false) {}
};

struct ServerConfig {
    std::string host;
    int port;
    std::string server_name;
    std::string root;
    std::string index;
    size_t client_max_body_size;
    std::map<int, std::string> error_pages;
    std::vector<LocationConfig> locations;
};

class Config {
private:
    std::vector<ServerConfig> _servers;
    void parseSimpleDirective(const std::string& line, ServerConfig& server);
    ServerConfig getDefaultServerConfig();
    bool finalizeConfig(bool in_server_block);

    //conf utility
    bool shouldSkipLine(const std::string& line);
    std::string trim(const std::string& str);
    std::vector<std::string> splitLine(const std::string& line);
    
    bool isServerStart(const std::string& line);
    bool isServerEnd(const std::string& line);
    bool handleServerStart(bool& in_server_block, ServerConfig& current_server, int line_number, std::ifstream& file);
    bool handleServerEnd(bool& in_server_block, ServerConfig& current_server, int line_number, std::ifstream& file);
    bool handleDirective(bool in_server_block, const std::string& line, ServerConfig& current_server,
                            int line_number, std::ifstream& file);
    
    bool isLocationStart(const std::string& line);
    bool isLocationEnd(const std::string& line);
    std::string extractLocationPath(const std::string& line);

    void parseSimpleDirective(const std::string& line, LocationConfig& location);
	bool parseLocationBlock(std::ifstream& file, ServerConfig& server, 
							const std::string& location_path, int& line_number);
	void parseErrorPage(const std::string& line, std::map<int, std::string>& error_pages);
	void parseAllowedMethods(const std::string& line, std::vector<std::string>& methods);
    
public:
    Config();
    ~Config();
    
    bool parseConfigFile(const std::string& filename);
    void setDefaultConfig();
    
    const std::vector<ServerConfig>& getServers() const { return _servers; }
};

#endif