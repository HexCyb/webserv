#include "Response.hpp"
#include "utils.hpp"

#include <ctime>

Response::Response(int code) : status(code)
{
}

void Response::setHeader(const std::string &name, const std::string &value)
{
    std::string key = utils::toLower(name);
    for (size_t i = 0; i < _headers.size(); ++i)
    {
        if (utils::toLower(_headers[i].first) == key)
        {
            _headers[i].second = value;
            return;
        }
    }
    _headers.push_back(std::make_pair(name, value));
}

std::string Response::header(const std::string &name) const
{
    std::string key = utils::toLower(name);
    for (size_t i = 0; i < _headers.size(); ++i)
        if (utils::toLower(_headers[i].first) == key)
            return _headers[i].second;
    return "";
}

std::string Response::serialize(bool keepAlive, bool headOnly) const
{
    bool noBody = (status == 204 || status == 304);
    std::string out = "HTTP/1.1 " + utils::toString(status) + " "
                    + (message.empty() ? reason(status) : message) + "\r\n";
    out += "Server: webserv/1.0\r\n";
    out += "Date: " + utils::httpDate(std::time(NULL)) + "\r\n";
    for (size_t i = 0; i < _headers.size(); ++i)
        out += _headers[i].first + ": " + _headers[i].second + "\r\n";
    if (!noBody)
        out += "Content-Length: " + utils::toString(static_cast<long>(body.size())) + "\r\n";
    out += std::string("Connection: ") + (keepAlive ? "keep-alive" : "close") + "\r\n\r\n";
    if (!headOnly && !noBody)
        out += body;
    return out;
}

std::string Response::reason(int status)
{
    switch (status)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Content Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown";
    }
}
