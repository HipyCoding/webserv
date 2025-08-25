
#include "WebServer.hpp"
#include "Config.hpp"
#include "HttpRequest.hpp"
#include "utils.hpp"
#include <sstream>


std::string WebServer::readFile(const std::string& file_path) {
	std::ifstream file(file_path.c_str(), std::ios::binary);
	if (!file.is_open()) {
		log_error("Cannot open file: " + file_path);
		return "";
	}
	
	file.seekg(0, std::ios::end);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	
	std::string buffer(size, '\0');
	if (!file.read(&buffer[0], size)) {
		log_error("Failed to read file: " + file_path);
		return "";
	}
	
	file.close();
	return buffer;
}

bool WebServer::isDirectory(const std::string& path) {
	struct stat buffer;
	if (stat(path.c_str(), &buffer) != 0) {
		return false;
	}
	return S_ISDIR(buffer.st_mode);
}

size_t WebServer::getContentLength(const std::string& headers) {
	std::string headers_lower = headers;
	for (size_t i = 0; i < headers_lower.length(); ++i) {
		headers_lower[i] = std::tolower(headers_lower[i]);
	}
	
	size_t pos = headers_lower.find("content-length:");
	if (pos == std::string::npos) {
		return 0;
	}

	pos += 15;
	while (pos < headers.length() && (headers[pos] == ' ' || headers[pos] == '\t')) {
		pos++;
	}
	
	size_t end_pos = headers.find('\r', pos);
	if (end_pos == std::string::npos) {
		end_pos = headers.find('\n', pos);
	}
	if (end_pos == std::string::npos) {
		end_pos = headers.length();
	}
	
	std::string value = headers.substr(pos, end_pos - pos);
	
	size_t content_length = 0;
	std::istringstream iss(value);
	iss >> content_length;
	
	return content_length;
}

std::string WebServer::getFilePathWithRoot(const std::string& uri, const std::string& root) {
	if (uri == "/")
		return root + "/index.html";
	
	std::string clean_uri = uri;
	size_t pos = 0;
	while ((pos = clean_uri.find("../", pos)) != std::string::npos)
		clean_uri.erase(pos, 3);

	pos = 0;
	while ((pos = clean_uri.find("//", pos)) != std::string::npos)
		clean_uri.erase(pos, 1);
	
	return root + clean_uri;
}

std::string WebServer::getContentType(const std::string& file_path) {
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "application/octet-stream";
    }
    
    std::string extension = file_path.substr(dot_pos);
    
    for (size_t i = 0; i < extension.length(); ++i) {
        extension[i] = std::tolower(extension[i]);
    }
    
    if (extension == ".html" || extension == ".htm") return "text/html";
    if (extension == ".css") return "text/css";
    if (extension == ".js") return "application/javascript";
    if (extension == ".json") return "application/json";
    if (extension == ".txt") return "text/plain";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".ico") return "image/x-icon";
    
    return "application/octet-stream";
}

std::string WebServer::getStatusMessage(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 307: return "Temporary Redirect";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default: return "Unknown Status";
    }
}

std::string WebServer::generateSuccessResponse(const std::string& content, const std::string& content_type) {
    std::ostringstream response;
    
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << content.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "Server: Webserv/1.0\r\n";
    response << "\r\n";
    response << content;
    
    return response.str();
}

std::string WebServer::generateErrorResponse(int status_code, const std::string& status_text) {
	std::string body;
	(void) status_text;
	// try to get custom error page from config
	const ServerConfig* server_config = _config->findServerConfig("127.0.0.1", 8080, "");
	if (server_config) {
		std::map<int, std::string>::const_iterator it = server_config->error_pages.find(status_code);
		if (it != server_config->error_pages.end()) {
			std::string error_file_path = server_config->root + it->second;
			std::string custom_content = readFile(error_file_path);
			if (!custom_content.empty()) {
				body = custom_content;
			}
		}
	}
	
	// if no page, use default
	if (body.empty()) {
		body = "<html><body>";
		body += "<h1>" + size_t_to_string(status_code) + " " + getStatusMessage(status_code) + "</h1>";
		body += "</body></html>";
	}
	
	std::string response = "HTTP/1.1 " + size_t_to_string(status_code) + " " + getStatusMessage(status_code) + "\r\n";
	response += "Content-Type: text/html\r\n";
	response += "Content-Length: " + size_t_to_string(body.length()) + "\r\n";
	response += "Connection: close\r\n";
	response += "\r\n";
	response += body;
	
	return response;
}