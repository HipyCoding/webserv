#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cctype>
#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>

#include "WebServer.hpp"
#include "Config.hpp"
#include "utils.hpp"
#include "Cgi.hpp"

class   Config;
struct  LocationConfig;
class   HttpRequest;
class   CgiHandler;

class WebServer {
	private:
    // classes
    CgiHandler* _cgi_handler;
    Config* _config;

    // poll functionality
	std::vector<struct pollfd> _poll_fds;
	std::vector<int> _server_sockets;
	std::map<int, std::string> _client_buffers; // incoming data buffers
	std::map<int, std::string> _client_write_buffers; // outgoing buffer
	std::map<int, bool> _clients_ready_to_write;     // check clients with queued responses
	std::map<int, time_t> _client_timeouts;
	std::map<int, HttpRequest*> _client_requests;
	static const int REQUEST_TIMEOUT = 30;
    // sockets
    int createServerSocket(const std::string& host, int port);

    // connection handling
    void handleNewConnection(int server_fd);
	void handleClientData(int client_fd, int poll_index);	// processes incoming data from client (called when POLLIN ready)
	void handleClientWrite(int client_fd, int poll_index);	// sends queued response data to client (called when POLLOUT ready)
	void queueResponse(int client_fd, const std::string& response);
	void cleanupClient(int client_fd, int poll_index);
	void checkClientTimeouts();

    // http request/resopnse
    std::string generateResponse(const HttpRequest& request);
	std::string handleGetRequest(const HttpRequest& request);
	std::string handlePostRequest(const HttpRequest& request);
	std::string handleDeleteRequest(const HttpRequest& request);
	std::string handleRedirect(const LocationConfig* location);

    // special requests
    std::string handleFileUpload(const HttpRequest& request);
	std::string handleMultipartUpload(const HttpRequest& request);
	std::string handleSimpleUpload(const HttpRequest& request);
	std::string handleFileUploadToLocation(const HttpRequest& request, const LocationConfig* location_config);
	std::string handleFormSubmission(const HttpRequest& request);
	std::string handlePostEcho(const HttpRequest& request);
	// std::string handleDirectoryRequest(const std::string& dir_path, const std::string& uri);
	std::string handleDirectoryRequest(const std::string& dir_path, const std::string& uri, const LocationConfig* location_config);
	std::string generateDirectoryListing(const std::string& dir_path, const std::string& uri);

    // server utilities
    std::string generateSuccessResponse(const std::string& content, const std::string& content_type);
	std::string getStatusMessage(int code);
	std::string getContentType(const std::string& file_path);
	// std::string getFilePath(const std::string& uri);
	std::string getFilePathWithRoot(const std::string& uri, const std::string& root);
	std::string getFileExtension(const std::string& filename);
	std::string getExtensionFromContentType(const std::string& content_type);
	size_t getContentLength(const std::string& headers);
	bool isDirectory(const std::string& path);
	std::string readFile(const std::string& file_path);

public:
    WebServer();
    ~WebServer();
    
    bool initialize(const std::string& config_file);
	std::string generateErrorResponse(int status_code, const std::string& status_text);
    void run();
    void cleanup();
};

#endif