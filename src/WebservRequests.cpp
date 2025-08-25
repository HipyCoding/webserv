
#include "WebServer.hpp"
#include "Config.hpp"
#include "HttpRequest.hpp"
#include "utils.hpp"
#include <sstream>

std::string WebServer::generateResponse(const HttpRequest& request) {
    std::string method = request.methodToString();
    std::string uri = request.getUri();
    
    std::cout << "Processing: " << method << " " << uri << std::endl;
    
	if (request.getMethod() == GET) {
		return handleGetRequest(request);
	} else if (request.getMethod() == POST) {
		return handlePostRequest(request);
	} else if (request.getMethod() == DELETE) {
		return handleDeleteRequest(request);
	} else if (request.getMethod() == UNKNOWN) {
		return generateErrorResponse(501, "Not Implemented");
	} else {
		return generateErrorResponse(405, "Method Not Allowed");
	}
}

std::string WebServer::handleGetRequest(const HttpRequest& request) {
	std::string uri = request.getUri();
	std::string host = request.getHeader("Host");

	const ServerConfig* server_config = _config->findServerConfig("127.0.0.1", 8080, "");
	if (!server_config) {
		log_error("no server config found");
		return generateErrorResponse(500, "Internal Server Error");
	}

	const LocationConfig* location_config = _config->findLocationConfig(*server_config, uri);
	
	if (location_config){
		std::string redirect_response = handleRedirect(location_config);
		if (!redirect_response.empty())
			return redirect_response;
	}


	if (location_config) {	// checkin method permissions
		bool method_allowed = false;
		for (size_t i = 0; i < location_config->allowed_methods.size(); ++i) {
			if (location_config->allowed_methods[i] == "GET") {
				method_allowed = true;
				break;
			}
		}
		if (!method_allowed)
			return generateErrorResponse(405, "Method Not Allowed");
	}

	if (_cgi_handler && _cgi_handler->isCgiRequest(uri))
		return _cgi_handler->handleCgiRequest(request);

	std::string root = server_config->root;
	if (location_config && !location_config->root.empty())
		root = location_config->root;
	
	std::string file_path = getFilePathWithRoot(uri, root);
    
    if (!fileExists(file_path)) {
        return generateErrorResponse(404, "Not Found");
    }
    
    if (isDirectory(file_path)) {
        return handleDirectoryRequest(file_path, uri, location_config);
    }
    
    if (access(file_path.c_str(), R_OK) != 0) {
        return generateErrorResponse(403, "Forbidden");
    }
    
    std::string content = readFile(file_path);
    if (content.empty() && fileExists(file_path)) {
        return generateErrorResponse(500, "Internal Server Error");
    }
    
    return generateSuccessResponse(content, getContentType(file_path));
}

std::string WebServer::handlePostRequest(const HttpRequest& request) {
	std::string uri = request.getUri();

	const ServerConfig* server_config = _config->findServerConfig("127.0.0.1", 8080, "");
	if (!server_config)
		return generateErrorResponse(500, "Internal Server Error");

	const LocationConfig* location_config = _config->findLocationConfig(*server_config, uri);

	if (location_config) {
		std::string redirect_response = handleRedirect(location_config);
		if (!redirect_response.empty())
			return redirect_response;
	}
	if (location_config) {
		bool method_allowed = false;
		for (size_t i = 0; i < location_config->allowed_methods.size(); ++i) {
			if (location_config->allowed_methods[i] == "POST") {
				method_allowed = true;
				break;
			}
		}
		if (!method_allowed)
			return generateErrorResponse(405, "Method Not Allowed");
	}

	size_t max_body_size = server_config->client_max_body_size;
	if (request.getBody().length() > max_body_size)
		return generateErrorResponse(413, "Request Entity Too Large");

	if (_cgi_handler && _cgi_handler->isCgiRequest(uri))
		return _cgi_handler->handleCgiRequest(request);

	if (location_config && !location_config->upload_path.empty())
		return handleFileUploadToLocation(request, location_config);
	
	std::string body = request.getBody();
	log_info("POST request for: " + uri + " (body: " + size_t_to_string(body.length()) + " bytes)");

    if (uri.find("/upload") == 0)
        return handleFileUpload(request);
    
    if (uri.find("/form") == 0) 
        return handleFormSubmission(request);
    
    return handlePostEcho(request);
}

std::string WebServer::handleDeleteRequest(const HttpRequest& request) {
	std::string uri = request.getUri();
	
	const ServerConfig* server_config = _config->findServerConfig("127.0.0.1", 8080, "");
	if (!server_config)
		return generateErrorResponse(500, "Internal Server Error");

	const LocationConfig* location_config = _config->findLocationConfig(*server_config, uri);

	if (location_config) {
		std::string redirect_response = handleRedirect(location_config);
		if(!redirect_response.empty())
			return redirect_response;
	}

	if (location_config) {
		bool method_allowed = false;
		for (size_t i = 0; i < location_config->allowed_methods.size(); ++i) {
			if (location_config->allowed_methods[i] == "DELETE") {
				method_allowed = true;
				break;
			}
		}
		if (!method_allowed)
			return generateErrorResponse(405, "Method Not Allowed");
	}

	std::string root = server_config->root;
	if (location_config && !location_config->root.empty())
		root = location_config->root;
	
	std::string file_path = getFilePathWithRoot(uri, root);
	
	log_info("DELETE request for: " + file_path);
	
	if (!fileExists(file_path))
		return generateErrorResponse(404, "Not Found");
	
	if (isDirectory(file_path))
		return generateErrorResponse(403, "Forbidden - Cannot delete directory");
	
	std::string parent_dir = file_path.substr(0, file_path.find_last_of('/'));
	if (access(parent_dir.c_str(), W_OK) != 0)
		return generateErrorResponse(403, "Forbidden - No write permission");
	
	if (unlink(file_path.c_str()) == 0) {
		std::ostringstream response;
		response << "HTTP/1.1 200 OK\r\n";
		response << "Content-Type: text/html\r\n";
		response << "Content-Length: 47\r\n";
		response << "Connection: close\r\n";
		response << "Server: Webserv/1.0\r\n";
		response << "\r\n";
		response << "<html><body><h1>File deleted</h1></body></html>";
		return response.str();
	} else {
		return generateErrorResponse(500, "Internal Server Error - Delete failed");
	}
}

std::string WebServer::handleRedirect(const LocationConfig* location) {
    if (!location || location->redirect.empty()) {
        return "";
    }
    
    std::string redirect_url = location->redirect;
    
    int status_code = 301;
    if (redirect_url.find("301 ") == 0) {
        status_code = 301;
        redirect_url = redirect_url.substr(4);
    } else if (redirect_url.find("302 ") == 0) {
        status_code = 302;
        redirect_url = redirect_url.substr(4);
    } else if (redirect_url.find("307 ") == 0) {
        status_code = 307;
        redirect_url = redirect_url.substr(4);
    }
    
    std::string status_text;
    switch (status_code) {
        case 301: status_text = "Moved Permanently"; break;
        case 302: status_text = "Found"; break;
        case 307: status_text = "Temporary Redirect"; break;
        default: status_text = "Moved Permanently"; status_code = 301; break;
    }
    
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Location: " << redirect_url << "\r\n";
    response << "Content-Length: 0\r\n";
    response << "Connection: close\r\n";
    response << "Server: Webserv/1.0\r\n";
    response << "\r\n";
    
    return response.str();
}

std::string WebServer::handleFileUpload(const HttpRequest& request) {
    std::string body = request.getBody();
    std::string upload_dir = "./www/uploads";
    
    // Create upload directory if it doesn't exist
    mkdir(upload_dir.c_str(), 0755);

    std::ostringstream filename;
    filename << "upload_" << time(NULL) << ".txt";
    
    std::string file_path = upload_dir + "/" + filename.str();
    
    std::ofstream outfile(file_path.c_str(), std::ios::binary);
    if (!outfile.is_open()) {
        return generateErrorResponse(500, "Internal Server Error");
    }
    
    outfile << body;
    outfile.close();

    std::ostringstream html_content;
    html_content << "<html><body><h1>File uploaded successfully</h1>";
    html_content << "<p>Saved as: " << filename.str() << "</p>";
    html_content << "<p>Size: " << body.length() << " bytes</p>";
    if (request.isChunked()) {
        html_content << "<p>Transfer: Chunked encoding</p>";
    }
    html_content << "</body></html>";
    
    std::ostringstream response;
    response << "HTTP/1.1 201 Created\r\n";
    response << "Content-Type: text/html\r\n";
    response << "Content-Length: " << html_content.str().length() << "\r\n";
    response << "Location: /uploads/" << filename.str() << "\r\n";
    response << "Connection: close\r\n";
    response << "Server: Webserv/1.0\r\n";
    response << "\r\n";
    response << html_content.str();

    return response.str();
}

std::string WebServer::handleFileUploadToLocation(const HttpRequest& request, const LocationConfig* location_config) {
	std::string body = request.getBody();
	std::string upload_dir = location_config->upload_path;
	
	std::ostringstream filename;
	filename << "upload_" << time(NULL) << ".txt";
	
	std::string file_path = upload_dir + "/" + filename.str();
	
	std::ofstream outfile(file_path.c_str());
	if (!outfile.is_open())
		return generateErrorResponse(500, "Internal Server Error - Cannot create file");
	
	outfile << body;
	outfile.close();
	
	std::ostringstream html;
	html << "<html><body><h1>File uploaded successfully</h1>";
	html << "<p>Saved to: " << location_config->upload_path << "/" << filename.str() << "</p>";
	html << "<p><a href='/'>Back to home</a></p>";
	html << "</body></html>";
	
	return generateSuccessResponse(html.str(), "text/html");
}

std::string WebServer::handleFormSubmission(const HttpRequest& request) {
    std::string body = request.getBody();
    
    std::cout << "Form data received: " << body << std::endl;
    
    std::ostringstream html;
    html << "<html><body>";
    html << "<h1>Form Submission Received</h1>";
    html << "<p>Data: " << body << "</p>";
    html << "<a href='/'>Back to home</a>";
    html << "</body></html>";
    
    return generateSuccessResponse(html.str(), "text/html");
}

std::string WebServer::handlePostEcho(const HttpRequest& request) {
    std::string body = request.getBody();
    
    std::ostringstream html;
    html << "<html><body>";
    html << "<h1>POST Request Received</h1>";
    html << "<p>URI: " << request.getUri() << "</p>";
    html << "<p>Body length: " << body.length() << " bytes</p>";
    html << "<pre>" << body << "</pre>";
    html << "</body></html>";
    
    return generateSuccessResponse(html.str(), "text/html");
}

std::string WebServer::handleDirectoryRequest(const std::string& dir_path, const std::string& uri,
			const LocationConfig* location_config) {
	std::vector<std::string> index_files;
	
	if (location_config && !location_config->index.empty())
		index_files.push_back(location_config->index);
	else {
		index_files.push_back("index.html");
		index_files.push_back("index.htm");
	}
	
	for (size_t i = 0; i < index_files.size(); ++i) {
		std::string index_path = dir_path;
		if (index_path[index_path.length() - 1] != '/')
			index_path += "/";
		index_path += index_files[i];
		
		if (fileExists(index_path) && access(index_path.c_str(), R_OK) == 0) {
			std::string content = readFile(index_path);
			if (!content.empty())
				return generateSuccessResponse(content, "text/html");
		}
	}

	if (location_config && location_config->autoindex)	// autoindex check
		return generateDirectoryListing(dir_path, uri);
	
	return generateErrorResponse(404, "Not Found");
}


std::string WebServer::generateDirectoryListing(const std::string& dir_path, const std::string& uri) {
	std::ostringstream html;
	
	html << "<html><head><title>Index of " << uri << "</title></head><body>";
	html << "<h1>Index of " << uri << "</h1><hr>";
	html << "<pre>";
	
	if (uri != "/")	// add parent directory link if not root
		html << "<a href=\"../\">../</a>\n";
	
	DIR* dir = opendir(dir_path.c_str());
	if (dir != NULL) {
		struct dirent* entry;
		while ((entry = readdir(dir)) != NULL) {
			std::string name = entry->d_name;
			if (name == "." || (name[0] == '.' && name != "..")) //skipps hidden files and current dir
				continue;
				
			html << "<a href=\"" << name << "\">" << name << "</a>\n";
		}
		closedir(dir);
	} else {
		html << "<i>Directory listing unavailable</i>\n";
	}
	
	html << "</pre><hr></body></html>";
	
	return generateSuccessResponse(html.str(), "text/html");
}