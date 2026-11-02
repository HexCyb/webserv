#include "Handler.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{

const char kStyle[] =
    "body{font:15px/1.6 ui-monospace,Menlo,Consolas,monospace;background:#0d1117;color:#c9d1d9;"
    "max-width:860px;margin:40px auto;padding:0 16px}"
    "h1{font-size:22px;color:#f0f6fc}a{color:#58a6ff;text-decoration:none}a:hover{text-decoration:underline}"
    "table{border-collapse:collapse;width:100%}"
    "td,th{padding:6px 10px;border-bottom:1px solid #21262d;text-align:left}"
    "th,.muted{color:#8b949e;font-weight:normal}";

unsigned long g_uploadCount = 0;

std::string page(const std::string &title, const std::string &content)
{
    return "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "<title>" + utils::htmlEscape(title) + "</title>\n<style>" + kStyle + "</style>\n</head>\n"
           "<body>\n" + content + "\n</body>\n</html>\n";
}

bool methodAllowed(const LocationConfig &loc, const std::string &method)
{
    if (loc.methods.count(method))
        return true;
    return method == "HEAD" && loc.methods.count("GET");
}

std::string allowList(const LocationConfig &loc, const std::string &except)
{
    std::string out;
    for (std::set<std::string>::const_iterator it = loc.methods.begin(); it != loc.methods.end(); ++it)
        if (*it != except)
            out += (out.empty() ? "" : ", ") + *it;
    return out;
}

Response redirect(int status, const std::string &location)
{
    std::string title = utils::toString(status) + " " + Response::reason(status);
    Response res(status);
    res.setHeader("Location", location);
    res.setHeader("Content-Type", "text/html; charset=utf-8");
    res.body = page(title, "<h1>" + title + "</h1>\n<p>Moved to <a href=\"" + utils::htmlEscape(location)
                    + "\">" + utils::htmlEscape(location) + "</a></p>");
    return res;
}

void serveFile(const ServerConfig &server, const std::string &file, Response &res)
{
    std::string content;
    if (access(file.c_str(), R_OK) != 0 || !utils::readFile(file, content))
    {
        res = handler::errorPage(&server, 403);
        return;
    }
    res = Response(200);
    res.setHeader("Content-Type", utils::mimeType(file));
    res.body = content;
}

// Generated "Index of /path/" page (autoindex on).
void listDirectory(const ServerConfig &server, const std::string &dir, const std::string &urlPath, Response &res)
{
    DIR *handle = opendir(dir.c_str());
    if (handle == NULL)
    {
        res = handler::errorPage(&server, 403);
        return;
    }
    std::vector<std::string> names;
    for (struct dirent *entry = readdir(handle); entry != NULL; entry = readdir(handle))
    {
        std::string name = entry->d_name;
        if (name[0] != '.')         // hides ".", ".." and dotfiles
            names.push_back(name);
    }
    closedir(handle);
    std::sort(names.begin(), names.end());

    std::string rows;
    if (urlPath != "/")
        rows += "<tr><td><a href=\"../\">../</a></td><td></td><td></td></tr>\n";
    for (size_t i = 0; i < names.size(); ++i)
    {
        struct stat st;
        if (stat(utils::joinPath(dir, names[i]).c_str(), &st) != 0)
            continue;
        bool isDir = S_ISDIR(st.st_mode);
        std::string shown = names[i] + (isDir ? "/" : "");
        char when[32] = "";
        struct tm *tm = std::gmtime(&st.st_mtime);
        if (tm != NULL)
            std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", tm);
        rows += "<tr><td><a href=\"" + utils::htmlEscape(utils::urlEncodePath(shown)) + "\">"
              + utils::htmlEscape(shown) + "</a></td><td>"
              + (isDir ? std::string("-") : utils::toString(static_cast<long>(st.st_size)))
              + "</td><td class=\"muted\">" + when + "</td></tr>\n";
    }
    res = Response(200);
    res.setHeader("Content-Type", "text/html; charset=utf-8");
    res.body = page("Index of " + urlPath,
                    "<h1>Index of " + utils::htmlEscape(urlPath) + "</h1>\n<table>\n"
                    "<tr><th>Name</th><th>Size</th><th>Modified (UTC)</th></tr>\n" + rows + "</table>");
}

void handleGet(const ServerConfig &server, const LocationConfig &loc, const Request &req,
               const std::string &file, Response &res)
{
    struct stat st;
    if (stat(file.c_str(), &st) != 0)
    {
        res = handler::errorPage(&server, 404);
        return;
    }
    if (S_ISDIR(st.st_mode))
    {
        const std::string &path = req.path();
        if (path[path.size() - 1] != '/')       // "/files" -> "/files/" so relative links work
        {
            res = redirect(301, path + "/" + (req.query().empty() ? "" : "?" + req.query()));
            return;
        }
        for (size_t i = 0; i < loc.index.size(); ++i)
        {
            std::string candidate = utils::joinPath(file, loc.index[i]);
            if (utils::isFile(candidate))
            {
                serveFile(server, candidate, res);
                return;
            }
        }
        if (loc.autoindex)
            listDirectory(server, file, path, res);
        else
            res = handler::errorPage(&server, 403);
        return;
    }
    if (!S_ISREG(st.st_mode))
    {
        res = handler::errorPage(&server, 403);
        return;
    }
    serveFile(server, file, res);
}

// Keeps only the last part of a client supplied name: "../../x.txt" -> "x.txt"
std::string safeFileName(const std::string &name)
{
    std::string base = name;
    size_t cut = base.find_last_of("/\\");
    if (cut != std::string::npos)
        base = base.substr(cut + 1);
    std::string out;
    for (size_t i = 0; i < base.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(base[i]);
        out += (c < 32 || c == 127) ? '_' : static_cast<char>(c);
    }
    if (out.empty() || out == "." || out == "..")
        return "";
    return out;
}

// Value of `key` in a header such as: form-data; name="file"; filename="a.txt"
std::string headerParam(const std::string &header, const std::string &key)
{
    std::string lower = utils::toLower(header);
    std::string needle = key + "=";
    size_t pos = 0;
    while ((pos = lower.find(needle, pos)) != std::string::npos)
    {
        if (pos == 0 || lower[pos - 1] == ' ' || lower[pos - 1] == ';' || lower[pos - 1] == '\t')
            break;
        pos += needle.size();       // e.g. "name=" inside "filename=": keep looking
    }
    if (pos == std::string::npos)
        return "";
    pos += needle.size();
    if (pos < header.size() && header[pos] == '"')
    {
        size_t end = header.find('"', pos + 1);
        return (end == std::string::npos) ? "" : header.substr(pos + 1, end - pos - 1);
    }
    size_t end = header.find(';', pos);
    return utils::trim(header.substr(pos, (end == std::string::npos) ? std::string::npos : end - pos));
}

// multipart/form-data: every part that carries a filename is saved in `dir`.
int saveMultipart(const std::string &body, const std::string &boundary, const std::string &dir,
                  std::vector<std::string> &saved)
{
    const std::string delimiter = "--" + boundary;
    size_t pos = body.find(delimiter);
    if (pos == std::string::npos)
        return 400;
    pos += delimiter.size();
    while (body.compare(pos, 2, "--") != 0)     // "--" right after a delimiter ends the body
    {
        if (body.compare(pos, 2, "\r\n") != 0)
            return 400;
        size_t headerEnd = body.find("\r\n\r\n", pos);
        if (headerEnd == std::string::npos)
            return 400;
        std::string partHeader = (headerEnd > pos) ? body.substr(pos + 2, headerEnd - pos - 2) : "";
        size_t dataStart = headerEnd + 4;
        size_t dataEnd = body.find("\r\n" + delimiter, dataStart);
        if (dataEnd == std::string::npos)
            return 400;
        std::string filename = headerParam(partHeader, "filename");
        if (!filename.empty())
        {
            std::string name = safeFileName(filename);
            if (name.empty())
                return 400;
            if (!utils::writeFile(utils::joinPath(dir, name), body.substr(dataStart, dataEnd - dataStart)))
                return 500;
            saved.push_back(name);
        }
        pos = dataEnd + 2 + delimiter.size();
    }
    return 0;
}

void handlePost(const ServerConfig &server, const LocationConfig &loc, const Request &req, Response &res)
{
    if (loc.uploadStore.empty())
    {
        res = handler::errorPage(&server, 405);     // nothing here knows what to do with a body
        res.setHeader("Allow", allowList(loc, "POST"));
        return;
    }
    if (!utils::isDirectory(loc.uploadStore))
    {
        res = handler::errorPage(&server, 500);
        return;
    }
    std::vector<std::string> saved;
    std::string type = req.header("content-type");
    if (utils::toLower(type).find("multipart/form-data") == 0)
    {
        std::string boundary = headerParam(type, "boundary");
        int status = boundary.empty() ? 400 : saveMultipart(req.body(), boundary, loc.uploadStore, saved);
        if (status == 0 && saved.empty())
            status = 400;           // a form without any file in it
        if (status != 0)
        {
            res = handler::errorPage(&server, status);
            return;
        }
    }
    else
    {
        // raw body: POST /uploads/notes.txt stores it as <upload_store>/notes.txt
        const std::string &path = req.path();
        std::string name;
        if (path != loc.path && path[path.size() - 1] != '/')
            name = safeFileName(utils::baseName(path));
        if (name.empty())
            name = "upload-" + utils::toString(static_cast<long>(std::time(NULL))) + "-"
                 + utils::toString(static_cast<long>(++g_uploadCount)) + ".bin";
        if (!utils::writeFile(utils::joinPath(loc.uploadStore, name), req.body()))
        {
            res = handler::errorPage(&server, 500);
            return;
        }
        saved.push_back(name);
    }
    std::string list;
    for (size_t i = 0; i < saved.size(); ++i)
    {
        std::string url = utils::urlEncodePath(utils::joinPath(loc.path, saved[i]));
        list += "<li><a href=\"" + utils::htmlEscape(url) + "\">" + utils::htmlEscape(saved[i]) + "</a></li>\n";
    }
    res = Response(201);
    res.setHeader("Location", utils::urlEncodePath(utils::joinPath(loc.path, saved[0])));
    res.setHeader("Content-Type", "text/html; charset=utf-8");
    res.body = page("201 Created", "<h1>201 Created</h1>\n<ul>\n" + list + "</ul>\n<p><a href=\"/\">back</a></p>");
}

void handleDelete(const ServerConfig &server, const std::string &file, Response &res)
{
    struct stat st;
    if (stat(file.c_str(), &st) != 0)
        res = handler::errorPage(&server, 404);
    else if (S_ISDIR(st.st_mode))
        res = handler::errorPage(&server, 409);     // folders are never deleted
    else if (std::remove(file.c_str()) != 0)
        res = handler::errorPage(&server, 403);
    else
        res = Response(204);
}

// "/cgi-bin/hello.py/extra" -> script "/cgi-bin/hello.py", PATH_INFO "/extra"
bool findCgi(const LocationConfig &loc, const std::string &path, CgiJob &job)
{
    if (loc.cgi.empty())
        return false;
    size_t pos = 1;
    while (pos <= path.size())
    {
        size_t slash = path.find('/', pos);
        size_t end = (slash == std::string::npos) ? path.size() : slash;
        std::string script = path.substr(0, end);
        std::map<std::string, std::string>::const_iterator it = loc.cgi.find(utils::extension(script));
        if (it != loc.cgi.end())
        {
            job.interpreter = it->second;
            job.scriptName = script;
            job.pathInfo = path.substr(end);
            job.scriptPath = utils::joinPath(loc.root, script);
            return true;
        }
        if (slash == std::string::npos)
            break;
        pos = slash + 1;
    }
    return false;
}

}

namespace handler
{

Response errorPage(const ServerConfig *server, int status)
{
    Response res(status);
    res.setHeader("Content-Type", "text/html; charset=utf-8");
    if (server != NULL)
    {
        std::map<int, std::string>::const_iterator it = server->errorPages.find(status);
        if (it != server->errorPages.end() && utils::readFile(utils::joinPath(server->root, it->second), res.body))
            return res;
    }
    std::string title = utils::toString(status) + " " + Response::reason(status);
    res.body = page(title, "<h1>" + title + "</h1>\n<p class=\"muted\">webserv</p>");
    return res;
}

bool handle(const ServerConfig &server, const Request &req, Response &res, CgiJob &job)
{
    if (req.state() == Request::ERROR)
    {
        res = errorPage(&server, req.error());
        return true;
    }
    const std::string &method = req.method();
    if (method != "GET" && method != "HEAD" && method != "POST" && method != "DELETE")
    {
        res = errorPage(&server, 501);
        return true;
    }
    const LocationConfig *loc = server.matchLocation(req.path());
    if (loc == NULL)
    {
        res = errorPage(&server, 404);
        return true;
    }
    if (loc->redirectCode != 0)
    {
        res = redirect(loc->redirectCode, loc->redirectUrl);
        return true;
    }
    if (!methodAllowed(*loc, method))
    {
        res = errorPage(&server, 405);
        res.setHeader("Allow", allowList(*loc, ""));
        return true;
    }
    if (method != "DELETE" && findCgi(*loc, req.path(), job))
    {
        if (!utils::isFile(job.scriptPath))
        {
            res = errorPage(&server, 404);
            return true;
        }
        return false;
    }
    std::string file = utils::joinPath(loc->root, req.path());
    if (method == "GET" || method == "HEAD")
        handleGet(server, *loc, req, file, res);
    else if (method == "POST")
        handlePost(server, *loc, req, res);
    else
        handleDelete(server, file, res);
    return true;
}

}
