#ifndef UTILS_HPP
# define UTILS_HPP

# include <cstddef>
# include <ctime>
# include <string>
# include <vector>

// Small string, path and file helpers shared by the whole server.
namespace utils
{
    std::string                 toString(long n);
    std::string                 toLower(const std::string &s);
    std::string                 trim(const std::string &s);
    std::vector<std::string>    split(const std::string &s, char sep);

    bool        parseLong(const std::string &s, long &out);     // digits only, no sign
    bool        parseSize(const std::string &s, size_t &out);   // "512", "10K", "8M", "1G"
    bool        parseHex(const std::string &s, size_t &out);    // chunk sizes

    std::string urlDecode(const std::string &s, bool &ok);
    std::string urlEncodePath(const std::string &s);
    std::string normalizePath(const std::string &path, bool &ok);
    std::string joinPath(const std::string &a, const std::string &b);
    std::string baseName(const std::string &path);
    std::string dirName(const std::string &path);
    std::string extension(const std::string &path);

    std::string mimeType(const std::string &path);
    std::string httpDate(time_t t);
    std::string htmlEscape(const std::string &s);

    bool        readFile(const std::string &path, std::string &out);
    bool        writeFile(const std::string &path, const std::string &data);
    bool        isDirectory(const std::string &path);
    bool        isFile(const std::string &path);
}

#endif
