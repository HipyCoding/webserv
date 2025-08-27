
#include "WebServer.hpp"
#include "Config.hpp"
#include "HttpRequest.hpp"
#include "utils.hpp"
#include <sstream>

std::string WebServer::generateResponse(const HttpRequest& request) {
    std::string method = request.methodToString();
    std::string uri = request.getUri();
    
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

    // Special handling for uploads directory
    if (uri.find("/uploads/") == 0) {
        std::string filename = uri.substr(9);
        
        if (filename.empty()) {
            std::string upload_dir = "./www/uploads";
            
            if (!fileExists(upload_dir) || !isDirectory(upload_dir)) {
                return generateErrorResponse(404, "Not Found");
            }
            
            return generateDirectoryListing(upload_dir, uri);
        }
        
        std::string file_path = "./www/uploads/" + filename;
        
        if (filename.find("..") != std::string::npos)
            return generateErrorResponse(403, "Forbidden");
        
        if (!fileExists(file_path))
            return generateErrorResponse(404, "Not Found");
        
        if (access(file_path.c_str(), R_OK) != 0)
            return generateErrorResponse(403, "Forbidden");
        
        std::string content = readFile(file_path);
        if (content.empty() && fileExists(file_path))
            return generateErrorResponse(500, "Internal Server Error");
        
        return generateSuccessResponse(content, getContentType(file_path));
    }

    // Special handling for CGI-bin directory
    if (uri.find("/cgi-bin/") == 0) {
        std::string script_name = uri.substr(9);
        
        if (script_name.empty()) {
            std::string cgi_dir = "./www/cgi-bin";
            
            if (!fileExists(cgi_dir) || !isDirectory(cgi_dir)) {
                return generateErrorResponse(404, "Not Found");
            }
            
            return generateDirectoryListing(cgi_dir, uri);
        }
        
        if (_cgi_handler && _cgi_handler->isCgiRequest(uri))
            return _cgi_handler->handleCgiRequest(request);
    }

    const ServerConfig* server_config = _config->findServerConfig("127.0.0.1", 8080, "");
    if (!server_config) {
        LOG_ERROR("no server config found");
        return generateErrorResponse(500, "Internal Server Error");
    }

    const LocationConfig* location_config = _config->findLocationConfig(*server_config, uri);
    
    // CHANGE: Check redirects BEFORE file existence
    if (location_config){
        std::string redirect_response = handleRedirect(location_config);
        if (!redirect_response.empty())
            return redirect_response;
    }

    if (location_config) {
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
    // file_path = root;
    // std::string redir_root = root;
    
    if (!fileExists(file_path))
        return generateErrorResponse(404, "Not Found");

    if (isDirectory(file_path))
        return handleDirectoryRequest(file_path, uri, location_config);
    
    if (access(file_path.c_str(), R_OK) != 0)
        return generateErrorResponse(403, "Forbidden");
    
    std::string content = readFile(file_path);
    if (content.empty() && fileExists(file_path))
        return generateErrorResponse(500, "Internal Server Error");
    
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
    LOG_INFO("POST request for: " + uri + " (body: " + size_t_to_string(body.length()) + " bytes)");

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
	
    // LOG_ERROR("uri: " + uri + "root: " + root);
	std::string file_path = getFilePathWithRoot(uri, root);
	
	LOG_INFO("DELETE request for: " + file_path);
	
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
	} else
		return generateErrorResponse(500, "Internal Server Error - Delete failed");
}

std::string WebServer::handleRedirect(const LocationConfig* location) {
    if (!location || location->redirect.empty())
        return "";
    
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
    std::string upload_dir = "./www/uploads";
    mkdir(upload_dir.c_str(), 0755);
    if (request.isMultipart() && !request.getUploadedFiles().empty())
        return handleMultipartUpload(request);
    else
        return handleSimpleUpload(request);
}

std::string WebServer::handleMultipartUpload(const HttpRequest& request) {
    std::string upload_dir = "./www/uploads";
    const std::vector<FormFile>& uploaded_files = request.getUploadedFiles();
    const std::map<std::string, std::string>& form_data = request.getFormData();
    
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><title>Upload Results</title>";
    html << "<style>body{font-family:Arial,sans-serif;margin:40px;} ";
    html << ".file{border:1px solid #ddd;padding:15px;margin:10px 0;} ";
    html << "img{max-width:300px;max-height:300px;border:1px solid #ccc;}</style>";
    html << "</head><body><h1>File Upload Results</h1>";
    
    if (uploaded_files.empty()) {
        html << "<p>No files uploaded.</p>";
    } else {
        html << "<h2>Uploaded Files:</h2>";
        
        for (size_t i = 0; i < uploaded_files.size(); ++i) {
            const FormFile& file = uploaded_files[i];
            
            if (file.filename.empty() || file.content.empty())
                continue;
            std::string extension = getFileExtension(file.filename);
            if (extension.empty())
                extension = getExtensionFromContentType(file.content_type);
            std::ostringstream filename;
            filename << "upload_" << time(NULL) << "_" << i << extension;
            
            std::string file_path = upload_dir + "/" + filename.str();
            
            std::ofstream outfile(file_path.c_str(), std::ios::binary);
            if (!outfile.is_open()) {
                html << "<p>Error: Could not save file " << file.filename << "</p>";
                continue;
            }
            
            outfile.write(file.content.c_str(), file.content.length());
            outfile.close();
            html << "<div class='file'>";
            html << "<h3>" << file.filename << "</h3>";
            html << "<p><strong>Size:</strong> " << file.content.length() << " bytes</p>";
            html << "<p><strong>Type:</strong> " << file.content_type << "</p>";
            html << "<p><strong>Saved as:</strong> " << filename.str() << "</p>";
            if (file.content_type.find("image/") == 0) {
                html << "<p><strong>Preview:</strong></p>";
                html << "<img src='/uploads/" << filename.str() << "' alt='" << file.filename << "'>";
            }
            html << "</div>";
            
            LOG_INFO("Saved uploaded file: " + file.filename + " as " + filename.str() + 
                    " (" + size_t_to_string(file.content.length()) + " bytes)");
        }
    }
    if (!form_data.empty()) {
        html << "<h2>Form Data:</h2>";
        for (std::map<std::string, std::string>::const_iterator it = form_data.begin(); 
             it != form_data.end(); ++it) {
            html << "<p><strong>" << it->first << ":</strong> " << it->second << "</p>";
        }
    }
    html << "<p><a href='/'>Back to home</a></p>";
    html << "</body></html>";
    
    return generateSuccessResponse(html.str(), "text/html");
}

std::string WebServer::handleSimpleUpload(const HttpRequest& request) {
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
    html_content << "<html><body><h1>File uploaded successfully</h1>";
    html_content << "<p>Saved as: " << filename.str() << "</p>";
    html_content << "<p>Size: " << body.length() << " bytes</p>";
    if (request.isChunked()) {
        html_content << "<p>Transfer: Chunked encoding</p>";
    }
    html_content << "</body></html>";
    
    return generateSuccessResponse(html_content.str(), "text/html");
}

std::string WebServer::getFileExtension(const std::string& filename) {
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos != std::string::npos && dot_pos < filename.length() - 1) {
        return filename.substr(dot_pos);
    }
    return "";
}

std::string WebServer::getExtensionFromContentType(const std::string& content_type) {
    if (content_type == "image/png") return ".png";
    if (content_type == "image/jpeg") return ".jpg";
    if (content_type == "image/gif") return ".gif";
    if (content_type == "image/bmp") return ".bmp";
    if (content_type == "image/webp") return ".webp";
    if (content_type == "text/plain") return ".txt";
    if (content_type == "text/html") return ".html";
    if (content_type == "text/css") return ".css";
    if (content_type == "application/javascript") return ".js";
    if (content_type == "application/json") return ".json";
    if (content_type == "application/pdf") return ".pdf";
    if (content_type == "application/zip") return ".zip";
    if (content_type == "video/mp4") return ".mp4";
    if (content_type == "audio/mpeg") return ".mp3";
    return ".bin";
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

    html << "<!DOCTYPE html><html><head><title>Index of " << uri << "</title>";
    html << "<style>body{font-family:Arial,sans-serif;margin:40px;} ";
    html << "table{border-collapse:collapse;width:100%;} ";
    html << "th,td{text-align:left;padding:8px;border-bottom:1px solid #ddd;} ";
    html << "a{text-decoration:none;color:#3498db;} a:hover{text-decoration:underline;}</style>";
    html << "</head><body>";
    html << "<h1>Index of " << uri << "</h1><hr>";
    
    DIR* dir = opendir(dir_path.c_str());
    if (dir == NULL) {
        LOG_ERROR("Cannot open directory: " + dir_path);
        html << "<p>Error: Cannot read directory</p>";
        html << "</body></html>";
        return generateSuccessResponse(html.str(), "text/html");
    }
    
    html << "<table>";
    html << "<tr><th>Name</th><th>Size</th><th>Type</th></tr>";
    
    if (uri != "/")
        html << "<tr><td><a href=\"../\">../</a></td><td>-</td><td>Directory</td></tr>";
    
    struct dirent* entry;
    std::vector<std::string> files;
    std::vector<std::string> directories;
    
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name == "." || (name[0] == '.' && name != ".."))
            continue;
        
        std::string full_path = dir_path + "/" + name;
        if (isDirectory(full_path))
            directories.push_back(name);
		else
            files.push_back(name);
    }
    closedir(dir);
    
    std::sort(directories.begin(), directories.end());
    std::sort(files.begin(), files.end());
    
    for (size_t i = 0; i < directories.size(); ++i) {
        std::string name = directories[i];
        html << "<tr><td><a href=\"" << name << "/\">" << name << "/</a></td>";
        html << "<td>-</td><td>Directory</td></tr>";
    }
    
    for (size_t i = 0; i < files.size(); ++i) {
        std::string name = files[i];
        std::string full_path = dir_path + "/" + name;
        struct stat file_stat;
        std::string size_str = "-";
        if (stat(full_path.c_str(), &file_stat) == 0) {
            size_str = size_t_to_string(file_stat.st_size) + " bytes";
        }
        std::string type = getContentType(name);
        
        html << "<tr><td><a href=\"" << name << "\">" << name << "</a></td>";
        html << "<td>" << size_str << "</td><td>" << type << "</td></tr>";
    }
    html << "</table>";
    html << "<hr><p>Generated by Webserv/1.0</p>";
    html << "</body></html>";
    
    return generateSuccessResponse(html.str(), "text/html");
}