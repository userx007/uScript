# Makefile for the unified loopback tool.
#
#   make            build ./loopback
#   make clean      remove build output
#
# raweth: (AF_PACKET raw sockets) needs CAP_NET_RAW to run:
#   sudo ./loopback -i raweth:eth0 ...
# or, once:
#   sudo setcap cap_net_raw+ep ./loopback

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
TARGET   := loopback
SRC      := loopback.cpp

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(SRC) *.hpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
