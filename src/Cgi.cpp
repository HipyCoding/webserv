
#include "Cgi.hpp"
#include "utils.hpp"

CgiHandler::CgiHandler() : _cgi_bin_path("./www/cgi-bin") {
	initializeInterpreters();
}

CgiHandler::CgiHandler(const std::string& cgi_bin_path) : _cgi_bin_path(cgi_bin_path) {
	initializeInterpreters();
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
		log_debug(uri + " found in cgi-bin");
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

std::string CgiHandler::execute(const std::string& script_path, 
								const HttpRequest& request,
								const std::map<std::string, std::string>& interpreters) const {
	// (void)script_path;	//tte
	// (void)request;		// tte
	// (void)interpreters;	// tte

	// log_debug("cgi request");
	// std::string response = "HTTP/1.1 200 OK\\r\\n";
	// response += "Content-Type: text/html\\r\\n";
	// response += "Content-Length: 52\\r\\n";
	// response += "Connection: close\\r\\n\\r\\n";
	// response += "<html><body><h1>cgi placeholder test</h1></body></html>";
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
	log_error("error in child");
	exit(1);
	}

	close(pipe_stdout[1]);
	close(pipe_stdin[0]);
	close(pipe_stdin[1]);
		
	// PLACEHOLDER: just read something simple
	char buffer[1024];
	read(pipe_stdout[0], buffer, sizeof(buffer));
	close(pipe_stdout[0]);
		
	int status;
	waitpid(pid, &status, 0);
		
	// SIMPLE response for now
	return "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 46\r\n\r\n<html><body><h1>basic cgi test</h1></body></html>";
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
	(void) interpreters;
	(void)	request;
	close(pipe_stdout[0]);
	close(pipe_stdin[1]);
		
	dup2(pipe_stdout[1], STDOUT_FILENO);
	close(pipe_stdout[1]);
		
	dup2(pipe_stdin[0], STDIN_FILENO);
	close(pipe_stdin[0]);
		
	char* argv[2];
	argv[0] = const_cast<char*>(script_path.c_str());
	argv[1] = NULL;
	execve(script_path.c_str(), argv, NULL); // no environment yet
		
	exit(1);
}

std::string CgiHandler::generateErrorResponse(int status_code, const std::string& status_text) const {
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