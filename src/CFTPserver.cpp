#include"CFTPserver.h"

// temporarily, you can modify it!
#define LOCAL_HOST "10.0.2.41"
#define UDP_PORT 44044

#define TARGET_HOST "10.0.1.99"
#define TARGET_PORT 44044

#define CHUNK_SIZE 1400

#include<iostream>

struct Packet {
    uint32_t packet_number;       // 包编号
    std::vector<char> data;       // 数据
};


// 定义EOF标识符，表示传输结束的包编号
const uint32_t EOF_PACKET_NUMBER = static_cast<uint32_t>(-1);

int checkParameter(int argc, char* argv[]) {
    // Show User input Parameters
    for (int i = 0; i < argc; ++i) {
        std::cout << "Argument " << i << ": " << argv[i] << std::endl;
    }
    return 0;
}

int write_to_file(std::ofstream& output_file, Packet packet) {
    std::cout << "write packet_number " << packet.packet_number << std::endl;
    // while (received_chunks.find(next_expected_packet) != received_chunks.end()) {
    if (packet.packet_number != EOF_PACKET_NUMBER){
        const std::vector<char>& chunk_data = packet.data;
        output_file.write(chunk_data.data(), chunk_data.size());
    }
    return 0;
}

// 接收握手消息并发送确认
bool perform_handshake(int sockfd) {
    const int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // 接收握手消息
    ssize_t bytes_received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                                      reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
    if (bytes_received < 0) {
        std::cerr << "Error receiving handshake message." << std::endl;
        return false;
    }
    buffer[bytes_received] = '\0';  // 确保字符串以空字符结束
    std::cout << "Received handshake message: " << buffer << std::endl;

    // 发送握手确认消息
    char ack_message[] = "ACK";
    ssize_t bytes_sent = sendto(sockfd, ack_message, strlen(ack_message), 0,
                                reinterpret_cast<const struct sockaddr*>(&client_addr), addr_len);
    if (bytes_sent < 0) {
        std::cerr << "Error sending handshake confirmation." << std::endl;
        return false;
    }
    std::cout << "Handshake confirmation sent." << std::endl;

    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "The Server begins running" << std::endl;

    // Check input parameters.
    while (-1 == checkParameter(argc, argv)){};

    // 获取用户主目录
    const char* home_dir = std::getenv("HOME");
    if (home_dir == nullptr) {
        std::cerr << "Error: Could not get home directory." << std::endl;
        return -1;
    }

    // 暂时写死类型。之后文件类型等元数据要从client传过来
    std::string output_file_path = std::string(home_dir) + "/output_file.bin";

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
    // target_udp_sock_addr.sin_family = AF_INET;
    // target_udp_sock_addr.sin_port = htons(TARGET_PORT);
    // inet_pton(AF_INET, TARGET_HOST, &(target_udp_sock_addr.sin_addr));
    socklen_t tsocklen = sizeof(target_udp_sock_addr);
    
    
    // perform handshake
    if (!perform_handshake(udp_socket_fd)) {
        std::cerr << "Handshake failed. Exiting." << std::endl;
        close(udp_socket_fd);
        return -1;
    }
    
    /*
    receive the file
    */
    // open output file
    std::ofstream output_file(output_file_path, std::ios::binary);
    
    // Use an unordered_map to store the received data
    // key: packet_number, value: chunk data
    std::unordered_map<uint32_t, std::vector<char>> received_chunks;
    // std::queue<Packet> packet_queue;  // 用于存储数据包的队列
    
    uint32_t next_expected_packet = 0;

    // buffer to receive data
    char buffer[CHUNK_SIZE + sizeof(uint32_t)];

    std::cout << "start receiving data" << std::endl;
    while (true) {
        // receive data
        ssize_t bytes_received = recvfrom(udp_socket_fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (bytes_received < 0) {
            std::cerr << "Error receiving packet." << std::endl;
            break;
        }

        // get packet_number
        uint32_t packet_number;
        memcpy(&packet_number, buffer, sizeof(packet_number));
        std::cout << "Received packet_number" << packet_number << std::endl;

        // check if it is EOF packet_number
        if (packet_number == EOF_PACKET_NUMBER) {
            std::cout << "Received EOF packet. Transmission complete." << std::endl;
            break;
        }

        
        // get Data
        size_t data_size = bytes_received - sizeof(packet_number);
        std::vector<char> chunk_data(buffer + sizeof(packet_number), buffer + sizeof(packet_number) + data_size);

        // store the chunk data
        // std::move() lvalue -> rvalue, avoid unnecessary copy.
        // received_chunks[packet_number] = std::move(chunk_data);

        // 将包编号和数据存入队列
        Packet packet;
        packet.packet_number = packet_number;
        packet.data.assign(buffer + sizeof(packet_number), buffer + bytes_received);

        // packet_queue.push(packet);  // 将接收到的包放入队列
        // std::cout << "Stored packet #" << packet_number << " in queue." << std::endl;
        //
        if (!output_file.is_open()) {
            std::cerr << "Error: Could not open file " << output_file_path << " for writing." << std::endl;
            return -1;
        }
        write_to_file(output_file, packet);  
    }
    // close the output file
    output_file.close();
    std::cout << "The receiver has already received all file data and write it to file." << std::endl;

    std::string send_back_message = "Thank you, I got that!";
    // Send back messgae to server.
    if (-1 == sendto(udp_socket_fd, send_back_message.data(), send_back_message.length(), 0,
             (const struct sockaddr *) &target_udp_sock_addr, tsocklen)) {
        std::cerr << "Failed to send data via UDP. " << std::endl;
        close(udp_socket_fd);
        return -1;
    }
    std::cout << "The Server has sent the back Message to the Client." << std::endl;
    
    close(udp_socket_fd);
    std::cout << "The Server Ends." << std::endl;
    return 0;
}