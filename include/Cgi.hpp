
#ifndef CGI_HPP
#define CGI_HPP

#include <string>
#include <map>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <sstream>
#include <fcntl.h>
#include <cstdlib>
#include "HttpRequest.hpp"
#include "utils.hpp"

class CgiHandler {
private:
	std::string _cgi_bin_path;
	std::map<std::string, std::string> _interpreters;

	bool createPipes(int pipe_stdout[2], int pipe_stdin[2]) const;
	void setupChildProcess(int pipe_stdout[2], int pipe_stdin[2], 
						const HttpRequest& request, const std::string& script_path,
						const std::map<std::string, std::string>& interpreters) const;
	std::string handleParentProcess(int pipe_stdout[2], int pipe_stdin[2], 
				const HttpRequest& request, pid_t child_pid) const;
	
	void initializeInterpreters();
	std::string generateErrorResponse(int status_code, const std::string& status_text) const;

public:
	CgiHandler();
	CgiHandler(const std::string& cgi_bin_path);
	~CgiHandler();
	
	bool isCgiRequest(const std::string& uri) const;
	void setCgiBinPath(const std::string& path);
	std::string execute(const std::string& script_path, 
					   const HttpRequest& request,
					   const std::map<std::string, std::string>& interpreters) const;
};

#endif