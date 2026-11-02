#ifndef SERVER_HPP
# define SERVER_HPP

# include "Cgi.hpp"
# include "Config.hpp"
# include "Request.hpp"
# include "Response.hpp"

# include <ctime>
# include <map>
# include <string>
# include <vector>

// One connected browser / curl. Lives from accept() to close().
struct Client
{
    enum Phase { READING, RUNNING_CGI, WRITING };

    Client(int fd, unsigned long id, size_t listener, const std::string &ip);

    int                 fd;
    unsigned long       id;             // fds get reused, ids never do
    size_t              listener;       // which listening socket accepted it
    std::string         ip;
    std::string         inBuffer;
    std::string         outBuffer;
    size_t              sent;
    Request             request;
    const ServerConfig  *server;        // chosen with the Host header
    Phase               phase;
    bool                closeAfterWrite;
    bool                draining;       // reading and dropping the rest of a refused (413) body
    size_t              drainLeft;
    Cgi                 *cgi;
    time_t              lastActivity;
};

// The event loop: one poll() for every listening socket, client and CGI pipe.
class Server
{
public:
    explicit Server(const Config &config);
    ~Server();

    void run();

private:
    struct Listener
    {
        int                                 fd;
        std::string                         host;
        int                                 port;
        std::vector<const ServerConfig *>   servers;    // the first one is the default
    };
    struct Slot     // what a pollfd entry belongs to
    {
        enum Kind { LISTENER, CLIENT, CGI_IN, CGI_OUT };

        Kind            kind;
        size_t          listener;
        int             clientFd;
        unsigned long   clientId;
    };

    std::vector<Listener>       _listeners;
    std::map<int, Client *>     _clients;
    unsigned long               _nextId;

    void                openListener(const std::string &host, int port, const ServerConfig *server);
    void                acceptClient(size_t index);
    Client              *findClient(int fd, unsigned long id);
    void                onReadable(Client *client);
    void                onWritable(Client *client);
    void                advance(Client *client);
    void                dispatch(Client *client);
    void                finishCgi(Client *client);
    void                reply(Client *client, Response &response, bool forceClose);
    void                prepareDrain(Client *client);
    void                closeClient(Client *client);
    void                checkTimeouts();
    const ServerConfig  *pickServer(const Client *client) const;

    Server(const Server &);
    Server &operator=(const Server &);
};

#endif
