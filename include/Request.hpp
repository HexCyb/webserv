#ifndef REQUEST_HPP
# define REQUEST_HPP

# include <cstddef>
# include <map>
# include <string>

// Incremental HTTP/1.1 request parser. Bytes are fed as they arrive from the
// socket; whatever belongs to the next request stays in the buffer.
class Request
{
public:
    enum State { HEADERS, BODY, CHUNKED, DONE, ERROR };

    Request();

    void    reset();
    void    feed(std::string &buffer);
    bool    needsBodyLimit() const;         // headers are in, the body waits for its limit
    void    setBodyLimit(size_t limit);     // 0 = unlimited

    State               state() const;
    int                 error() const;      // HTTP status to answer with when state() == ERROR
    const std::string   &method() const;
    const std::string   &target() const;    // request target exactly as received
    const std::string   &path() const;      // decoded and normalized, always starts with '/'
    const std::string   &query() const;
    const std::string   &version() const;
    const std::string   &body() const;
    std::string         header(const std::string &lowercaseName) const;
    const std::map<std::string, std::string> &headers() const;
    bool                keepAlive() const;

private:
    State       _state;
    int         _error;
    bool        _waitingForLimit;
    size_t      _limit;
    size_t      _contentLength;
    size_t      _chunkLeft;
    int         _chunkStep;
    std::string _method;
    std::string _target;
    std::string _path;
    std::string _query;
    std::string _version;
    std::string _body;
    std::map<std::string, std::string> _headers;

    void    fail(int status);
    void    parseHead(const std::string &head);
    void    parseRequestLine(const std::string &line);
    void    parseHeaderLine(const std::string &line);
    void    feedBody(std::string &buffer);
    void    feedChunked(std::string &buffer);
};

#endif
