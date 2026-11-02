#include "Cgi.hpp"
#include "utils.hpp"

#include <cctype>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{
    const time_t kTimeout = 5;                      // seconds before a CGI program is killed
    const size_t kMaxOutput = 64 * 1024 * 1024;     // larger answers are refused (502)

    // "user-agent" -> "HTTP_USER_AGENT"
    std::string envName(const std::string &header)
    {
        std::string name = "HTTP_";
        for (size_t i = 0; i < header.size(); ++i)
            name += (header[i] == '-') ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(header[i])));
        return name;
    }
}

Cgi::Cgi() : _pid(-1), _in(-1), _out(-1), _written(0), _done(false), _failed(false), _started(0)
{
}

Cgi::~Cgi()
{
    stop();
}

bool Cgi::start(const CgiJob &job, const Request &req, const ServerConfig &server,
                const std::string &clientIp, int port)
{
    std::vector<std::string> env;
    env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env.push_back("SERVER_PROTOCOL=" + req.version());
    env.push_back("SERVER_SOFTWARE=webserv/1.0");
    env.push_back("SERVER_NAME=" + (server.serverNames.empty() ? std::string("localhost") : server.serverNames[0]));
    env.push_back("SERVER_PORT=" + utils::toString(port));
    env.push_back("REQUEST_METHOD=" + req.method());
    env.push_back("REQUEST_URI=" + req.target());
    env.push_back("SCRIPT_NAME=" + job.scriptName);
    env.push_back("SCRIPT_FILENAME=" + utils::baseName(job.scriptPath));
    env.push_back("PATH_INFO=" + job.pathInfo);
    env.push_back("QUERY_STRING=" + req.query());
    env.push_back("REMOTE_ADDR=" + clientIp);
    env.push_back("REDIRECT_STATUS=200");               // php-cgi refuses to run without it
    env.push_back("PATH=/usr/local/bin:/usr/bin:/bin");
    if (req.method() == "POST" || !req.body().empty())
        env.push_back("CONTENT_LENGTH=" + utils::toString(static_cast<long>(req.body().size())));
    if (!req.header("content-type").empty())
        env.push_back("CONTENT_TYPE=" + req.header("content-type"));
    const std::map<std::string, std::string> &headers = req.headers();
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
        if (it->first != "content-type" && it->first != "content-length")
            env.push_back(envName(it->first) + "=" + it->second);

    // everything the child needs is prepared before fork()
    std::vector<char *> envp;
    for (size_t i = 0; i < env.size(); ++i)
        envp.push_back(const_cast<char *>(env[i].c_str()));
    envp.push_back(NULL);
    std::string script = utils::baseName(job.scriptPath);
    std::string dir = utils::dirName(job.scriptPath);
    char *argv[3];
    argv[0] = const_cast<char *>(job.interpreter.c_str());
    argv[1] = const_cast<char *>(script.c_str());
    argv[2] = NULL;

    int toChild[2];
    int fromChild[2];
    if (pipe(toChild) < 0)
        return false;
    if (pipe(fromChild) < 0)
    {
        close(toChild[0]);
        close(toChild[1]);
        return false;
    }
    std::cout.flush();
    _pid = fork();
    if (_pid < 0)
    {
        close(toChild[0]);
        close(toChild[1]);
        close(fromChild[0]);
        close(fromChild[1]);
        return false;
    }
    if (_pid == 0)
    {
        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        for (int fd = 3; fd < 1024; ++fd)       // no sockets or other pipes leak into the script
            close(fd);
        if (chdir(dir.c_str()) == 0)            // so the script can open files next to itself
            execve(argv[0], argv, &envp[0]);
        const char msg[] = "Status: 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\n"
                           "the CGI program could not be started\n";
        ssize_t ignored = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        (void)ignored;
        std::exit(1);
    }
    close(toChild[0]);
    close(fromChild[1]);
    _in = toChild[1];
    _out = fromChild[0];
    fcntl(_in, F_SETFL, O_NONBLOCK);
    fcntl(_out, F_SETFL, O_NONBLOCK);
    _input = req.body();
    _started = std::time(NULL);
    if (_input.empty())
        closeInput();
    return true;
}

int Cgi::inputFd() const
{
    return _in;
}

int Cgi::outputFd() const
{
    return _out;
}

bool Cgi::done() const
{
    return _done;
}

bool Cgi::expired(time_t now) const
{
    return !_done && now - _started > kTimeout;
}

void Cgi::writeInput()
{
    if (_in < 0)
        return;
    size_t left = _input.size() - _written;
    ssize_t n = write(_in, _input.data() + _written, (left > 65536) ? 65536 : left);
    if (n <= 0)
    {
        closeInput();       // the script stopped reading: nothing more to send
        return;
    }
    _written += static_cast<size_t>(n);
    if (_written >= _input.size())
        closeInput();       // EOF tells the script the body is over
}

void Cgi::readOutput()
{
    if (_out < 0)
        return;
    char buf[65536];
    ssize_t n = read(_out, buf, sizeof(buf));
    if (n > 0)
    {
        _output.append(buf, static_cast<size_t>(n));
        if (_output.size() > kMaxOutput)
        {
            _failed = true;
            stop();
            _done = true;
        }
        return;
    }
    if (n < 0)
        _failed = true;
    close(_out);            // 0 = the script closed its output: it is finished
    _out = -1;
    closeInput();
    reap();
    _done = true;
}

void Cgi::closeInput()
{
    if (_in >= 0)
    {
        close(_in);
        _in = -1;
    }
}

void Cgi::reap()
{
    if (_pid <= 0)
        return;
    int status = 0;
    if (waitpid(_pid, &status, WNOHANG) == 0)       // output closed but still running
    {
        kill(_pid, SIGKILL);
        waitpid(_pid, &status, 0);
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0 && _output.empty())
        _failed = true;
    _pid = -1;
}

void Cgi::stop()
{
    closeInput();
    if (_out >= 0)
    {
        close(_out);
        _out = -1;
    }
    if (_pid > 0)
    {
        kill(_pid, SIGKILL);
        waitpid(_pid, NULL, 0);
        _pid = -1;
    }
}

// A CGI answer is "Header: value" lines, an empty line, then the body.
Response Cgi::response() const
{
    if (_failed)
        return Response(502);
    size_t split = _output.find("\r\n\r\n");
    size_t skip = 4;
    size_t lf = _output.find("\n\n");
    if (lf != std::string::npos && (split == std::string::npos || lf < split))
    {
        split = lf;
        skip = 2;
    }
    if (split == std::string::npos)
        return Response(502);

    Response res(200);
    bool hasType = false;
    bool hasStatus = false;
    std::vector<std::string> lines = utils::split(_output.substr(0, split), '\n');
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string line = utils::trim(lines[i]);
        if (line.empty())
            continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            return Response(502);
        std::string name = utils::trim(line.substr(0, colon));
        std::string value = utils::trim(line.substr(colon + 1));
        std::string lower = utils::toLower(name);
        if (lower == "status")
        {
            long code = 0;
            if (!utils::parseLong(value.substr(0, 3), code) || code < 100 || code > 599)
                return Response(502);
            res.status = static_cast<int>(code);
            res.message = utils::trim(value.substr(3));
            hasStatus = true;
        }
        else if (lower == "content-length" || lower == "connection" || lower == "transfer-encoding"
                 || lower == "server" || lower == "date")
            continue;           // the server writes these itself
        else
        {
            if (lower == "content-type")
                hasType = true;
            if (lower == "location" && !hasStatus)
                res.status = 302;
            res.setHeader(name, value);
        }
    }
    if (!hasType)
        res.setHeader("Content-Type", "text/html; charset=utf-8");
    res.body = _output.substr(split + skip);
    return res;
}
