
#ifndef CGI_HPP
#define CGI_HPP

#include <string>
#include <map>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <iostream>
#include <sstream>
#include <fcntl.h>
#include <cstdlib>
#include <poll.h>
#include "HttpRequest.hpp"
#include "utils.hpp"

class CgiHandler {
private:
	std::string _cgi_bin_path;
	std::map<std::string, std::string> _interpreters;

	// pipes
	bool createPipes(int pipe_stdout[2], int pipe_stdin[2]) const;
	void setupCgiPipes(std::vector<struct pollfd>& cgi_fds, int pipe_stdout[2], 
					  int pipe_stdin[2], const HttpRequest& request) const;
	void closeCgiPipes(int pipe_stdout[2], int pipe_stdin[2], bool stdin_closed) const;

	// process management
	void setupChildProcess(int pipe_stdout[2], int pipe_stdin[2], 
						  const HttpRequest& request, const std::string& script_path,
						  const std::map<std::string, std::string>& interpreters) const;
	bool checkCgiProcessFinished(pid_t child_pid, int pipe_stdout, std::string& output) const;
	bool waitForCgiCompletion(pid_t child_pid) const;

	// poll i/o
	bool handleCgiStdoutRead(int pipe_stdout, std::string& output) const;
	bool handleCgiStdinWrite(int pipe_stdin, const std::string& body, 
							size_t& bytes_written, bool& stdin_closed, 
							std::vector<struct pollfd>& cgi_fds) const;
	std::string handleParentProcess(int pipe_stdout[2], int pipe_stdin[2], 
								   const HttpRequest& request, pid_t child_pid) const;

	// cgi environment and execution
	void initializeInterpreters();
	std::vector<std::string> setupEnvironment(const HttpRequest& request,
											  const std::string& script_path) const;
	std::string getInterpreter(const std::string& script_path,
							  const std::map<std::string, std::string>& interpreters) const;

	// cgi output
	std::string readFromPipe(int pipe_fd) const;
	std::string parseCgiOutput(const std::string& raw_output) const;
	std::string generateCgiResponse(const std::string& cgi_headers, const std::string& body) const;

	// cgi utilities
	std::string generateErrorResponse(int status_code, const std::string& status_text) const;
	bool isExecutable(const std::string& path) const;
	std::string getScriptPath(const std::string& uri) const;


public:
	CgiHandler();
	CgiHandler(const std::string& cgi_bin_path);
	~CgiHandler();
	
	bool isCgiRequest(const std::string& uri) const;
	void setCgiBinPath(const std::string& path);
	std::string execute(const std::string& script_path, 
					   const HttpRequest& request,
					   const std::map<std::string, std::string>& interpreters) const;
	std::string handleCgiRequest(const HttpRequest& request) const;
};

#endif