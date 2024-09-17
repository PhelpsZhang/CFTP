#include"CFTPclient.h"
// test
// temporarily, you can modify it!
#define LOCAL_HOST "10.0.1.99"
#define UDP_PORT 44044

#define TARGET_HOST "10.0.2.41"
#define TARGET_PORT 44044

#define CHUNK_SIZE 1400

#include<iostream>

int checkParameter(int argc, char* argv[]) {
    // show parameters
    for (int i = 0; i < argc; ++i) {
        std::cout << "Argument " << i << ": " << argv[i] << std::endl;
    }
    return 0;
}

// Handshake and wait for ACK
bool perform_handshake(int sockfd, struct sockaddr_in& target_addr) {
    const int BUFFER_SIZE = 1024;
    char handshake_message[] = "HELLO";
    char response[BUFFER_SIZE];
    socklen_t addr_len = sizeof(target_addr);

    // 发送握手消息
    ssize_t bytes_sent = sendto(sockfd, handshake_message, strlen(handshake_message), 0,
                                reinterpret_cast<const struct sockaddr*>(&target_addr), addr_len);
    if (bytes_sent < 0) {
        std::cerr << "Error sending handshake message." << std::endl;
        return false;
    }
    std::cout << "Handshake message sent." << std::endl;

    // wait for ACK from receiver
    ssize_t bytes_received = recvfrom(sockfd, response, BUFFER_SIZE, 0,
                                reinterpret_cast<struct sockaddr*>(&target_addr), &addr_len);
    if (bytes_received < 0) {
        std::cerr << "Error receiving handshake confirmation." << std::endl;
        return false;
    }
    response[bytes_received] = '\0';
    // 确保字符串以空字符结束
    std::cout << "Received handshake confirmation: " << response << std::endl;

    return strcmp(response, "ACK") == 0;
}

// 在文件传输结束后，发送一个特殊的 EOF 包通知接收方
int send_eof_packet(int sockfd, const struct sockaddr_in& target_addr) {
    // EOF packetnumber, -1.
    uint32_t eof_packet_number = static_cast<uint32_t>(-1);
    std::vector<char> eof_packet;

    // insert EOF packet_number
    eof_packet.insert(eof_packet.end(), reinterpret_cast<char*>(&eof_packet_number), 
                      reinterpret_cast<char*>(&eof_packet_number) + sizeof(eof_packet_number));

    // send it.
    ssize_t bytes_sent = sendto(sockfd, eof_packet.data(), eof_packet.size(), 0, 
                                reinterpret_cast<const struct sockaddr*>(&target_addr), sizeof(target_addr));
    if (bytes_sent == -1) {
        std::cerr << "Failed to send EOF packet." << std::endl;
    } else {
        std::cout << "EOF packet sent." << std::endl;
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
    
    // 获取用户主目录
    const char* home_dir = std::getenv("HOME");
    if (home_dir == nullptr) {
        std::cerr << "Error: Could not get home directory." << std::endl;
        return -1;
    }
    // Resolve Parameters.
    std::string target_ip;
    int target_port;
    std::string file_path = std::string(home_dir) + "/filesystem.bin";
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

    // perform handshake
    if (!perform_handshake(udp_socket_fd, target_udp_sock_addr)) {
        std::cerr << "Handshake failed. Exiting." << std::endl;
        close(udp_socket_fd);
        return -1;
    }

    /*
    build the file
     */
    // open the file
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << file_path << std::endl;
        return -1;
    }
    std::vector<char> buffer(CHUNK_SIZE);  // buffer to store each chunk
    int packet_number = 0;                 // packet number
    uint32_t packet_header;                // 包编号的头部
    // split and send at the same time (for Big File)
    while (!file.eof()) {
        // read the file to the buffer
        file.read(buffer.data(), CHUNK_SIZE);
        // the byte number of read actually.
        std::streamsize bytes_read = file.gcount();

        if (bytes_read > 0) {
            // packet the data
            std::vector<char> packaged_data;

            // packet_number as the header.
            packet_header = static_cast<uint32_t>(packet_number);
            // insert packet_number
            packaged_data.insert(packaged_data.end(), reinterpret_cast<char*>(&packet_header), reinterpret_cast<char*>(&packet_header) + sizeof(packet_header));
            // insert chunk data
            packaged_data.insert(packaged_data.end(), buffer.begin(), buffer.begin() + bytes_read);

            // Send data.
            ssize_t bytes_sent = sendto(udp_socket_fd, packaged_data.data(), packaged_data.size(), 0,
                    (const struct sockaddr *) &target_udp_sock_addr, tsocklen);
            if (-1 == bytes_sent) {
                std::cerr << "Failed to send data via UDP. " << strerror(errno) << std::endl;
                close(udp_socket_fd);
                return -1;
            } else {
                std::cout << "Successfuly sent " << bytes_sent << " bytes.  " << "packet_bumber: " << packet_number << std::endl;
            }

            // packet_number increment
            packet_number++;
        }

    }
    file.close();
    send_eof_packet(udp_socket_fd, target_udp_sock_addr);
    std::cout << "The sender has already read the file and send all chunks." << std::endl;

    sockaddr_in unknown_sock_addr;
    memset(&unknown_sock_addr, 0, sizeof(unknown_sock_addr));
    socklen_t uslen = sizeof(unknown_sock_addr);

    char xbuffer[1024];
    int recvNum = 0;
    // receive message from server.
    int byteLen = recvfrom(udp_socket_fd, (char*) xbuffer, 1024, 0,
                (struct sockaddr*)&unknown_sock_addr, &uslen);
    if (byteLen < 0) {
        std::cerr << "recvfrom Error" << std::endl;
    }
    std::string res(xbuffer, byteLen);
    std::cout << res << std::endl;

    close(udp_socket_fd);
    std::cout << "The Client Ends." << std::endl;
    return 0;
}