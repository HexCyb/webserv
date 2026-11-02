#ifndef HANDLER_HPP
# define HANDLER_HPP

# include "Config.hpp"
# include "Request.hpp"
# include "Response.hpp"

# include <string>

// What the event loop needs to start a CGI program for a request.
struct CgiJob
{
    std::string interpreter;    // /usr/bin/python3
    std::string scriptPath;     // ./www/cgi-bin/hello.py (file on disk)
    std::string scriptName;     // /cgi-bin/hello.py      (URL of the script)
    std::string pathInfo;       // whatever follows the script name in the URL
};

namespace handler
{
    // Answers a complete request. Returns false when a CGI program has to
    // answer it instead; `job` then describes which one.
    bool        handle(const ServerConfig &server, const Request &request, Response &response, CgiJob &job);

    // The server's custom page for `status` if it has one, a built-in page otherwise.
    Response    errorPage(const ServerConfig *server, int status);
}

#endif
