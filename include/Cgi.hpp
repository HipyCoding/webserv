
#ifndef CGI_HPP
#define CGI_HPP

#include <string>
#include <map>
#include <iostream>
#include "HttpRequest.hpp"

class CgiHandler {
private:
	std::string _cgi_bin_path;
	std::map<std::string, std::string> _interpreters;
	
	void initializeInterpreters();

public:
	CgiHandler();
	CgiHandler(const std::string& cgi_bin_path);
	~CgiHandler();
	
	bool isCgiRequest(const std::string& uri) const;
	void setCgiBinPath(const std::string& path);
};

#endif