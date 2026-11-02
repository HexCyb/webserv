#include "Server.hpp"
#include "Handler.hpp"
#include "utils.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    const time_t    kIdleTimeout = 30;                  // seconds a silent connection stays open
    const time_t    kDrainTimeout = 5;
    const size_t    kMaxDrain = 256 * 1024 * 1024;
    const size_t    kMaxClients = 1000;

    volatile sig_atomic_t g_running = 1;

    void onSignal(int)
    {
        g_running = 0;
    }

    std::string ipToString(const struct sockaddr_in &addr)
    {
        unsigned long ip = ntohl(addr.sin_addr.s_addr);
        return utils::toString((ip >> 24) & 255) + "." + utils::toString((ip >> 16) & 255) + "."
             + utils::toString((ip >> 8) & 255) + "." + utils::toString(ip & 255);
    }

    std::string timeOfDay()
    {
        char buf[16];
        time_t now = std::time(NULL);
        struct tm *local = std::localtime(&now);
        if (local == NULL || std::strftime(buf, sizeof(buf), "%H:%M:%S", local) == 0)
            return "--:--:--";
        return buf;
    }
}

Client::Client(int socket, unsigned long number, size_t listenerIndex, const std::string &address)
    : fd(socket), id(number), listener(listenerIndex), ip(address), sent(0), server(NULL),
      phase(READING), closeAfterWrite(false), draining(false), drainLeft(0), cgi(NULL),
      lastActivity(std::time(NULL))
{
}

Server::Server(const Config &config) : _nextId(1)
{
    const std::vector<ServerConfig> &servers = config.servers();
    for (size_t s = 0; s < servers.size(); ++s)
        for (size_t l = 0; l < servers[s].listens.size(); ++l)
            openListener(servers[s].listens[l].first, servers[s].listens[l].second, &servers[s]);
}

Server::~Server()
{
    while (!_clients.empty())
        closeClient(_clients.begin()->second);
    for (size_t i = 0; i < _listeners.size(); ++i)
        close(_listeners[i].fd);
}

void Server::openListener(const std::string &host, int port, const ServerConfig *server)
{
    for (size_t i = 0; i < _listeners.size(); ++i)
    {
        if (_listeners[i].host == host && _listeners[i].port == port)
        {
            _listeners[i].servers.push_back(server);    // virtual host on an existing socket
            return;
        }
    }
    std::string where = host + ":" + utils::toString(port);
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    int rc = getaddrinfo(host.c_str(), utils::toString(port).c_str(), &hints, &res);
    if (rc != 0)
        throw std::runtime_error("cannot resolve " + where + ": " + gai_strerror(rc));
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0)
    {
        freeaddrinfo(res);
        throw std::runtime_error("socket: " + std::string(std::strerror(errno)));
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0 || listen(fd, SOMAXCONN) < 0
        || fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
    {
        std::string reason = std::strerror(errno);
        freeaddrinfo(res);
        close(fd);
        throw std::runtime_error("cannot listen on " + where + ": " + reason);
    }
    freeaddrinfo(res);
    Listener listener;
    listener.fd = fd;
    listener.host = host;
    listener.port = port;
    listener.servers.push_back(server);
    _listeners.push_back(listener);
    std::cout << "listening on http://" << where << std::endl;
}

void Server::run()
{
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    while (g_running)
    {
        std::vector<struct pollfd> fds;
        std::vector<Slot> slots;
        for (size_t i = 0; i < _listeners.size(); ++i)
        {
            struct pollfd p = {_listeners[i].fd, POLLIN, 0};
            Slot s = {Slot::LISTENER, i, -1, 0};
            fds.push_back(p);
            slots.push_back(s);
        }
        for (std::map<int, Client *>::iterator it = _clients.begin(); it != _clients.end(); ++it)
        {
            Client *c = it->second;
            short events = 0;
            if (c->phase == Client::READING)
                events = POLLIN;
            else if (c->phase == Client::WRITING)
                events = POLLOUT;
            struct pollfd p = {c->fd, events, 0};
            Slot s = {Slot::CLIENT, c->listener, c->fd, c->id};
            fds.push_back(p);
            slots.push_back(s);
            if (c->cgi != NULL && c->cgi->inputFd() >= 0)
            {
                struct pollfd q = {c->cgi->inputFd(), POLLOUT, 0};
                Slot t = {Slot::CGI_IN, c->listener, c->fd, c->id};
                fds.push_back(q);
                slots.push_back(t);
            }
            if (c->cgi != NULL && c->cgi->outputFd() >= 0)
            {
                struct pollfd q = {c->cgi->outputFd(), POLLIN, 0};
                Slot t = {Slot::CGI_OUT, c->listener, c->fd, c->id};
                fds.push_back(q);
                slots.push_back(t);
            }
        }

        if (poll(&fds[0], fds.size(), 1000) < 0)
            continue;       // interrupted by a signal: the loop condition decides

        for (size_t i = 0; i < fds.size(); ++i)
        {
            short ev = fds[i].revents;
            if (ev == 0)
                continue;
            if (slots[i].kind == Slot::LISTENER)
            {
                if (ev & POLLIN)
                    acceptClient(slots[i].listener);
                continue;
            }
            Client *c = findClient(slots[i].clientFd, slots[i].clientId);
            if (c == NULL)
                continue;   // closed earlier in this round
            if (slots[i].kind == Slot::CLIENT)
            {
                if (ev & (POLLERR | POLLNVAL))
                    closeClient(c);
                else if (c->phase == Client::READING && (ev & (POLLIN | POLLHUP)))
                    onReadable(c);
                else if (c->phase == Client::WRITING && (ev & POLLOUT))
                    onWritable(c);
                else if (ev & POLLHUP)
                    closeClient(c);
            }
            else if (c->cgi == NULL)
                continue;
            else if (slots[i].kind == Slot::CGI_IN && c->cgi->inputFd() == fds[i].fd)
                c->cgi->writeInput();
            else if (slots[i].kind == Slot::CGI_OUT && c->cgi->outputFd() == fds[i].fd)
            {
                c->cgi->readOutput();
                if (c->cgi->done())
                    finishCgi(c);
            }
        }
        checkTimeouts();
    }
    std::cout << "\nwebserv stopped" << std::endl;
}

void Server::acceptClient(size_t index)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int fd = accept(_listeners[index].fd, reinterpret_cast<struct sockaddr *>(&addr), &len);
    if (fd < 0)
        return;
    if (_clients.size() >= kMaxClients || fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
    {
        close(fd);
        return;
    }
    _clients[fd] = new Client(fd, _nextId++, index, ipToString(addr));
}

Client *Server::findClient(int fd, unsigned long id)
{
    std::map<int, Client *>::iterator it = _clients.find(fd);
    if (it == _clients.end() || it->second->id != id)
        return NULL;
    return it->second;
}

void Server::onReadable(Client *c)
{
    char buf[65536];
    ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
    if (n <= 0)
    {
        closeClient(c);     // 0: the client hung up, -1: the connection broke
        return;
    }
    c->lastActivity = std::time(NULL);
    if (c->draining)
    {
        if (static_cast<size_t>(n) >= c->drainLeft)
            closeClient(c);
        else
            c->drainLeft -= static_cast<size_t>(n);
        return;
    }
    c->inBuffer.append(buf, static_cast<size_t>(n));
    advance(c);
}

// Runs the parser on what has arrived and answers once a request is complete.
void Server::advance(Client *c)
{
    c->request.feed(c->inBuffer);
    if (c->request.needsBodyLimit())
    {
        c->server = pickServer(c);
        const LocationConfig *loc = c->server->matchLocation(c->request.path());
        c->request.setBodyLimit((loc != NULL) ? loc->maxBodySize : c->server->maxBodySize);
        c->request.feed(c->inBuffer);
    }
    Request::State state = c->request.state();
    if (state == Request::DONE || state == Request::ERROR)
        dispatch(c);
}

void Server::dispatch(Client *c)
{
    if (c->server == NULL)
        c->server = pickServer(c);
    Response res;
    CgiJob job;
    if (handler::handle(*c->server, c->request, res, job))
    {
        reply(c, res, false);
        return;
    }
    Cgi *cgi = new Cgi();
    if (!cgi->start(job, c->request, *c->server, c->ip, _listeners[c->listener].port))
    {
        delete cgi;
        Response error = handler::errorPage(c->server, 500);
        reply(c, error, false);
        return;
    }
    c->cgi = cgi;
    c->phase = Client::RUNNING_CGI;
}

void Server::finishCgi(Client *c)
{
    Response res = c->cgi->response();
    delete c->cgi;
    c->cgi = NULL;
    if (res.status >= 400 && res.body.empty())
        res = handler::errorPage(c->server, res.status);
    reply(c, res, false);
}

void Server::reply(Client *c, Response &res, bool forceClose)
{
    const Request &req = c->request;
    bool keepAlive = !forceClose && g_running && req.state() == Request::DONE && req.keepAlive();
    c->outBuffer = res.serialize(keepAlive, req.method() == "HEAD");
    c->sent = 0;
    c->closeAfterWrite = !keepAlive;
    c->phase = Client::WRITING;
    c->lastActivity = std::time(NULL);
    if (req.state() == Request::ERROR && req.error() == 413)
        prepareDrain(c);
    std::cout << timeOfDay() << "  " << c->ip << "  "
              << (req.method().empty() ? "-" : req.method()) << " "
              << (req.target().empty() ? "-" : req.target()) << "  ->  "
              << res.status << " " << Response::reason(res.status) << std::endl;
}

// After a 413 the client is usually still sending its body. Closing at once
// would make the kernel reset the connection and the client could lose our
// answer, so the rest of the body is read and thrown away first.
void Server::prepareDrain(Client *c)
{
    long length = 0;
    size_t pending = c->inBuffer.size();
    c->inBuffer.clear();
    if (!utils::parseLong(c->request.header("content-length"), length))
        c->drainLeft = kMaxDrain;           // chunked: until the client stops sending
    else if (static_cast<size_t>(length) > pending)
        c->drainLeft = static_cast<size_t>(length) - pending;
    else
        c->drainLeft = 0;
    c->draining = (c->drainLeft > 0 && c->drainLeft <= kMaxDrain);
}

void Server::onWritable(Client *c)
{
    ssize_t n = send(c->fd, c->outBuffer.data() + c->sent, c->outBuffer.size() - c->sent, 0);
    if (n <= 0)
    {
        closeClient(c);
        return;
    }
    c->sent += static_cast<size_t>(n);
    c->lastActivity = std::time(NULL);
    if (c->sent < c->outBuffer.size())
        return;
    c->outBuffer.clear();
    c->sent = 0;
    if (c->closeAfterWrite)
    {
        if (c->draining)
            c->phase = Client::READING;     // answer sent, now swallow the rest of the body
        else
            closeClient(c);
        return;
    }
    c->request.reset();
    c->server = NULL;
    c->phase = Client::READING;
    if (!c->inBuffer.empty())
        advance(c);         // a pipelined request is already waiting
}

void Server::closeClient(Client *c)
{
    if (c->cgi != NULL)
    {
        c->cgi->stop();
        delete c->cgi;
    }
    close(c->fd);
    _clients.erase(c->fd);
    delete c;
}

void Server::checkTimeouts()
{
    time_t now = std::time(NULL);
    std::vector<Client *> expired;
    for (std::map<int, Client *>::iterator it = _clients.begin(); it != _clients.end(); ++it)
    {
        Client *c = it->second;
        if (c->phase == Client::RUNNING_CGI)
        {
            if (!c->cgi->expired(now))
                continue;
            c->cgi->stop();
            delete c->cgi;
            c->cgi = NULL;
            Response res = handler::errorPage(c->server, 504);
            reply(c, res, true);
            continue;
        }
        time_t limit = c->draining ? kDrainTimeout : kIdleTimeout;
        if (now - c->lastActivity <= limit)
            continue;
        bool halfRequest = c->phase == Client::READING && !c->draining
                           && (!c->inBuffer.empty() || c->request.state() != Request::HEADERS);
        if (halfRequest)
        {
            Response res = handler::errorPage(c->server, 408);
            reply(c, res, true);
        }
        else
            expired.push_back(c);
    }
    for (size_t i = 0; i < expired.size(); ++i)
        closeClient(expired[i]);
}

// Virtual hosts: the Host header picks the server block, the first one is the default.
const ServerConfig *Server::pickServer(const Client *c) const
{
    const Listener &l = _listeners[c->listener];
    std::string host = utils::toLower(c->request.header("host"));
    size_t colon = host.rfind(':');
    if (colon != std::string::npos)
        host.erase(colon);
    for (size_t s = 0; s < l.servers.size(); ++s)
        for (size_t n = 0; n < l.servers[s]->serverNames.size(); ++n)
            if (utils::toLower(l.servers[s]->serverNames[n]) == host)
                return l.servers[s];
    return l.servers[0];
}
