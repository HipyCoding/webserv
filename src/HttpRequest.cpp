#include "HttpRequest.hpp"
#include "utils.hpp"
#include <sstream>
#include <algorithm>

HttpRequest::HttpRequest() : _method(UNKNOWN), _is_complete(false), _is_chunked(false), _bytes_remaining(0) {
}

HttpRequest::~HttpRequest() {
}

bool HttpRequest::parseRequest(const std::string& raw_request) {
    std::istringstream stream(raw_request);
    std::string line;

    if (!std::getline(stream, line)) {
        return false;
    }
    if (!line.empty() && line[line.length() - 1] == '\r') {
        line.erase(line.length() - 1);
    }
    
    std::istringstream request_line(line);
    std::string method_str;
    
    if (!(request_line >> method_str >> _uri >> _version)) {
        return false;
    }
    if (_uri.length() > 2048) // rfc 7230 recommends 8000, but 2048 is common
		return false;
    if (method_str == "GET") {
        _method = GET;
    } else if (method_str == "POST") {
        _method = POST;
    } else if (method_str == "DELETE") {
        _method = DELETE;
    } else {
        _method = UNKNOWN;
    }
    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }
        
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            while (!value.empty() && value[0] == ' ') {
                value.erase(0, 1);
            }
            
            _headers[key] = value;
        }
    }
    std::string transfer_encoding = getHeader("Transfer-Encoding");
    if(!transfer_encoding.empty()){
        std::transform(transfer_encoding.begin(), transfer_encoding.end(),
                    transfer_encoding.begin(), ::tolower);
        if (transfer_encoding.find("chunked") != std::string::npos){
            _is_chunked = true;
            LOG_DEBUG("Detect chunked transfer encoding");
        }
    }
    if (_is_chunked){
        std::string remaining_data;
        std::string body_line;
        while (std::getline(stream, body_line))
            remaining_data += body_line + "\n";
        if (!remaining_data.empty())
            return processChunk(remaining_data);
        _is_complete = false;
        return true;
    }
    else {
    std::string body_line;
    while (std::getline(stream, body_line))
        _body += body_line + "\n";
    
    _is_complete = true;
    return true;
    }
}

bool HttpRequest::needsMoreChunks() const {
    return _is_chunked && !_is_complete;
}

bool HttpRequest::processChunk(const std::string& chunk_data) {
    if (!_is_chunked) {
        return false;
    }
    
    _chunk_buffer += chunk_data;
    
    while (true) {
        size_t line_end = _chunk_buffer.find("\r\n");
        if (line_end == std::string::npos)
            return true;
        std::string chunk_size_line = _chunk_buffer.substr(0, line_end);
        size_t chunk_size = 0;
        std::istringstream hex_stream(chunk_size_line);
        hex_stream >> std::hex >> chunk_size;
        
        if (hex_stream.fail()) {
            LOG_ERROR("Invalid chunk size format: " + chunk_size_line);
            return false;
        }
        
        LOG_DEBUG("Chunk size: " + size_t_to_string(chunk_size));
        
        // checks if we have the complete chunk (size + \r\n + data + \r\n)
        size_t chunk_data_start = line_end + 2;
        size_t required_size = chunk_data_start + chunk_size + 2;
        
        if (_chunk_buffer.length() < required_size)
            return true;
        if (chunk_size == 0) {
            _is_complete = true;
            LOG_DEBUG("Received terminating chunk, request complete");
            return true;
        }
        std::string chunk_content = _chunk_buffer.substr(chunk_data_start, chunk_size);
        _body += chunk_content;
        LOG_DEBUG("Added chunk data: " + size_t_to_string(chunk_size) + " bytes");
        _chunk_buffer.erase(0, required_size);
        if (_chunk_buffer.empty()) {
            break;
        }
    }
    
    return true;
}

std::string HttpRequest::getHeader(const std::string& key) const {
    std::map<std::string, std::string>::const_iterator it = _headers.find(key);
    if (it != _headers.end()) {
        return it->second;
    }
    return "";
}

std::string HttpRequest::methodToString() const {
    switch (_method) {
        case GET: return "GET";
        case POST: return "POST";
        case DELETE: return "DELETE";
        case UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}
