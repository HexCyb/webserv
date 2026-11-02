#ifndef RESPONSE_HPP
# define RESPONSE_HPP

# include <string>
# include <utility>
# include <vector>

class Response
{
public:
    explicit Response(int status = 200);

    int         status;
    std::string message;    // reason phrase, empty = the standard one
    std::string body;

    void        setHeader(const std::string &name, const std::string &value);
    std::string header(const std::string &name) const;
    std::string serialize(bool keepAlive, bool headOnly) const;

    static std::string reason(int status);

private:
    std::vector<std::pair<std::string, std::string> > _headers;
};

#endif
