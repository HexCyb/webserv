#include "Config.hpp"
#include "Server.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc > 2)
    {
        std::cerr << "usage: ./webserv [configuration file]" << std::endl;
        return 1;
    }
    try
    {
        Config config(argc == 2 ? argv[1] : "conf/default.conf");
        Server server(config);
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "webserv: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
