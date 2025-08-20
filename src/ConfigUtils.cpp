#include "Config.hpp"
#include "utils.hpp"

std::string Config::trim(const std::string& str) {
	size_t start = str.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = str.find_last_not_of(" \t\r\n");
	return str.substr(start, end - start + 1);
}

bool Config::shouldSkipLine(const std::string& line) {
	std::string trimmed = trim(line);
	return trimmed.empty() || trimmed[0] == '#';
}

bool Config::isServerStart(const std::string& line) {
	return trim(line) == "server {";
}

bool Config::isServerEnd(const std::string& line) {
	return trim(line) == "}";
}

bool Config::isLocationStart(const std::string& line) {
	std::string trimmed = trim(line);
	return trimmed.find("location") == 0 && trimmed.find("{") != std::string::npos;
}

bool Config::isLocationEnd(const std::string& line) {
	return trim(line) == "}";
}

std::string Config::extractLocationPath(const std::string& line) {
	size_t start = line.find("location");
	if (start == std::string::npos)
		return "/";
	
	start += 8; // length of "location"
	size_t end = line.find("{");
	if (end == std::string::npos)
		return "/";
	
	std::string path = line.substr(start, end - start);
	return trim(path);
}

std::vector<std::string> Config::splitLine(const std::string& line) {
	std::vector<std::string> tokens;
	std::istringstream iss(line);
	std::string token;
	
	while (iss >> token) {
		// remove semicolon from last token
		if (!token.empty() && token[token.length() - 1] == ';')
			token = token.substr(0, token.length() - 1);
		tokens.push_back(token);
	}
	
	return tokens;
}

bool Config::handleDirective(bool in_server_block, const std::string& line, ServerConfig& current_server, int line_number, std::ifstream& file) {
	if (isLocationStart(line)) {
		if (!in_server_block) {
			log_error("location directive outside server block (line " + int_to_string(line_number) + ")");
			return false;
		}
		std::string location_path = extractLocationPath(line);
		return parseLocationBlock(file, current_server, location_path, line_number);
	}
	
	if (!in_server_block) {
		log_error("directive outside server block (line " + int_to_string(line_number) + ")");
		return false;
	}
	parseSimpleDirective(line, current_server);
	return true;
}

bool Config::finalizeConfig(bool in_server_block) {
	if (in_server_block) {
		log_error("unclosed server block");
		return false;
	}
		
	if (_servers.empty()) {
		log_info("no servers configured, using default");
		setDefaultConfig();
	}
		
	return validateConfig(); // validation better now than just returning true
}

bool Config::handleServerStart(bool& in_server_block, ServerConfig& current_server,
		int line_number, std::ifstream& file) {
	if (in_server_block) {
		log_error("nested server blocks not allowed (line " + int_to_string(line_number) + ")");
		file.close();
		return false;
	}
	in_server_block = true;
	current_server = getDefaultServerConfig();
	log_debug("found server block");
	return true;
}

bool Config::handleServerEnd(bool& in_server_block, ServerConfig& current_server,
		int line_number, std::ifstream& file) {
	if (!in_server_block) {
		log_error("unexpected '}' (line " + int_to_string(line_number) + ")");
		file.close();
		return false;
	}
	in_server_block = false;
	_servers.push_back(current_server);
	log_info("parsed server: " + current_server.host + ":" + int_to_string(current_server.port));
	return true;
}
