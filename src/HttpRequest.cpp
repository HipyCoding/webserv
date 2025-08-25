#include "HttpRequest.hpp"
#include "utils.hpp"
#include <sstream>
#include <algorithm>

// HttpRequest::HttpRequest() : _method(UNKNOWN), _is_complete(false), _is_chunked(false), _bytes_remaining(0), _is_multipart(false) {
// }

HttpRequest::HttpRequest() : _method(UNKNOWN), _is_complete(false), _is_chunked(false), _is_multipart(false) {
}

HttpRequest::~HttpRequest() {
}

bool HttpRequest::parseRequest(const std::string& raw_request) {
    if (_is_complete)
        return true;
    
    std::istringstream stream(raw_request);
    std::string line;

    if (_uri.empty()) {
        if (!std::getline(stream, line))
            return false;
        if (!line.empty() && line[line.length() - 1] == '\r')
            line.erase(line.length() - 1);
        
        std::istringstream request_line(line);
        std::string method_str;
        
        if (!(request_line >> method_str >> _uri >> _version)) {
            return false;
        }
        if (_uri.length() > 2048) {
            return false;
        }
        
        if (method_str == "GET") {
            _method = GET;
        } else if (method_str == "POST") {
            _method = POST;
        } else if (method_str == "DELETE") {
            _method = DELETE;
        } else {
            _method = UNKNOWN;
        }
        
        // Parse headers
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
        std::string content_type = getHeader("Content-Type");
        if (content_type.find("multipart/form-data") != std::string::npos){
            _is_multipart = true;
            LOG_DEBUG("Detected multipart form data");
        }
        
        std::string transfer_encoding = getHeader("Transfer-Encoding");
        if (!transfer_encoding.empty()) {
            std::transform(transfer_encoding.begin(), transfer_encoding.end(), 
                          transfer_encoding.begin(), ::tolower);
            if (transfer_encoding.find("chunked") != std::string::npos) {
                _is_chunked = true;
                LOG_DEBUG("Detected chunked transfer encoding");
            }
        }
    }

    size_t body_start = raw_request.find("\r\n\r\n");
    if (body_start == std::string::npos)
    return false;
    body_start += 4;

    if (body_start == std::string::npos)
        return false;
    if (_is_chunked) {
        std::string body_data = raw_request.substr(body_start);
        return processChunk(body_data);
    } else {
    std::string content_length_str = getHeader("Content-Length");
    if (!content_length_str.empty()) {
        size_t content_length = 0;
        std::istringstream iss(content_length_str);
        iss >> content_length;
        
        std::string body_data = raw_request.substr(body_start);
        if (body_data.length() >= content_length) {
            _body = body_data.substr(0, content_length);
            _is_complete = true;
            if (_is_multipart)
                parseMultipartData();
            return true;
        }
        return false;
    } else {
        if (_method == GET || _method == DELETE) {
            _body = raw_request.substr(body_start);
            _is_complete = true;
            return true;
        }
        return false;
    }
}

}
bool HttpRequest::parseMultipartData() {
    std::string content_type = getHeader("Content-Type");
    size_t boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos) {
        LOG_ERROR("No boundary found in multipart Content-Type");
        return false;
    }
    
    std::string boundary = content_type.substr(boundary_pos + 9);
    if (!boundary.empty() && boundary[0] == '"') {
        boundary = boundary.substr(1);
        if (!boundary.empty() && boundary[boundary.length() - 1] == '"') {
            boundary = boundary.substr(0, boundary.length() - 1);
        }
    }
    
    LOG_DEBUG("Multipart boundary: " + boundary);
    
    std::string delimiter = "--" + boundary;
    std::string end_delimiter = "--" + boundary + "--";
    
    size_t pos = 0;
    
    pos = _body.find(delimiter, pos);
    if (pos == std::string::npos) {
        LOG_ERROR("First boundary not found");
        return false;
    }
    
    pos += delimiter.length();
    
    while (pos < _body.length()) {
        if (pos < _body.length() && _body[pos] == '\r') pos++;
        if (pos < _body.length() && _body[pos] == '\n') pos++;
        size_t next_boundary = _body.find(delimiter, pos);
        if (next_boundary == std::string::npos) {
            break;
        }
        std::string part_data = _body.substr(pos, next_boundary - pos);
        size_t headers_end = part_data.find("\r\n\r\n");
        if (headers_end == std::string::npos) {
            headers_end = part_data.find("\n\n");
            if (headers_end != std::string::npos) {
                headers_end += 2;
            }
        } else
            headers_end += 4;
        
        if (headers_end == std::string::npos) {
            LOG_ERROR("No headers/body separator found in part");
            pos = next_boundary + delimiter.length();
            continue;
        }
        
        std::string part_headers = part_data.substr(0, headers_end - 4);
        std::string part_content = part_data.substr(headers_end);
        
        while (!part_content.empty() && 
               (part_content[part_content.length() - 1] == '\r' || 
                part_content[part_content.length() - 1] == '\n')) {
            part_content.erase(part_content.length() - 1);
        }
        std::map<std::string, std::string> part_header_map;
        std::istringstream header_stream(part_headers);
        std::string header_line;
        
        while (std::getline(header_stream, header_line)) {
            if (!header_line.empty() && header_line[header_line.length() - 1] == '\r')
                header_line.erase(header_line.length() - 1);
            
            size_t colon_pos = header_line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = header_line.substr(0, colon_pos);
                std::string value = header_line.substr(colon_pos + 1);
                while (!value.empty() && value[0] == ' ')
                    value.erase(0, 1);
                part_header_map[key] = value;
            }
        }
        std::string content_disposition = part_header_map["Content-Disposition"];
        if (content_disposition.empty())
            content_disposition = part_header_map["content-disposition"];
        
        if (!content_disposition.empty()) {
            std::string field_name;
            size_t name_pos = content_disposition.find("name=\"");
            if (name_pos != std::string::npos) {
                name_pos += 6;
                size_t name_end = content_disposition.find("\"", name_pos);
                if (name_end != std::string::npos) {
                    field_name = content_disposition.substr(name_pos, name_end - name_pos);
                }
            }
            size_t filename_pos = content_disposition.find("filename=\"");
            if (filename_pos != std::string::npos) {
                filename_pos += 10;
                size_t filename_end = content_disposition.find("\"", filename_pos);
                std::string filename;
                if (filename_end != std::string::npos)
                    filename = content_disposition.substr(filename_pos, filename_end - filename_pos);
                std::string file_content_type = part_header_map["Content-Type"];
                if (file_content_type.empty())
                    file_content_type = part_header_map["content-type"];
                if (file_content_type.empty())
                    file_content_type = "application/octet-stream";
                FormFile file;
                file.name = field_name;
                file.filename = filename;
                file.content_type = file_content_type;
                file.content = part_content;
                
                _uploaded_files.push_back(file);
                LOG_DEBUG("Found uploaded file: " + filename + " (" + size_t_to_string(part_content.length()) + " bytes)");
                
            } else {
                _form_data[field_name] = part_content;
                LOG_DEBUG("Found form field: " + field_name + " = " + part_content);
            }
        }
        pos = next_boundary + delimiter.length();
        if (_body.substr(next_boundary, end_delimiter.length()) == end_delimiter) {
            LOG_DEBUG("Found end delimiter, multipart parsing complete");
            break;
        }
    }
    
    return true;
}

bool HttpRequest::needsMoreChunks() const {
    return _is_chunked && !_is_complete;
}

bool HttpRequest::processChunk(const std::string& chunk_data) {
    if (!_is_chunked) {
        return false;
    }
    _chunk_buffer = chunk_data; // Replace the buffer with new data
    while (true) {
        size_t line_end = _chunk_buffer.find("\r\n");
        if (line_end == std::string::npos) {
            line_end = _chunk_buffer.find("\n");
            if (line_end == std::string::npos) {
                LOG_DEBUG("Incomplete chunk size line, need more data");
                return true;
            }
        }
        
        std::string chunk_size_line = _chunk_buffer.substr(0, line_end);
        LOG_DEBUG("Chunk size line: '" + chunk_size_line + "'");
        size_t chunk_size = 0;
        std::istringstream hex_stream(chunk_size_line);
        hex_stream >> std::hex >> chunk_size;
        
        if (hex_stream.fail()) {
            LOG_ERROR("Invalid chunk size format: " + chunk_size_line);
            return false;
        }
        
        LOG_DEBUG("Chunk size: " + size_t_to_string(chunk_size));
        size_t chunk_data_start = line_end + (_chunk_buffer.substr(line_end, 2) == "\r\n" ? 2 : 1);
        size_t required_size = chunk_data_start + chunk_size + 2; // +2 for trailing \r\n
        
        if (_chunk_buffer.length() < required_size) {
            LOG_DEBUG("Need more data: have " + size_t_to_string(_chunk_buffer.length()) + 
                     ", need " + size_t_to_string(required_size));
            return true;
        }
        
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
            LOG_DEBUG("Buffer empty, need more data for next chunk");
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
