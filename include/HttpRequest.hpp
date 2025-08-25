#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include <map>
#include <vector>

enum HttpMethod {
    GET,
    POST,
    DELETE,
    UNKNOWN
};

class HttpRequest {
private:
    HttpMethod _method;
    std::string _uri;
    std::string _version;
    std::map<std::string, std::string> _headers;
    std::string _body;
    bool _is_complete;
    bool _is_chunked;
    size_t _bytes_remaining;
    std::string _chunk_buffer;

public:
    HttpRequest();
    ~HttpRequest();
    
    bool parseRequest(const std::string& raw_request);

    bool isChunked() const {return _is_chunked; }
    bool needsMoreChunks() const;
    bool processChunk(const std::string& chunk_data);
    
    // Getters
    HttpMethod getMethod() const { return _method; }
    const std::string& getUri() const { return _uri; }
    const std::string& getVersion() const { return _version; }
    const std::map<std::string, std::string>& getHeaders() const { return _headers; }
    const std::string& getBody() const { return _body; }
    bool isComplete() const { return _is_complete; }
    
    std::string getHeader(const std::string& key) const;
    std::string methodToString() const;
};

#endif