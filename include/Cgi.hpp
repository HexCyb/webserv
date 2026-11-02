#ifndef CGI_HPP
# define CGI_HPP

# include "Config.hpp"
# include "Handler.hpp"
# include "Request.hpp"
# include "Response.hpp"

# include <ctime>
# include <string>
# include <sys/types.h>

// One running CGI program. Its stdin and stdout pipes are non-blocking and are
// watched by the server's poll() loop together with the sockets.
class Cgi
{
public:
    Cgi();
    ~Cgi();

    bool        start(const CgiJob &job, const Request &req, const ServerConfig &server,
                      const std::string &clientIp, int port);
    int         inputFd() const;    // -1 once the whole body was written
    int         outputFd() const;   // -1 once the output is complete
    void        writeInput();       // poll(): the program's stdin is writable
    void        readOutput();       // poll(): the program's stdout has data or hung up
    bool        done() const;
    bool        expired(time_t now) const;
    void        stop();             // kills the program and closes the pipes
    Response    response() const;   // the program's output turned into an HTTP response

private:
    pid_t       _pid;
    int         _in;
    int         _out;
    std::string _input;
    size_t      _written;
    std::string _output;
    bool        _done;
    bool        _failed;
    time_t      _started;

    void        closeInput();
    void        reap();

    Cgi(const Cgi &);
    Cgi &operator=(const Cgi &);
};

#endif
