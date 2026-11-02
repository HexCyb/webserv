#include "Config.hpp"
#include "utils.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace
{
    bool knownMethod(const std::string &m)
    {
        return m == "GET" || m == "HEAD" || m == "POST" || m == "DELETE";
    }

    bool onOff(const std::string &value, bool &out)
    {
        if (value != "on" && value != "off")
            return false;
        out = (value == "on");
        return true;
    }
}

LocationConfig::LocationConfig()
    : autoindex(false), redirectCode(0), maxBodySize(0),
      hasRoot(false), hasIndex(false), hasAutoindex(false), hasMethods(false), hasMaxBody(false)
{
}

ServerConfig::ServerConfig() : root("./www"), autoindex(false), maxBodySize(1024 * 1024)
{
}

// The longest matching prefix wins, and only on whole path segments:
// "/images" matches "/images" and "/images/cat.png" but not "/imagesX".
const LocationConfig *ServerConfig::matchLocation(const std::string &path) const
{
    const LocationConfig *best = NULL;
    for (size_t i = 0; i < locations.size(); ++i)
    {
        const std::string &prefix = locations[i].path;
        if (path.compare(0, prefix.size(), prefix) != 0)
            continue;
        if (prefix != "/" && path.size() > prefix.size() && path[prefix.size()] != '/')
            continue;
        if (best == NULL || prefix.size() > best->path.size())
            best = &locations[i];
    }
    return best;
}

Config::Config(const std::string &file) : _pos(0), _line(0)
{
    std::ifstream in(file.c_str());
    if (!in)
        throw std::runtime_error("cannot open config file '" + file + "'");
    std::ostringstream content;
    content << in.rdbuf();
    tokenize(content.str());
    while (!atEnd())
    {
        if (next() != "server")
            fail("expected a 'server' block");
        parseServer();
    }
    if (_servers.empty())
        throw std::runtime_error("no 'server' block in '" + file + "'");
}

const std::vector<ServerConfig> &Config::servers() const
{
    return _servers;
}

// Splits the file into words, "{", "}" and ";". "#" starts a comment.
void Config::tokenize(const std::string &text)
{
    std::string word;
    int line = 1;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if (c == '#')
        {
            while (i + 1 < text.size() && text[i + 1] != '\n')
                ++i;
            continue;
        }
        bool special = (c == '{' || c == '}' || c == ';');
        if (special || std::isspace(static_cast<unsigned char>(c)))
        {
            if (!word.empty())
            {
                _tokens.push_back(word);
                _lines.push_back(line);
                word.clear();
            }
            if (special)
            {
                _tokens.push_back(std::string(1, c));
                _lines.push_back(line);
            }
            if (c == '\n')
                ++line;
            continue;
        }
        word += c;
    }
    if (!word.empty())
    {
        _tokens.push_back(word);
        _lines.push_back(line);
    }
}

bool Config::atEnd() const
{
    return _pos >= _tokens.size();
}

const std::string &Config::next()
{
    if (atEnd())
        fail("unexpected end of file (missing '}' ?)");
    _line = _lines[_pos];
    return _tokens[_pos++];
}

// Reads the values of a directive up to its ";".
std::vector<std::string> Config::arguments(const std::string &directive)
{
    std::vector<std::string> args;
    while (true)
    {
        const std::string &token = next();
        if (token == ";")
            break;
        if (token == "{" || token == "}")
            fail("missing ';' after '" + directive + "'");
        args.push_back(token);
    }
    if (args.empty())
        fail("'" + directive + "' needs a value");
    return args;
}

void Config::parseServer()
{
    if (next() != "{")
        fail("expected '{' after 'server'");
    ServerConfig server;
    while (true)
    {
        const std::string key = next();
        if (key == "}")
            break;
        if (key == "location")
        {
            parseLocation(server);
            continue;
        }
        std::vector<std::string> args = arguments(key);
        if (key == "listen")
            server.listens.push_back(listenFrom(args));
        else if (key == "server_name")
            server.serverNames.insert(server.serverNames.end(), args.begin(), args.end());
        else if (key == "root")
            server.root = single(key, args);
        else if (key == "index")
            server.index = args;
        else if (key == "autoindex")
            server.autoindex = flagFrom(key, args);
        else if (key == "methods" || key == "allow_methods")
            server.methods = methodsFrom(args);
        else if (key == "client_max_body_size")
            server.maxBodySize = sizeFrom(args);
        else if (key == "error_page")
            addErrorPages(server, args);
        else
            fail("unknown directive '" + key + "'");
    }
    finish(server);
    _servers.push_back(server);
}

void Config::parseLocation(ServerConfig &server)
{
    LocationConfig loc;
    loc.path = next();
    if (loc.path[0] != '/')
        fail("a location path must start with '/'");
    while (loc.path.size() > 1 && loc.path[loc.path.size() - 1] == '/')
        loc.path.erase(loc.path.size() - 1);
    if (next() != "{")
        fail("expected '{' after 'location " + loc.path + "'");
    while (true)
    {
        const std::string key = next();
        if (key == "}")
            break;
        if (key == "location" || key == "server")
            fail("'" + key + "' is not allowed inside a location");
        std::vector<std::string> args = arguments(key);
        if (key == "root")
        {
            loc.root = single(key, args);
            loc.hasRoot = true;
        }
        else if (key == "index")
        {
            loc.index = args;
            loc.hasIndex = true;
        }
        else if (key == "autoindex")
        {
            loc.autoindex = flagFrom(key, args);
            loc.hasAutoindex = true;
        }
        else if (key == "methods" || key == "allow_methods")
        {
            loc.methods = methodsFrom(args);
            loc.hasMethods = true;
        }
        else if (key == "client_max_body_size")
        {
            loc.maxBodySize = sizeFrom(args);
            loc.hasMaxBody = true;
        }
        else if (key == "return")
        {
            long code = 0;
            if (args.size() != 2 || !utils::parseLong(args[0], code)
                || (code != 301 && code != 302 && code != 303 && code != 307 && code != 308))
                fail("usage: return 301|302|303|307|308 <url>;");
            loc.redirectCode = static_cast<int>(code);
            loc.redirectUrl = args[1];
        }
        else if (key == "upload_store")
            loc.uploadStore = single(key, args);
        else if (key == "cgi")
        {
            if (args.size() != 2 || args[0].size() < 2 || args[0][0] != '.')
                fail("usage: cgi .<extension> <interpreter>;");
            loc.cgi[utils::toLower(args[0])] = args[1];
        }
        else
            fail("unknown directive '" + key + "' in location " + loc.path);
    }
    for (size_t i = 0; i < server.locations.size(); ++i)
        if (server.locations[i].path == loc.path)
            fail("duplicate location '" + loc.path + "'");
    server.locations.push_back(loc);
}

// Fills defaults and copies server values into locations that did not set them.
void Config::finish(ServerConfig &server) const
{
    if (server.listens.empty())
        server.listens.push_back(std::make_pair(std::string("0.0.0.0"), 8080));
    if (server.index.empty())
        server.index.push_back("index.html");
    if (server.methods.empty())
    {
        server.methods.insert("GET");
        server.methods.insert("HEAD");
    }
    bool hasRootLocation = false;
    for (size_t i = 0; i < server.locations.size(); ++i)
        if (server.locations[i].path == "/")
            hasRootLocation = true;
    if (!hasRootLocation)
    {
        LocationConfig loc;
        loc.path = "/";
        server.locations.push_back(loc);
    }
    for (size_t i = 0; i < server.locations.size(); ++i)
    {
        LocationConfig &loc = server.locations[i];
        if (!loc.hasRoot)
            loc.root = server.root;
        if (!loc.hasIndex)
            loc.index = server.index;
        if (!loc.hasAutoindex)
            loc.autoindex = server.autoindex;
        if (!loc.hasMethods)
            loc.methods = server.methods;
        if (!loc.hasMaxBody)
            loc.maxBodySize = server.maxBodySize;
    }
}

std::string Config::single(const std::string &key, const std::vector<std::string> &args) const
{
    if (args.size() != 1)
        fail("'" + key + "' takes exactly one value");
    return args[0];
}

bool Config::flagFrom(const std::string &key, const std::vector<std::string> &args) const
{
    bool value = false;
    if (args.size() != 1 || !onOff(args[0], value))
        fail("usage: " + key + " on|off;");
    return value;
}

std::pair<std::string, int> Config::listenFrom(const std::vector<std::string> &args) const
{
    if (args.size() != 1)
        fail("usage: listen [host:]port;");
    std::string host = "0.0.0.0";
    std::string port = args[0];
    size_t colon = args[0].rfind(':');
    if (colon != std::string::npos)
    {
        host = args[0].substr(0, colon);
        port = args[0].substr(colon + 1);
    }
    if (host == "localhost")
        host = "127.0.0.1";
    else if (host == "*")
        host = "0.0.0.0";
    long number = 0;
    if (host.empty() || !utils::parseLong(port, number) || number < 1 || number > 65535)
        fail("invalid listen address '" + args[0] + "'");
    return std::make_pair(host, static_cast<int>(number));
}

std::set<std::string> Config::methodsFrom(const std::vector<std::string> &args) const
{
    std::set<std::string> methods;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (!knownMethod(args[i]))
            fail("unknown method '" + args[i] + "' (GET, HEAD, POST, DELETE)");
        methods.insert(args[i]);
    }
    return methods;
}

size_t Config::sizeFrom(const std::vector<std::string> &args) const
{
    size_t size = 0;
    if (args.size() != 1 || !utils::parseSize(args[0], size))
        fail("usage: client_max_body_size <number>[K|M|G];");
    return size;
}

void Config::addErrorPages(ServerConfig &server, const std::vector<std::string> &args) const
{
    if (args.size() < 2)
        fail("usage: error_page <code>... <file>;");
    for (size_t i = 0; i + 1 < args.size(); ++i)
    {
        long code = 0;
        if (!utils::parseLong(args[i], code) || code < 300 || code > 599)
            fail("invalid error_page code '" + args[i] + "'");
        server.errorPages[static_cast<int>(code)] = args.back();
    }
}

void Config::fail(const std::string &message) const
{
    throw std::runtime_error("config line " + utils::toString(_line) + ": " + message);
}
