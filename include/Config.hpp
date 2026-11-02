#ifndef CONFIG_HPP
# define CONFIG_HPP

# include <cstddef>
# include <map>
# include <set>
# include <string>
# include <utility>
# include <vector>

// One "location /path { ... }" block. Values that are not written in the
// block are inherited from the server block when the file is loaded.
struct LocationConfig
{
    std::string                         path;
    std::string                         root;
    std::vector<std::string>            index;
    bool                                autoindex;
    std::set<std::string>               methods;
    int                                 redirectCode;   // 0 = no "return"
    std::string                         redirectUrl;
    std::string                         uploadStore;    // empty = uploads disabled
    std::map<std::string, std::string>  cgi;            // ".py" -> "/usr/bin/python3"
    size_t                              maxBodySize;    // 0 = unlimited

    bool    hasRoot;
    bool    hasIndex;
    bool    hasAutoindex;
    bool    hasMethods;
    bool    hasMaxBody;

    LocationConfig();
};

// One "server { ... }" block.
struct ServerConfig
{
    std::vector<std::pair<std::string, int> >   listens;        // host, port
    std::vector<std::string>                    serverNames;
    std::string                                 root;
    std::vector<std::string>                    index;
    bool                                        autoindex;
    std::set<std::string>                       methods;
    size_t                                      maxBodySize;
    std::map<int, std::string>                  errorPages;     // 404 -> "/errors/404.html"
    std::vector<LocationConfig>                 locations;

    ServerConfig();
    const LocationConfig *matchLocation(const std::string &path) const;
};

// Reads an nginx-like configuration file. Throws std::runtime_error with the
// line number when something is wrong.
class Config
{
public:
    explicit Config(const std::string &file);

    const std::vector<ServerConfig> &servers() const;

private:
    std::vector<ServerConfig>   _servers;
    std::vector<std::string>    _tokens;
    std::vector<int>            _lines;
    size_t                      _pos;
    int                         _line;

    void                        tokenize(const std::string &text);
    bool                        atEnd() const;
    const std::string           &next();
    std::vector<std::string>    arguments(const std::string &directive);
    void                        parseServer();
    void                        parseLocation(ServerConfig &server);
    void                        finish(ServerConfig &server) const;

    std::string                 single(const std::string &key, const std::vector<std::string> &args) const;
    bool                        flagFrom(const std::string &key, const std::vector<std::string> &args) const;
    std::pair<std::string, int> listenFrom(const std::vector<std::string> &args) const;
    std::set<std::string>       methodsFrom(const std::vector<std::string> &args) const;
    size_t                      sizeFrom(const std::vector<std::string> &args) const;
    void                        addErrorPages(ServerConfig &server, const std::vector<std::string> &args) const;
    void                        fail(const std::string &message) const;
};

#endif
