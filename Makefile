NAME        := webserv
CXX         := c++
CXXFLAGS    := -Wall -Wextra -Werror -std=c++98 -Iinclude

SRCS        := src/main.cpp src/Config.cpp src/Request.cpp src/Response.cpp \
               src/Handler.cpp src/Cgi.cpp src/Server.cpp src/utils.cpp
OBJS        := $(SRCS:src/%.cpp=obj/%.o)
DEPS        := $(OBJS:.o=.d)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

obj/%.o: src/%.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf obj

fclean: clean
	rm -f $(NAME)

re: fclean
	$(MAKE) all

test: all
	bash tests/run_tests.sh

-include $(DEPS)

.PHONY: all clean fclean re test
