CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -pthread
QT_PACKAGES ?= Qt6Widgets Qt6Network
QT_CXXFLAGS = $(shell pkg-config --cflags $(QT_PACKAGES) 2>/dev/null)
QT_LIBS = $(shell pkg-config --libs $(QT_PACKAGES) 2>/dev/null)

.PHONY: all clean gui

all: server client technician

server: src/server.cpp src/chat.hpp
	$(CXX) $(CXXFLAGS) src/server.cpp -o server

client: src/client.cpp src/chat.hpp
	$(CXX) $(CXXFLAGS) src/client.cpp -o client

technician: src/technician.cpp src/chat.hpp
	$(CXX) $(CXXFLAGS) src/technician.cpp -o technician

gui: src/gui.cpp
	@pkg-config --exists $(QT_PACKAGES) || { echo "Qt development packages not found: $(QT_PACKAGES)"; exit 1; }
	$(CXX) $(CXXFLAGS) -fPIC $(QT_CXXFLAGS) src/gui.cpp -o chat_gui $(QT_LIBS)

clean:
	rm -f server client technician chat_gui
