CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -pthread
all: server
server: server.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o server
clean:
	rm -f server
