#include "utils.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace
{
    int hexValue(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }
}

namespace utils
{

std::string toString(long n)
{
    std::ostringstream out;
    out << n;
    return out.str();
}

std::string toLower(const std::string &s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    return out;
}

std::string trim(const std::string &s)
{
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string> split(const std::string &s, char sep)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true)
    {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos)
            break;
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    parts.push_back(s.substr(start));
    return parts;
}

bool parseLong(const std::string &s, long &out)
{
    if (s.empty() || s.size() > 18)
        return false;
    long value = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
        value = value * 10 + (s[i] - '0');
    }
    out = value;
    return true;
}

bool parseSize(const std::string &s, size_t &out)
{
    if (s.empty())
        return false;
    std::string digits = s;
    size_t unit = 1;
    char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(s[s.size() - 1])));
    if (suffix == 'K' || suffix == 'M' || suffix == 'G')
    {
        digits.erase(digits.size() - 1);
        unit = (suffix == 'K') ? 1024UL : (suffix == 'M') ? 1024UL * 1024 : 1024UL * 1024 * 1024;
    }
    long value = 0;
    if (digits.size() > 10 || !parseLong(digits, value))
        return false;
    out = static_cast<size_t>(value) * unit;
    return true;
}

bool parseHex(const std::string &s, size_t &out)
{
    if (s.empty() || s.size() > 15)
        return false;
    size_t value = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        int digit = hexValue(s[i]);
        if (digit < 0)
            return false;
        value = value * 16 + static_cast<size_t>(digit);
    }
    out = value;
    return true;
}

// "%20" -> " ". Rejects broken escapes and %00.
std::string urlDecode(const std::string &s, bool &ok)
{
    std::string out;
    ok = true;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '%')
        {
            out += s[i];
            continue;
        }
        int high = (i + 2 < s.size()) ? hexValue(s[i + 1]) : -1;
        int low = (i + 2 < s.size()) ? hexValue(s[i + 2]) : -1;
        if (high < 0 || low < 0 || (high == 0 && low == 0))
        {
            ok = false;
            return "";
        }
        out += static_cast<char>(high * 16 + low);
        i += 2;
    }
    return out;
}

std::string urlEncodePath(const std::string &s)
{
    const char *hex = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// "/a/./b/../c/" -> "/a/c/". Fails when ".." would climb above "/".
std::string normalizePath(const std::string &path, bool &ok)
{
    ok = false;
    if (path.empty() || path[0] != '/')
        return "";
    std::vector<std::string> parts = split(path, '/');
    std::vector<std::string> kept;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].empty() || parts[i] == ".")
            continue;
        if (parts[i] == "..")
        {
            if (kept.empty())
                return "";
            kept.pop_back();
        }
        else
            kept.push_back(parts[i]);
    }
    std::string out;
    for (size_t i = 0; i < kept.size(); ++i)
        out += "/" + kept[i];
    if (out.empty())
        out = "/";
    else if (path[path.size() - 1] == '/')
        out += "/";
    ok = true;
    return out;
}

// joinPath("./www", "/files/") -> "./www/files/"
std::string joinPath(const std::string &a, const std::string &b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    std::string left = a;
    while (left.size() > 1 && left[left.size() - 1] == '/')
        left.erase(left.size() - 1);
    size_t start = b.find_first_not_of('/');
    std::string right = (start == std::string::npos) ? "" : b.substr(start);
    if (left == "/")
        return "/" + right;
    return left + "/" + right;
}

std::string baseName(const std::string &path)
{
    std::string p = path;
    while (p.size() > 1 && p[p.size() - 1] == '/')
        p.erase(p.size() - 1);
    size_t slash = p.rfind('/');
    return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

std::string dirName(const std::string &path)
{
    size_t slash = path.rfind('/');
    if (slash == std::string::npos)
        return ".";
    if (slash == 0)
        return "/";
    return path.substr(0, slash);
}

std::string extension(const std::string &path)
{
    std::string name = baseName(path);
    size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0)
        return "";
    return toLower(name.substr(dot));
}

std::string mimeType(const std::string &path)
{
    static const char *table[][2] = {
        {".html", "text/html; charset=utf-8"},  {".htm", "text/html; charset=utf-8"},
        {".css", "text/css"},                   {".js", "application/javascript"},
        {".json", "application/json"},          {".txt", "text/plain; charset=utf-8"},
        {".md", "text/plain; charset=utf-8"},   {".xml", "application/xml"},
        {".png", "image/png"},                  {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},                {".gif", "image/gif"},
        {".svg", "image/svg+xml"},              {".ico", "image/x-icon"},
        {".webp", "image/webp"},                {".pdf", "application/pdf"},
        {".zip", "application/zip"},            {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"},                  {".woff2", "font/woff2"}
    };
    std::string ext = extension(path);
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
        if (ext == table[i][0])
            return table[i][1];
    return "application/octet-stream";
}

std::string httpDate(time_t t)
{
    char buf[64];
    struct tm *gmt = std::gmtime(&t);
    if (gmt == NULL || std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmt) == 0)
        return "";
    return buf;
}

std::string htmlEscape(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        switch (s[i])
        {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += s[i];
        }
    }
    return out;
}

bool readFile(const std::string &path, std::string &out)
{
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in)
        return false;
    std::ostringstream content;
    content << in.rdbuf();
    if (in.bad())
        return false;
    out = content.str();
    return true;
}

bool writeFile(const std::string &path, const std::string &data)
{
    std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool isDirectory(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

}
