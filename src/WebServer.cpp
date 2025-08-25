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
    HttpRequest request;
    if (!request.parseRequest(client_buffer)) {
        LOG_DEBUG("Request parsing failed, waiting for more data from client " + size_t_to_string(client_fd));
        return;
    }
    if (request.isChunked()) {
        if (request.needsMoreChunks()) {
            LOG_DEBUG("Chunked request needs more data from client " + size_t_to_string(client_fd));
            return;
        }
    } else {
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
        std::string headers = client_buffer.substr(0, header_end_pos);
        size_t content_length = getContentLength(headers);
        size_t expected_total_size = header_end_pos + content_length;
        size_t current_size = client_buffer.length();
        if (current_size < expected_total_size) {
            LOG_DEBUG("Waiting for " + size_t_to_string(expected_total_size - current_size) + " more bytes from client " + size_t_to_string(client_fd));
            return;
        }
        if (current_size > expected_total_size) {
            client_buffer = client_buffer.substr(0, expected_total_size);
            if (!request.parseRequest(client_buffer)) {
                log_error("HTTP request parse error after trimming for client " + size_t_to_string(client_fd));
                std::string error_response = generateErrorResponse(400, "Bad Request");
                queueResponse(client_fd, error_response);
                _client_buffers.erase(client_fd);
                return;
            }
        }
    }

    LOG_DEBUG("Complete HTTP request received from client " + size_t_to_string(client_fd));
    const ServerConfig* server_config = _config->findServerConfig("127.0.0.1", 8080, "");
    if (server_config && request.getBody().length() > server_config->client_max_body_size) {
        std::string error_response = generateErrorResponse(413, "Request Entity Too Large");
        queueResponse(client_fd, error_response);
        _client_buffers.erase(client_fd);
        return;
    }
    
    LOG_DEBUG("Request parsed successfully");
    std::string response = generateResponse(request);
    LOG_DEBUG("Generated response for client " + size_t_to_string(client_fd));
    
    queueResponse(client_fd, response);
    _client_buffers.erase(client_fd);
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
