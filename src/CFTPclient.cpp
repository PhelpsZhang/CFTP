#include"CFTPclient.h"

// temporarily, you can modify it!
#define LOCAL_HOST "10.0.1.99"
#define UDP_PORT 44044

#define TARGET_HOST "10.0.2.41"
#define TARGET_PORT 44044

#include<iostream>


int checkParameter(int argc, char* argv[]) {
    // show parameters
    for (int i = 0; i < argc; ++i) {
        std::cout << "Argument " << i << ": " << argv[i] << std::endl;
    }
    return 0;
}

/*
There should be at least 4 parameters:
1. target ip
2. target port number (default xxx)
3. path-to-file
4. target path-file
 */
int main(int argc, char* argv[]) {
    std::cout << "The Client begins running" << std::endl;

    // Check input parameters.
    while (-1 == checkParameter(argc, argv)){};
    // Resolve Parameters.
    std::string target_ip;
    int target_port;
    std::string file_path;
    std::string target_file_path;

    // 1. Create udp Socket
    int udp_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == udp_socket_fd) {
        std::cerr << "UDP socket create failed." << std::endl;
        return -1;
    }
    
    // build the udp socket struct sockaddr_in
    sockaddr_in udp_sock_addr;
    memset(&udp_sock_addr, 0, sizeof(udp_sock_addr));
    udp_sock_addr.sin_family = AF_INET;
    udp_sock_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, LOCAL_HOST, &(udp_sock_addr.sin_addr));
    socklen_t socklen = sizeof(udp_sock_addr);

    // 2. Bind UDP Socket
    if (-1 == bind(udp_socket_fd, (const struct sockaddr *)&udp_sock_addr, sizeof(udp_sock_addr))) {
        std::cerr << "UDP Socket bind Failed." << std::endl;
        close(udp_socket_fd);
        return -1;
    }
    std::cout << "UDP Bind Successful!" << std::endl;
    
    // build remote target udp sockaddr struct
    sockaddr_in target_udp_sock_addr;
    memset(&target_udp_sock_addr, 0, sizeof(target_udp_sock_addr));
    target_udp_sock_addr.sin_family = AF_INET;
    target_udp_sock_addr.sin_port = htons(TARGET_PORT);
    inet_pton(AF_INET, TARGET_HOST, &(target_udp_sock_addr.sin_addr));
    socklen_t tsocklen = sizeof(target_udp_sock_addr);

    std::string test_message = "hello, this is client!";

    // Send messgae to server.
    if (-1 == sendto(udp_socket_fd, test_message.data(), test_message.length(), 0,
             (const struct sockaddr *) &target_udp_sock_addr, tsocklen)) {
        std::cerr << "Failed to send data via UDP. " << strerror(errno) << std::endl;
        close(udp_socket_fd);
        return -1;
    }
    std::cout << "The Client has sent the test Message to the Server." << std::endl;

    sockaddr_in unknown_sock_addr;
    memset(&unknown_sock_addr, 0, sizeof(unknown_sock_addr));
    socklen_t uslen = sizeof(unknown_sock_addr);

    char buffer[1024];
    int recvNum = 0;
    // receive message from server.
    int byteLen = recvfrom(udp_socket_fd, (char*) buffer, 1024, 0,
                (struct sockaddr*)&unknown_sock_addr, &uslen);
    if (byteLen < 0) {
        std::cerr << "recvfrom Error" << std::endl;
    }
    std::string res(buffer, byteLen);
    std::cout << res << std::endl;

    close(udp_socket_fd);
    std::cout << "The Client Ends." << std::endl;
    return 0;
}