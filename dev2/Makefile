# Makefile for UDP Client and Server

CXX = g++
CXXFLAGS = -Wall -std=c++11

CLIENT = CFTPclient
SERVER = CFTPserver

all: $(CLIENT) $(SERVER)

$(CLIENT): CFTPclient.cpp
	$(CXX) $(CXXFLAGS) -o $(CLIENT) CFTPclient.cpp

$(SERVER): CFTPserver.cpp
	$(CXX) $(CXXFLAGS) -o $(SERVER) CFTPserver.cpp

clean:
	rm -f $(CLIENT) $(SERVER)
