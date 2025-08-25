/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   WebServer.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: christian <christian@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/03 07:04:49 by christian         #+#    #+#             */
/*   Updated: 2025/08/24 17:24:01 by christian        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "WebServer.hpp"
#include "Config.hpp"
#include "HttpRequest.hpp"
#include "utils.hpp"
#include <sstream>

WebServer::WebServer() : _config(NULL) {
	_cgi_handler = new CgiHandler();  // tte
}

WebServer::~WebServer() {
	cleanup();
	delete _config;
	delete _cgi_handler;  // tte
}

bool WebServer::initialize(const std::string& config_file) {
	_config = new Config();
	
	if (!_config->parseConfigFile(config_file)) {
		log_info("Using default configuration");
		_config->setDefaultConfig();
	}
	
	const std::vector<ServerConfig>& servers = _config->getServers();
	
	for (size_t i = 0; i < servers.size(); ++i) {
		int server_fd = createServerSocket(servers[i].host, servers[i].port);
		if (server_fd == -1) {
			log_error("Failed to create server socket for " + servers[i].host + ":" + size_t_to_string(servers[i].port));
			return false;
		}
		
		_server_sockets.push_back(server_fd);
		
		struct pollfd pfd;
		pfd.fd = server_fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		_poll_fds.push_back(pfd);
		
		log_info("Server listening on " + servers[i].host + ":" + size_t_to_string(servers[i].port));
	}
	
	return true;
}

int WebServer::createServerSocket(const std::string& host, int port) {
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		log_error("Failed to create socket");
		return -1;
	}
	
	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
		log_error("Failed to set socket options");
		close(server_fd);
		return -1;
	}
	
	if (fcntl(server_fd, F_SETFL, O_NONBLOCK) == -1) {
		log_error("Failed to set socket to non-blocking");
		close(server_fd);
		return -1;
	}
	
	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(host.c_str());
	
	if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		log_error("Failed to bind socket to " + host + ":" + size_t_to_string(port));
		close(server_fd);
		return -1;
	}
	
	if (listen(server_fd, 128) == -1) {
		log_error("Failed to listen on socket");
		close(server_fd);
		return -1;
	}
	
	return server_fd;
}
void WebServer::run() {
	log_info("Server entering main loop...");
	while (true) {
		checkClientTimeouts();
		// set up poll events for both read and write
		for (size_t i = 0; i < _poll_fds.size(); ++i) {
			_poll_fds[i].events = POLLIN;
			
			// check if client needs to write
			if (_clients_ready_to_write.find(_poll_fds[i].fd) != _clients_ready_to_write.end() &&
				_clients_ready_to_write[_poll_fds[i].fd]) {
				_poll_fds[i].events |= POLLOUT;
			}
		}
		
		LOG_DEBUG("Calling poll with " + size_t_to_string(_poll_fds.size()) + " file descriptors...");
		int poll_count = poll(&_poll_fds[0], _poll_fds.size(), 2000);
		LOG_DEBUG("Poll returned: " + size_t_to_string(poll_count));
		
		if (poll_count == -1) {
			log_error("Poll error");
			break;
		}
		
		for (size_t i = 0; i < _poll_fds.size(); ++i) {
			if (_poll_fds[i].revents & POLLIN) {
				LOG_DEBUG("Activity on fd " + size_t_to_string(_poll_fds[i].fd));
				bool is_server = false;
				for (size_t j = 0; j < _server_sockets.size(); ++j) {
					if (_poll_fds[i].fd == _server_sockets[j]) {
						LOG_DEBUG("New connection on server socket " + size_t_to_string(_poll_fds[i].fd));
						handleNewConnection(_poll_fds[i].fd);
						is_server = true;
						break;
					}
				}
				
				if (!is_server) {
					LOG_DEBUG("Client data on fd " + size_t_to_string(_poll_fds[i].fd));
					handleClientData(_poll_fds[i].fd, i);
				}
			}
			
			if (_poll_fds[i].revents & POLLOUT) { // write events
				handleClientWrite(_poll_fds[i].fd, i);
			}
		}
	}
}

void WebServer::checkClientTimeouts() {
	time_t current_time = time(NULL);
	std::vector<int> timed_out_clients;

	for (std::map <int, time_t>::iterator it = _client_timeouts.begin();
		it != _client_timeouts.end(); ++it) {
			if (current_time - it->second > REQUEST_TIMEOUT) {
				timed_out_clients.push_back(it->first);
			}
		}
	for (size_t i = 0; i < timed_out_clients.size(); i++) {
		for(size_t j = 0; j < _poll_fds.size(); ++j) {
			if (_poll_fds[j].fd == timed_out_clients[i]) {
				log_info("Client " + size_t_to_string(timed_out_clients[i]) + " timed out");
				cleanupClient(timed_out_clients[i], j);
				break ;
			}
		}
	}
}

void WebServer::handleNewConnection(int server_fd) {
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	LOG_DEBUG("Accepting connection on server_fd " + size_t_to_string(server_fd));

	int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
	if (client_fd == -1) {
		log_error("Accept failed");
		return;
	}

	log_info("New client connected: fd = " + size_t_to_string(client_fd));
	
	if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1) {
		log_error("fcntl failed");
		close(client_fd);
		return;
	}
	
	struct pollfd pfd;
	pfd.fd = client_fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_poll_fds.push_back(pfd);
	
	_client_buffers[client_fd] = "";
	_client_timeouts[client_fd] = time(NULL);

	LOG_DEBUG("Client " + size_t_to_string(client_fd) + " added to poll list");
}

void WebServer::handleClientWrite(int client_fd, int poll_index) {
	if (_client_write_buffers.find(client_fd) == _client_write_buffers.end())
		return;
		
	std::string& response = _client_write_buffers[client_fd];
	if (response.empty()) {
		cleanupClient(client_fd, poll_index);
		return;
	}

	ssize_t bytes_sent = send(client_fd, response.c_str(), response.length(), MSG_DONTWAIT);
	
	if (bytes_sent <= 0) {
		log_error("send() failed for client " + size_t_to_string(client_fd));
		cleanupClient(client_fd, poll_index);
		return;
	}
	
	LOG_DEBUG("Sent " + size_t_to_string(bytes_sent) + " bytes to client " + size_t_to_string(client_fd));
	response.erase(0, bytes_sent);  // removes sent data
	
	if (response.empty())
		cleanupClient(client_fd, poll_index);
}

void WebServer::handleClientData(int client_fd, int poll_index) {
	LOG_DEBUG("Reading data from client " + size_t_to_string(client_fd));
	char buffer[8192];
	ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
	LOG_DEBUG("recv() returned " + size_t_to_string(bytes_read) + " bytes");

	if (bytes_read <= 0) {
		if (bytes_read == 0)
			log_info("Client " + size_t_to_string(client_fd) + " disconnected");
		else
			log_error("recv() failed for client " + size_t_to_string(client_fd));
		cleanupClient(client_fd, poll_index);
		return;
	}
	
	buffer[bytes_read] = '\0';
	_client_buffers[client_fd] += buffer;
	LOG_DEBUG("Buffer for client " + size_t_to_string(client_fd) + " now has " + size_t_to_string(_client_buffers[client_fd].length()) + " bytes");

	std::string& client_buffer = _client_buffers[client_fd];
	size_t header_end_pos = client_buffer.find("\r\n\r\n");
	if (header_end_pos == std::string::npos) {
		header_end_pos = client_buffer.find("\n\n");
		if (header_end_pos != std::string::npos)
			header_end_pos += 2;
	} else {
		header_end_pos += 4;
	}

	if (header_end_pos == std::string::npos) {
		LOG_DEBUG("Headers not complete yet, waiting for more data from client " + size_t_to_string(client_fd));
		return;
	}

	LOG_DEBUG("Headers complete, checking for request body...");
	std::string headers = client_buffer.substr(0, header_end_pos);
	size_t content_length = getContentLength(headers);
	LOG_DEBUG("Content-Length: " + size_t_to_string(content_length));

	size_t expected_total_size = header_end_pos + content_length;
	size_t current_size = client_buffer.length();
	LOG_DEBUG("Expected total size: " + size_t_to_string(expected_total_size) + ", current size: " + size_t_to_string(current_size));

	if (current_size >= expected_total_size) {
		LOG_DEBUG("Complete HTTP request received from client " + size_t_to_string(client_fd));
		
		if (current_size > expected_total_size) {
			client_buffer = client_buffer.substr(0, expected_total_size);
			LOG_DEBUG("Trimmed buffer to exact request size");
		}

		HttpRequest request;
		if (request.parseRequest(client_buffer)) {
			LOG_DEBUG("Request parsed successfully");
			std::string response = generateResponse(request);
			LOG_DEBUG("Generated response for client " + size_t_to_string(client_fd));
			
			queueResponse(client_fd, response);
			
		} else {
			log_error("HTTP request parse error for client " + size_t_to_string(client_fd));
			std::string error_response = generateErrorResponse(400, "Bad Request");
			queueResponse(client_fd, error_response);
		}
		
		_client_buffers.erase(client_fd);  // clears read buffer after processing
	} else {
		LOG_DEBUG("Waiting for " + size_t_to_string(expected_total_size - current_size) + " more bytes from client " + size_t_to_string(client_fd));
	}
}

void WebServer::cleanupClient(int client_fd, int poll_index) {
	close(client_fd);
	_poll_fds.erase(_poll_fds.begin() + poll_index);
	_client_buffers.erase(client_fd);
	_client_write_buffers.erase(client_fd);
	_clients_ready_to_write.erase(client_fd);
	_client_timeouts.erase(client_fd);
	log_info("Client " + size_t_to_string(client_fd) + " connection closed");
}

void WebServer::queueResponse(int client_fd, const std::string& response) {
	_client_write_buffers[client_fd] = response;
	_clients_ready_to_write[client_fd] = true;
	LOG_DEBUG("Queued " + size_t_to_string(response.length()) + " bytes for writing to client " + size_t_to_string(client_fd));
}

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


std::string WebServer::handleFileUpload(const HttpRequest& request) {
    std::string body = request.getBody();
    std::string upload_dir = "./www/uploads";
    
	mkdir(upload_dir.c_str(), 0755);

    std::ostringstream filename;
    filename << "upload_" << time(NULL) << ".txt";
    
    std::string file_path = upload_dir + "/" + filename.str();
    
    std::ofstream outfile(file_path.c_str());
    if (!outfile.is_open()) {
        return generateErrorResponse(500, "Internal Server Error");
    }
    
    outfile << body;
    outfile.close();

	std::ostringstream html_content;
	html_content << "<html><body><h1>File uploaded sucessfully<h1>";
	html_content << "<p> Saved as: " << filename.str() << "</p></body></html>";
    
    std::ostringstream response;
    response << "HTTP/1.1 201 Created\r\n";
    response << "Content-Type: text/html\r\n";
	response << "Content-Lenght: " << html_content.str().length() << "\r\n";
    response << "Location: /uploads/" << filename.str() << "\r\n";
    response << "Connection: close\r\n";
    response << "Server: Webserv/1.0\r\n";
	response << html_content.str();

    return response.str();
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

void WebServer::cleanup() {
	log_info("Cleaning up WebServer...");
	for (size_t i = 0; i < _server_sockets.size(); ++i) {
		close(_server_sockets[i]);
		LOG_DEBUG("Closed server socket " + size_t_to_string(_server_sockets[i]));
	}
	
	delete _config;
	_config = NULL;
	log_info("WebServer cleanup complete");
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