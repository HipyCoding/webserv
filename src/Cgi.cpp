#include "Cgi.hpp"
#include "utils.hpp"

CgiHandler::CgiHandler() : _cgi_bin_path("./www/cgi-bin"), _web_server(NULL) {
    initializeInterpreters();
}

CgiHandler::CgiHandler(const std::string& cgi_bin_path) : _cgi_bin_path(cgi_bin_path), _web_server(NULL) {
    initializeInterpreters();
}

CgiHandler::CgiHandler(const std::string& cgi_bin_path, WebServer* web_server) 
    : _cgi_bin_path(cgi_bin_path), _web_server(web_server) {
    initializeInterpreters();
}

void CgiHandler::setWebServer(WebServer* web_server) {
    _web_server = web_server;
}

CgiHandler::~CgiHandler() {
	
}

void CgiHandler::initializeInterpreters() {
	_interpreters[".py"] = "/usr/bin/python3";
	_interpreters[".pl"] = "/usr/bin/perl";
	_interpreters[".sh"] = "/bin/bash";
	_interpreters[".rb"] = "/usr/bin/ruby";
	_interpreters[".php"] = "/usr/bin/php";
	_interpreters[".cgi"] = "";
}

bool CgiHandler::isCgiRequest(const std::string& uri) const {
	if (uri.find("/cgi-bin/") == 0) {
		LOG_DEBUG(uri + " found in cgi-bin");
		return true;
	}
		
	// checking file extensions
	for (std::map<std::string, std::string>::const_iterator it = _interpreters.begin();
		 it != _interpreters.end(); ++it) {
		if (uri.find(it->first) != std::string::npos)
			return true;
	}
	return false;
}

void CgiHandler::setCgiBinPath(const std::string& path) {
	_cgi_bin_path = path;
}

std::string CgiHandler::handleCgiRequest(const HttpRequest& request) const {
	std::string uri = request.getUri();
	std::string script_path = getScriptPath(uri);
	
	LOG_DEBUG("cgi request for: " + script_path);

	if(!fileExists(script_path)) {
		LOG_ERROR("cgi script not found: " + script_path);
		return generateErrorResponse(404, "CGI Script Not Found");
	}
	if (!isExecutable(script_path)) {
		LOG_ERROR("cgi script not executable: " + script_path);
		return generateErrorResponse(403, "CGI Script Not Executable");
	}

	LOG_DEBUG("cgi script is executable, going to execution");
	return execute(script_path, request, _interpreters);
}

std::string CgiHandler::execute(const std::string& script_path, 
								const HttpRequest& request,
								const std::map<std::string, std::string>& interpreters) const {

	int pipe_stdout[2];
	int pipe_stdin[2];

	if (!createPipes(pipe_stdout, pipe_stdin))
		return generateErrorResponse(500, "Internal Server Error - Pipe Creation Failed");

	pid_t pid = fork();
	if (pid == -1) {
		close(pipe_stdout[0]);
		close(pipe_stdout[1]);
		close(pipe_stdin[0]);
		close(pipe_stdin[1]);
	return generateErrorResponse(500, "Internal Server Error - Fork Failed");
	}

	if (pid == 0) {
	setupChildProcess(pipe_stdout, pipe_stdin, request, script_path, interpreters);
	LOG_ERROR("error in child");
	exit(1);
	}
	return handleParentProcess(pipe_stdout, pipe_stdin, request, pid);
}

bool CgiHandler::createPipes(int pipe_stdout[2], int pipe_stdin[2]) const {
	if (pipe(pipe_stdout) == -1)
	return false;
		
	if (pipe(pipe_stdin) == -1) {
	close(pipe_stdout[0]);
	close(pipe_stdout[1]);
	return false;
	}
		
	return true;
}

void CgiHandler::setupChildProcess(int pipe_stdout[2], int pipe_stdin[2], 
				  const HttpRequest& request, const std::string& script_path,
				  const std::map<std::string, std::string>& interpreters) const {
	// (void) interpreters;
	// (void)	request;
	close(pipe_stdout[0]);
	close(pipe_stdin[1]);
		
	dup2(pipe_stdout[1], STDOUT_FILENO);
	close(pipe_stdout[1]);
		
	dup2(pipe_stdin[0], STDIN_FILENO);
	close(pipe_stdin[0]);

	std::vector<std::string> env_vars = setupEnvironment(request, script_path);
	
	std::vector<char*> env_ptrs;	// extracting array from env
	for (size_t i = 0; i < env_vars.size(); ++i)
		env_ptrs.push_back(const_cast<char*>(env_vars[i].c_str()));
	env_ptrs.push_back(NULL);

	std::vector<char*> argv_ptrs;	// extract from av for setup
	std::string interpreter = getInterpreter(script_path, interpreters);
	
	if (!interpreter.empty()) {
		argv_ptrs.push_back(const_cast<char*>(interpreter.c_str()));
		argv_ptrs.push_back(const_cast<char*>(script_path.c_str()));
		argv_ptrs.push_back(NULL);
		execve(interpreter.c_str(), &argv_ptrs[0], &env_ptrs[0]);
	} 
	else {// executes script directly
		argv_ptrs.push_back(const_cast<char*>(script_path.c_str()));
		argv_ptrs.push_back(NULL);
		execve(script_path.c_str(), &argv_ptrs[0], &env_ptrs[0]);
	}
		
	exit(1);
}

bool CgiHandler::waitForCgiCompletion(pid_t child_pid) const {
	int status;
	waitpid(child_pid, &status, 0);
	
	if (WEXITSTATUS(status) != 0) {
		LOG_ERROR("CGI script exited with non-zero status");
		return false;
	}
	
	return true;
}

bool CgiHandler::checkCgiProcessFinished(pid_t child_pid, int pipe_stdout, std::string& output) const {
	int status;
	pid_t result = waitpid(child_pid, &status, WNOHANG);
	
	if (result == child_pid) {
		char buffer[1024];
		ssize_t bytes_read;
		while ((bytes_read = read(pipe_stdout, buffer, sizeof(buffer) - 1)) > 0) {
			buffer[bytes_read] = '\0';
			output += buffer;
		}
		return true;
	}
	
	return false;
}

bool CgiHandler::handleCgiStdinWrite(int pipe_stdin, const std::string& body, 
									size_t& bytes_written, bool& stdin_closed, 
									std::vector<struct pollfd>& cgi_fds) const {
	if (bytes_written < body.length()) {
		ssize_t written = write(pipe_stdin, 
			body.c_str() + bytes_written, 
			body.length() - bytes_written);
		
		if (written > 0) // negative = EAGAIN/EWOULDBLOCK - continue polling
			bytes_written += written;
		
	}

	if (bytes_written >= body.length()) {	// check if finished
		close(pipe_stdin);
		stdin_closed = true;
		cgi_fds.pop_back();  // remove stdin from polling
		return false;  // finished writing
	}
	return true;
}

bool CgiHandler::handleCgiStdoutRead(int pipe_stdout, std::string& output) const {
	char buffer[1024];
	ssize_t bytes_read = read(pipe_stdout, buffer, sizeof(buffer) - 1);
	
	if (bytes_read > 0) {
		buffer[bytes_read] = '\0';
		output += buffer;
		return true;
	} else if (bytes_read == 0) // means EOF
		return false;
	return true;
}

void CgiHandler::setupCgiPipes(std::vector<struct pollfd>& cgi_fds, int pipe_stdout[2], 
							   int pipe_stdin[2], const HttpRequest& request) const {
	
	struct pollfd stdout_fd = {pipe_stdout[0], POLLIN, 0}; // monitors stdout for reading
	cgi_fds.push_back(stdout_fd);

	if (request.getMethod() == POST && !request.getBody().empty()) { // stdin if data
		struct pollfd stdin_fd = {pipe_stdin[1], POLLOUT, 0};
		cgi_fds.push_back(stdin_fd);
	}
}

void CgiHandler::closeCgiPipes(int pipe_stdout[2], int pipe_stdin[2], bool stdin_closed) const {
	if (!stdin_closed)
		close(pipe_stdin[1]);
	close(pipe_stdout[0]);
}

std::string CgiHandler::handleParentProcess(int pipe_stdout[2], int pipe_stdin[2], 
				const HttpRequest& request, pid_t child_pid) const {
	close(pipe_stdout[1]);
	close(pipe_stdin[0]);
	
	fcntl(pipe_stdout[0], F_SETFL, O_NONBLOCK);
	fcntl(pipe_stdin[1], F_SETFL, O_NONBLOCK);

	std::vector<struct pollfd> cgi_fds;
	setupCgiPipes(cgi_fds, pipe_stdout, pipe_stdin, request);
	
	// cgi communication state
	std::string output;
	size_t bytes_written = 0;
	bool stdin_closed = false;
	
	while (true) {
		int poll_result = poll(&cgi_fds[0], cgi_fds.size(), 30000);  // 5s timeout
		if (poll_result == -1) {
			kill(child_pid, SIGKILL);
			waitpid(child_pid, NULL, 0);
			LOG_ERROR("CGI poll failed");
			break;
		}
		if (poll_result == 0) {
			kill(child_pid, SIGKILL);
			waitpid(child_pid, NULL, 0);
			LOG_ERROR("CGI timeout");
			break;
		}
		if (cgi_fds[0].revents & POLLIN) {// reading from cgi stdout
			if (!handleCgiStdoutRead(pipe_stdout[0], output))
				break;
		}
		if (cgi_fds.size() > 1 && (cgi_fds[1].revents & POLLOUT) && !stdin_closed) { //writing to cgi stdin
			const std::string& body = request.getBody();
			handleCgiStdinWrite(pipe_stdin[1], body, bytes_written, stdin_closed, cgi_fds);
		}
		if (checkCgiProcessFinished(child_pid, pipe_stdout[0], output))
			break;
	}

	closeCgiPipes(pipe_stdout, pipe_stdin, stdin_closed);
	
	if (!waitForCgiCompletion(child_pid))
		return generateErrorResponse(500, "CGI Script Execution Error");

	return parseCgiOutput(output);
}

std::string CgiHandler::readFromPipe(int pipe_fd) const {
	std::string output;
	char buffer[1024];
	ssize_t bytes_read;
	
	while ((bytes_read = read(pipe_fd, buffer, sizeof(buffer) - 1)) > 0) {
		buffer[bytes_read] = '\0';
		output += buffer;
	}
	
	return output;
}

std::string CgiHandler::parseCgiOutput(const std::string& raw_output) const {
	size_t header_end = raw_output.find("\r\n\r\n");
	if (header_end == std::string::npos) {
		header_end = raw_output.find("\n\n");
		if (header_end == std::string::npos) {
			return generateCgiResponse("", raw_output);
		}
		header_end += 2;
	} else 
		header_end += 4;
	
	std::string headers = raw_output.substr(0, header_end - (header_end > 2 ? (raw_output[header_end - 3] == '\r' ? 4 : 2) : 0));
	std::string body = raw_output.substr(header_end);
	
	return generateCgiResponse(headers, body);
}

std::string CgiHandler::generateCgiResponse(const std::string& cgi_headers, const std::string& body) const {
	std::ostringstream response;
	
	response << "HTTP/1.1 200 OK\r\n";
	
	// cgi headers
	if (!cgi_headers.empty()) {
		std::string headers = cgi_headers;// add cgi headers for correct line endings
		if (headers[headers.length() - 1] != '\n')
			headers += "\r\n";
		response << headers;
	}
	// adding default headers if not existing
	if (cgi_headers.find("Content-Type:") == std::string::npos && 
		cgi_headers.find("content-type:") == std::string::npos)
		response << "Content-Type: text/html\r\n";
	
	response << "Content-Length: " << body.length() << "\r\n";
	response << "Connection: close\r\n";
	response << "Server: Webserv/1.0\r\n";
	response << "\r\n";
	response << body;
	
	return response.str();
}
