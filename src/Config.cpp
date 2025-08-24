#include "Config.hpp"
#include "utils.hpp"
#include <fstream>
#include <iostream>

Config::Config() {
}

Config::~Config() {
}

ServerConfig Config::getDefaultServerConfig() {
	ServerConfig default_server;
	default_server.host = "127.0.0.1";
	default_server.port = 8080;
	default_server.server_name = "localhost";
	default_server.root = "./www";
	default_server.index = "index.html";
	default_server.client_max_body_size = 1024 * 1024;
	return default_server;
}

void Config::setDefaultConfig() {
	_servers.clear();
	ServerConfig default_server = getDefaultServerConfig();

	LocationConfig default_location;
	default_location.path = "/";
	default_location.root = "./www";
	default_location.index = "index.html";
	default_location.autoindex = false;
	default_location.allowed_methods.push_back("GET");
	default_location.allowed_methods.push_back("POST");
	default_location.allowed_methods.push_back("DELETE");
	
	default_server.locations.push_back(default_location);
	_servers.push_back(default_server);
}

void Config::parseSimpleDirective(const std::string& line, ServerConfig& server) {
	std::istringstream iss(line);
	std::string key, value;
	iss >> key >> value;

	if (!value.empty() && value[value.length() - 1] == ';')
		value.erase(value.length() - 1);
	
	if (key == "listen") {
		// handle host and port
		size_t colon_pos = value.find(':');
		if (colon_pos != std::string::npos) {
			server.host = value.substr(0, colon_pos);
			server.port = atoi(value.substr(colon_pos + 1).c_str());
		} else {
			server.port = atoi(value.c_str());
		}
		log_debug("parsed listen: " + server.host + ":" + int_to_string(server.port));
	} else if (key == "server_name") {
		server.server_name = value;
		log_debug("parsed server_name: " + value);
	} else if (key == "root") {
		server.root = value;
		log_debug("parsed root: " + value);
	} else if (key == "index") {
		server.index = value;
		log_debug("parsed index: " + value);
	} else
		log_debug("unknown dir: " + key);
}

bool Config::parseLocationBlock(std::ifstream& file, ServerConfig& server, 
								const std::string& location_path, int& line_number) {
	LocationConfig location;
	location.path = location_path;
	location.root = server.root;
	location.index = server.index;
	
	std::string line;
	while (std::getline(file, line)) {
		line_number++;
		line = trim(line);
		
		if (shouldSkipLine(line)) 
			continue;
		
		if (isLocationEnd(line)) {
			server.locations.push_back(location);
			return true;
		}
		
		parseSimpleDirective(line, location);
	}
	
	return false;
}

void Config::parseSimpleDirective(const std::string& line, LocationConfig& location) {
	std::vector<std::string> tokens = splitLine(line);
	if (tokens.empty()) 
		return;
	
	std::string directive = tokens[0];
	
	if (directive == "root" && tokens.size() >= 2)
		location.root = tokens[1];
	else if (directive == "index" && tokens.size() >= 2)
		location.index = tokens[1];
	else if (directive == "autoindex" && tokens.size() >= 2)
		location.autoindex = (tokens[1] == "on");
	else if (directive == "allow_methods" || directive == "methods")
		parseAllowedMethods(line, location.allowed_methods);
	else if (directive == "cgi_extension" && tokens.size() >= 2)
		location.cgi_extension = tokens[1];
	else if (directive == "cgi_path" && tokens.size() >= 2)
		location.cgi_path = tokens[1];
	else if (directive == "upload_path" && tokens.size() >= 2)
		location.upload_path = tokens[1];
	else if (directive == "error_page")
		parseErrorPage(line, location.error_pages);
	else if (directive == "return" && tokens.size() >= 2)
		location.redirect = tokens[1];
}

void Config::parseAllowedMethods(const std::string& line, std::vector<std::string>& methods) {
	std::vector<std::string> tokens = splitLine(line);
	methods.clear();
	
	for (size_t i = 1; i < tokens.size(); ++i) {
		std::string method = tokens[i];
		if (method == "GET" || method == "POST" || method == "DELETE")
			methods.push_back(method);
	}
	
	if (methods.empty())
		methods.push_back("GET");
}

void Config::parseErrorPage(const std::string& line, std::map<int, std::string>& error_pages) {
	std::vector<std::string> tokens = splitLine(line);
	
	if (tokens.size() >= 3) {
		int error_code = std::atoi(tokens[1].c_str());
		std::string error_page = tokens[2];
		error_pages[error_code] = error_page;
	}
}

const ServerConfig* Config::findServerConfig(const std::string& host, int port, const std::string& server_name) const {
	for (std::vector<ServerConfig>::const_iterator it = _servers.begin(); it != _servers.end(); ++it) {
		if (it->host == host && it->port == port) {
			if (server_name.empty() || it->server_name == server_name)
				return &(*it);
		}
	}
	
	for (std::vector<ServerConfig>::const_iterator it = _servers.begin(); it != _servers.end(); ++it) {
		if (it->port == port)
			return &(*it);
	}
	
	return _servers.empty() ? NULL : &_servers[0];
}

const LocationConfig* Config::findLocationConfig(const ServerConfig& server, const std::string& uri) const {
	const LocationConfig* best_match = NULL;
	size_t best_match_length = 0;
	
	for (std::vector<LocationConfig>::const_iterator it = server.locations.begin(); it != server.locations.end(); ++it) {
		if (uri.find(it->path) == 0) {
			if (it->path.length() > best_match_length) {
				best_match = &(*it);
				best_match_length = it->path.length();
			}
		}
	}
	
	return best_match;
}

bool Config::validateConfig() const {
	for (std::vector<ServerConfig>::const_iterator it = _servers.begin(); it != _servers.end(); ++it) {
		if (it->port <= 0 || it->port > 65535) {
			log_error("invalid port number " + int_to_string(it->port));
			return false;
		}
		
		if (it->root.empty()) {
			log_error("empty root directory");
			return false;
		}
		
		if (it->client_max_body_size == 0) {
			log_error("invalid client_max_body_size");
			return false;
		}
	}
	
	return true;
}

bool Config::parseConfigFile(const std::string& filename) {
	std::ifstream file(filename.c_str());
	if (!file.is_open()) {
		log_error("cant open config file: " + filename);
		return false;
	}

	std::string line;
	ServerConfig current_server;
	bool in_server_block = false;
	int line_number = 0;

	while (std::getline(file, line)) {
		line_number++;
		if (shouldSkipLine(line))
			continue;
		if (isServerStart(line)) {
			if (!handleServerStart(in_server_block, current_server, line_number, file))
				return false;
			continue;
		}
		if (isServerEnd(line)) {
			if (!handleServerEnd(in_server_block, current_server, line_number, file))
				return false;
			continue;
		}
		if (!handleDirective(in_server_block, line, current_server, line_number, file))
			return false;
	}
	file.close();
	return finalizeConfig(in_server_block);
}
