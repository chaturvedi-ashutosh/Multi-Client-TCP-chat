CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -pthread

.PHONY: all clean

all: server client technician

server: src/server.cpp src/chat.hpp
	$(CXX) $(CXXFLAGS) src/server.cpp -o server

client: src/client.cpp src/chat.hpp
	$(CXX) $(CXXFLAGS) src/client.cpp -o client

technician: src/technician.cpp src/chat.hpp
	$(CXX) $(CXXFLAGS) src/technician.cpp -o technician

clean:
	rm -f server client technician
