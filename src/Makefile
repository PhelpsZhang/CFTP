CXX = g++
CXXFLAGS = -Wall -std=c++11

# source file
CLIENT_SRC = CFTPclient.cpp
SERVER_SRC = CFTPserver.cpp

# header file
CLIENT_HDR = CFTPclient.h
SERVER_HDR = CFTPserver.h

# executable file
CLIENT_EXEC = CFTPclient
SERVER_EXEC = CFTPserver

# generate all
all: $(CLIENT_EXEC) $(SERVER_EXEC)

# Client 
$(CLIENT_EXEC): $(CLIENT_SRC) $(CLIENT_HDR)
	$(CXX) $(CXXFLAGS) -o $(CLIENT_EXEC) $(CLIENT_SRC)

# Server
$(SERVER_EXEC): $(SERVER_SRC) $(SERVER_HDR)
	$(CXX) $(CXXFLAGS) -o $(SERVER_EXEC) $(SERVER_SRC)

# clean 
clean:
	rm -f $(CLIENT_EXEC) $(SERVER_EXEC)
