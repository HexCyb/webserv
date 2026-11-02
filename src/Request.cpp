#include "Request.hpp"
#include "utils.hpp"

#include <cctype>

namespace
{
    const size_t kMaxHeaderBytes = 16 * 1024;
    const size_t kMaxTargetBytes = 8 * 1024;

    enum { CHUNK_SIZE, CHUNK_DATA, CHUNK_CRLF, CHUNK_TRAILER };
}

Request::Request()
{
    reset();
}

void Request::reset()
{
    _state = HEADERS;
    _error = 0;
    _waitingForLimit = false;
    _limit = static_cast<size_t>(-1);
    _contentLength = 0;
    _chunkLeft = 0;
    _chunkStep = CHUNK_SIZE;
    _method.clear();
    _target.clear();
    _path.clear();
    _query.clear();
    _version.clear();
    _body.clear();
    _headers.clear();
}

void Request::feed(std::string &buffer)
{
    if (_state == HEADERS)
    {
        // RFC 9112 2.2: empty lines before the request line are ignored
        while (buffer.size() >= 2 && buffer[0] == '\r' && buffer[1] == '\n')
            buffer.erase(0, 2);
        size_t end = buffer.find("\r\n\r\n");
        if (end == std::string::npos)
        {
            if (buffer.size() > kMaxHeaderBytes)
                fail(431);
            return;
        }
        if (end > kMaxHeaderBytes)
            return fail(431);
        std::string head = buffer.substr(0, end);
        buffer.erase(0, end + 4);
        parseHead(head);
        if (_state == ERROR)
            return;

        std::string encoding = utils::toLower(header("transfer-encoding"));
        std::string length = header("content-length");
        if (!encoding.empty())
        {
            if (encoding != "chunked")
                return fail(501);
            if (!length.empty())
                return fail(400);       // both at once is a request smuggling trick
            _state = CHUNKED;
        }
        else if (!length.empty())
        {
            long n = 0;
            if (!utils::parseLong(length, n))
                return fail(400);
            _contentLength = static_cast<size_t>(n);
            _state = (_contentLength > 0) ? BODY : DONE;
        }
        else
            _state = DONE;
        // the body limit depends on the matched location, so the server sets it first
        _waitingForLimit = (_state != DONE);
        return;
    }
    if (_waitingForLimit)
        return;
    if (_state == BODY)
        feedBody(buffer);
    else if (_state == CHUNKED)
        feedChunked(buffer);
}

bool Request::needsBodyLimit() const
{
    return _waitingForLimit;
}

void Request::setBodyLimit(size_t limit)
{
    _limit = (limit == 0) ? static_cast<size_t>(-1) : limit;
    _waitingForLimit = false;
    if (_state == BODY && _contentLength > _limit)
        fail(413);
}

void Request::parseHead(const std::string &head)
{
    size_t pos = head.find("\r\n");
    parseRequestLine(head.substr(0, pos));
    while (_state != ERROR && pos != std::string::npos)
    {
        size_t start = pos + 2;
        pos = head.find("\r\n", start);
        parseHeaderLine(head.substr(start, (pos == std::string::npos) ? std::string::npos : pos - start));
    }
    if (_state != ERROR && _version == "HTTP/1.1" && _headers.find("host") == _headers.end())
        fail(400);
}

void Request::parseRequestLine(const std::string &line)
{
    size_t sp1 = line.find(' ');
    size_t sp2 = (sp1 == std::string::npos) ? sp1 : line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos || line.find(' ', sp2 + 1) != std::string::npos)
        return fail(400);
    _method = line.substr(0, sp1);
    _target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    _version = line.substr(sp2 + 1);
    if (_method.empty() || _target.empty())
        return fail(400);
    for (size_t i = 0; i < _method.size(); ++i)
        if (!std::isupper(static_cast<unsigned char>(_method[i])))
            return fail(400);
    if (_version != "HTTP/1.1" && _version != "HTTP/1.0")
        return fail(_version.compare(0, 5, "HTTP/") == 0 ? 505 : 400);
    if (_target.size() > kMaxTargetBytes)
        return fail(414);

    std::string target = _target;
    if (target.compare(0, 7, "http://") == 0)       // absolute form: keep only the path
    {
        size_t slash = target.find('/', 7);
        target = (slash == std::string::npos) ? "/" : target.substr(slash);
    }
    if (target[0] != '/')
        return fail(400);
    size_t mark = target.find('?');
    if (mark != std::string::npos)
    {
        _query = target.substr(mark + 1);
        target.erase(mark);
    }
    bool ok = false;
    std::string decoded = utils::urlDecode(target, ok);
    if (ok)
        _path = utils::normalizePath(decoded, ok);
    if (!ok)
        fail(400);      // broken %-escape, or "/../" trying to leave the web root
}

void Request::parseHeaderLine(const std::string &line)
{
    size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0)
        return fail(400);
    std::string name = line.substr(0, colon);
    if (name.find_first_of(" \t") != std::string::npos)    // RFC 9112 5.1
        return fail(400);
    name = utils::toLower(name);
    std::string value = utils::trim(line.substr(colon + 1));
    std::map<std::string, std::string>::iterator it = _headers.find(name);
    if (it == _headers.end())
        _headers[name] = value;
    else if (name == "host" || name == "content-length")
    {
        if (it->second != value)
            fail(400);
    }
    else
        it->second += ", " + value;
}

void Request::feedBody(std::string &buffer)
{
    size_t missing = _contentLength - _body.size();
    size_t take = (buffer.size() < missing) ? buffer.size() : missing;
    _body.append(buffer, 0, take);
    buffer.erase(0, take);
    if (_body.size() == _contentLength)
        _state = DONE;
}

// Transfer-Encoding: chunked -> "<hex size>\r\n<data>\r\n" ... "0\r\n\r\n"
void Request::feedChunked(std::string &buffer)
{
    while (_state == CHUNKED)
    {
        if (_chunkStep == CHUNK_SIZE || _chunkStep == CHUNK_TRAILER)
        {
            size_t end = buffer.find("\r\n");
            if (end == std::string::npos)
            {
                if (buffer.size() > kMaxHeaderBytes)
                    fail(400);
                return;
            }
            std::string line = buffer.substr(0, end);
            buffer.erase(0, end + 2);
            if (_chunkStep == CHUNK_TRAILER)
            {
                if (line.empty())
                    _state = DONE;      // trailer fields are read and ignored
                continue;
            }
            size_t size = 0;
            if (!utils::parseHex(utils::trim(line.substr(0, line.find(';'))), size))
                return fail(400);
            if (size == 0)
                _chunkStep = CHUNK_TRAILER;
            else if (_body.size() + size > _limit)
                return fail(413);
            else
            {
                _chunkLeft = size;
                _chunkStep = CHUNK_DATA;
            }
        }
        else if (_chunkStep == CHUNK_DATA)
        {
            if (buffer.empty())
                return;
            size_t take = (buffer.size() < _chunkLeft) ? buffer.size() : _chunkLeft;
            _body.append(buffer, 0, take);
            buffer.erase(0, take);
            _chunkLeft -= take;
            if (_chunkLeft == 0)
                _chunkStep = CHUNK_CRLF;
        }
        else
        {
            if (buffer.size() < 2)
                return;
            if (buffer[0] != '\r' || buffer[1] != '\n')
                return fail(400);
            buffer.erase(0, 2);
            _chunkStep = CHUNK_SIZE;
        }
    }
}

void Request::fail(int status)
{
    _state = ERROR;
    _error = status;
    _waitingForLimit = false;
}

bool Request::keepAlive() const
{
    std::string connection = utils::toLower(header("connection"));
    if (_version == "HTTP/1.0")
        return connection.find("keep-alive") != std::string::npos;
    return connection.find("close") == std::string::npos;
}

std::string Request::header(const std::string &lowercaseName) const
{
    std::map<std::string, std::string>::const_iterator it = _headers.find(lowercaseName);
    return (it == _headers.end()) ? std::string() : it->second;
}

Request::State Request::state() const { return _state; }
int Request::error() const { return _error; }
const std::string &Request::method() const { return _method; }
const std::string &Request::target() const { return _target; }
const std::string &Request::path() const { return _path; }
const std::string &Request::query() const { return _query; }
const std::string &Request::version() const { return _version; }
const std::string &Request::body() const { return _body; }
const std::map<std::string, std::string> &Request::headers() const { return _headers; }
