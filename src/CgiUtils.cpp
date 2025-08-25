
#include "Cgi.hpp"
#include "utils.hpp"

std::vector<std::string> CgiHandler::setupEnvironment(const HttpRequest& request, 
                                                     const std::string& script_path) const {
    std::vector<std::string> env_vars;

	// basic envs
    env_vars.push_back("REQUEST_METHOD=" + request.methodToString());
    env_vars.push_back("SERVER_PROTOCOL=HTTP/1.1");
    env_vars.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env_vars.push_back("SERVER_SOFTWARE=Webserv/1.0");
    
	// for server info
    std::string host = request.getHeader("Host");
    if (!host.empty()) {
        size_t colon_pos = host.find(':');
        if (colon_pos != std::string::npos) {
            host = host.substr(0, colon_pos);
        }
        env_vars.push_back("SERVER_NAME=" + host);
    } else {
        env_vars.push_back("SERVER_NAME=localhost");
    }
    env_vars.push_back("SERVER_PORT=8080");
    
	// for script info
    std::string uri = request.getUri();
    size_t query_pos = uri.find('?');
    std::string script_uri = (query_pos != std::string::npos) ? uri.substr(0, query_pos) : uri;
    
    env_vars.push_back("SCRIPT_NAME=" + script_uri);
    env_vars.push_back("SCRIPT_FILENAME=" + script_path);
    
    // for client info
    env_vars.push_back("REMOTE_ADDR=127.0.0.1");
    env_vars.push_back("REMOTE_HOST=");
    
    // request info
    if (query_pos != std::string::npos && query_pos + 1 < uri.length()) {
        env_vars.push_back("QUERY_STRING=" + uri.substr(query_pos + 1));
    } else {
        env_vars.push_back("QUERY_STRING=");
    }
    
    // path info
    env_vars.push_back("PATH_INFO=");
    env_vars.push_back("PATH_TRANSLATED=");
    
    // for post data
    if (request.getMethod() == POST) {
        env_vars.push_back("CONTENT_LENGTH=" + size_t_to_string(request.getBody().length()));
        std::string content_type = request.getHeader("Content-Type");
        if (!content_type.empty()) {
            env_vars.push_back("CONTENT_TYPE=" + content_type);
        } else {
            env_vars.push_back("CONTENT_TYPE=application/x-www-form-urlencoded");
        }
    }
    
    // adds http headers
    const std::map<std::string, std::string>& headers = request.getHeaders();
    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it) {
        
        std::string header_name = it->first;
        std::string header_value = it->second;
        
        // change to cgi format: HTTP_HEADER_NAME
        std::string cgi_name = "HTTP_";
        for (size_t i = 0; i < header_name.length(); ++i) {
            char c = header_name[i];
            if (c == '-') {
                cgi_name += '_';
            } else {
                cgi_name += std::toupper(c);
            }
        }
        
        env_vars.push_back(cgi_name + "=" + header_value);
    }
    
    // additional stuff (maybe helpful later)
    env_vars.push_back("DOCUMENT_ROOT=./www");
    env_vars.push_back("REQUEST_URI=" + request.getUri());
    env_vars.push_back("REDIRECT_STATUS=200");
    env_vars.push_back("HTTPS=");	// empty (no https support)
    env_vars.push_back("SERVER_ADMIN=webmaster@localhost"); 
    
    return env_vars;
}

std::string CgiHandler::getInterpreter(const std::string& script_path, const std::map<std::string, std::string>& interpreters) const {
	for (std::map<std::string, std::string>::const_iterator it = interpreters.begin();
		 it != interpreters.end(); ++it) {
		if (script_path.find(it->first) != std::string::npos)
			return it->second;
	}
	return "";
}

bool CgiHandler::isExecutable(const std::string& path) const {
	return access(path.c_str(), X_OK) == 0;
}

std::string CgiHandler::getScriptPath(const std::string& uri) const {
	if (uri.find("/cgi-bin/") == 0)
		return _cgi_bin_path + uri.substr(8); // remove "/cgi-bin" prefix
	return "./www" + uri; // default web root
}

std::string CgiHandler::generateErrorResponse(int status_code,
		const std::string& status_text) const {
	std::ostringstream response;
	std::ostringstream body;
	
	body << "<html><body><h1>" << status_code << " " << status_text << "</h1></body></html>";
	std::string body_str = body.str();
	
	response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
	response << "Content-Type: text/html\r\n";
	response << "Content-Length: " << body_str.length() << "\r\n";
	response << "Connection: close\r\n";
	response << "Server: Webserv/1.0\r\n";
	response << "\r\n";
	response << body_str;
	
	return response.str();
}