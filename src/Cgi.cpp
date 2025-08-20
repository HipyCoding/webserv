
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