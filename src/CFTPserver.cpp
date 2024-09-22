#include "CFTPserver.h"

#define MAX_PACKET_SIZE 1400
#define WINDOW_SIZE 2000  
#define TIMEOUT 10 // ms

enum MessageType {
    HANDSHAKE_REQUEST,
    HANDSHAKE_RESPONSE,
    DATA_PACKET,
    ACK_PACKET,
    END_OF_TRANSMISSION // tranmission end.
};

struct HandshakeMessage {
    MessageType type;
    int window_size;
    int mtu;
    int64_t file_size;
    int file_name_length; 
};

struct Packet {
    MessageType type;
    int seq_num;        
    uint32_t crc32;     
    int size;
    //        
};

struct Ack {
    MessageType type;
    int acked;             
    int interval_count;     
    // 
};

uint32_t calculate_CRC32(const char* data, size_t length) {
    return crc32(0L, reinterpret_cast<const Bytef*>(data), length);
}

std::vector<char> serialize_Ack(const Ack& ack, const std::vector<std::pair<int, int>>& missing_intervals) {
    size_t total_size = sizeof(int) * 3 + missing_intervals.size() * sizeof(int) * 2;
    std::vector<char> buffer(total_size);

    size_t offset = 0;

    // type
    int type_network = htonl(ack.type);
    memcpy(buffer.data() + offset, &type_network, sizeof(int));
    offset += sizeof(int);

    // acked
    int acked_network = htonl(ack.acked);
    memcpy(buffer.data() + offset, &acked_network, sizeof(int));
    offset += sizeof(int);

    // interval_count
    int interval_count_network = htonl(ack.interval_count);
    memcpy(buffer.data() + offset, &interval_count_network, sizeof(int));
    offset += sizeof(int);

    // 
    for (const auto& interval : missing_intervals) {
        int start_network = htonl(interval.first);
        int end_network = htonl(interval.second);

        // std::cout << "Serializing, start: " << interval.first << ", end: " << interval.second << std::endl; // 调试信息
        memcpy(buffer.data() + offset, &start_network, sizeof(int));
        offset += sizeof(int);

        memcpy(buffer.data() + offset, &end_network, sizeof(int));
        offset += sizeof(int);
    }

    return buffer;
}

Packet deserialize_packet(const std::vector<char>& buffer, char*& data) {
    Packet packet;
    size_t offset = 0;

    int type_network;
    memcpy(&type_network, buffer.data() + offset, sizeof(int));
    packet.type = static_cast<MessageType>(ntohl(type_network));
    offset += sizeof(int);

    int seq_num_network;
    memcpy(&seq_num_network, buffer.data() + offset, sizeof(int));
    packet.seq_num = ntohl(seq_num_network);
    offset += sizeof(int);

    uint32_t crc32_network;
    memcpy(&crc32_network, buffer.data() + offset, sizeof(uint32_t));
    packet.crc32 = ntohl(crc32_network);
    offset += sizeof(uint32_t);

    int size_network;
    memcpy(&size_network, buffer.data() + offset, sizeof(int));
    packet.size = ntohl(size_network);
    offset += sizeof(int);

    if (packet.size > 0) {
        data = new char[packet.size];
        memcpy(data, buffer.data() + offset, packet.size);
    } else {
        data = nullptr;
    }

    return packet;
}

// HandShakeMessage
std::vector<char> serialize_handshake(const HandshakeMessage& msg, const std::string& file_name) {
    size_t total_size = sizeof(int) * 4 + sizeof(int64_t) + file_name.size();
    std::vector<char> buffer(total_size);

    size_t offset = 0;

    int type_network = htonl(msg.type);
    memcpy(buffer.data() + offset, &type_network, sizeof(int));
    offset += sizeof(int);

    int window_size_network = htonl(msg.window_size);
    memcpy(buffer.data() + offset, &window_size_network, sizeof(int));
    offset += sizeof(int);

    // MTU
    int mtu_network = htonl(msg.mtu);
    memcpy(buffer.data() + offset, &mtu_network, sizeof(int));
    offset += sizeof(int);

    int64_t file_size_network = htonl(msg.file_size);
    memcpy(buffer.data() + offset, &file_size_network, sizeof(int64_t));
    offset += sizeof(int64_t);

    int file_name_length_network = htonl(file_name.size());
    memcpy(buffer.data() + offset, &file_name_length_network, sizeof(int));
    offset += sizeof(int);

    memcpy(buffer.data() + offset, file_name.data(), file_name.size());

    return buffer;
}

// De handshake
HandshakeMessage deserialize_handshake(const std::vector<char>& buffer, std::string& file_name) {
    HandshakeMessage msg;
    size_t offset = 0;

    //  type
    int type_network;
    memcpy(&type_network, buffer.data() + offset, sizeof(int));
    msg.type = static_cast<MessageType>(ntohl(type_network));
    offset += sizeof(int);

    //  window_size
    int window_size_network;
    memcpy(&window_size_network, buffer.data() + offset, sizeof(int));
    msg.window_size = ntohl(window_size_network);
    offset += sizeof(int);

    // MTU
    int mtu_network;
    memcpy(&mtu_network, buffer.data() + offset, sizeof(int));
    msg.mtu = ntohl(mtu_network);
    offset += sizeof(int);

    // file_size
    int64_t file_size_network;
    memcpy(&file_size_network, buffer.data() + offset, sizeof(int64_t));
    msg.file_size = ntohl(file_size_network);
    offset += sizeof(int64_t);

    // file_name_length
    int file_name_length_network;
    memcpy(&file_name_length_network, buffer.data() + offset, sizeof(int));
    int file_name_length = ntohl(file_name_length_network);
    offset += sizeof(int);

    // 
    if (file_name_length > 0) {
        file_name.assign(buffer.data() + offset, file_name_length);
    } else {
        file_name.clear();
    }

    return msg;
}

int set_sock_nonblock(int sockfd) {
    // 获取当前的文件描述符标志
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "fcntl(F_GETFL) failed" << std::endl;
        return -1;
    }

    // 将 O_NONBLOCK 标志添加到 flags 中
    flags |= O_NONBLOCK;

    // 设置新的标志回到 socket
    if (fcntl(sockfd, F_SETFL, flags) == -1) {
        std::cerr << "fcntl(F_SETFL) failed" << std::endl;
        return -1;
    }

    return 0;  // 设置成功
}

void send_ack(int sockfd, struct sockaddr_in& cliaddr, socklen_t len, int acked_num, const std::vector<std::pair<int, int>>& missing_intervals) {
    Ack ack;
    ack.type = ACK_PACKET;
    ack.acked = acked_num;
    ack.interval_count = missing_intervals.size();
    
    std::vector<char> serialized_ack = serialize_Ack(ack, missing_intervals);

    ssize_t sent_size = sendto(sockfd, serialized_ack.data(), serialized_ack.size(), 0, (struct sockaddr*)&cliaddr, len);
    if (sent_size < 0) {
        perror("Failed to send ack");
        return ;
    }

    // std::cout << "Sent ACK，Accumu acked Seq: " << ack.acked << " sent_size: " << sent_size << std::endl;
    if (ack.interval_count > 0) {
         std::cout << "，Missing Intervals Count: " << ack.interval_count;
        for (const auto& interval : missing_intervals) {
             std::cout << " (" << interval.first << ", " << interval.second << ")";
        }
    }
    //  std::cout << std::endl;
}

std::vector<std::pair<int, int>> merge_intervals(const std::vector<std::pair<int, int>>& intervals) {
    std::vector<std::pair<int, int>> merged_intervals;

    if (intervals.empty()) {
        return merged_intervals;
    }

    // copy and sort intervals
    std::vector<std::pair<int, int>> sorted_intervals = intervals;
    std::sort(sorted_intervals.begin(), sorted_intervals.end(),
              [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                  return a.first < b.first;
              });

    int current_start = sorted_intervals[0].first;
    int current_end = sorted_intervals[0].second;

    for (size_t i = 1; i < sorted_intervals.size(); ++i) {
        if (sorted_intervals[i].first <= current_end + 1) {
            // merge intervals
            current_end = std::max(current_end, sorted_intervals[i].second);
        } else {
            // cannot merge, keep it and start new interval
            merged_intervals.emplace_back(current_start, current_end);
            current_start = sorted_intervals[i].first;
            current_end = sorted_intervals[i].second;
        }
    }

    // add the last interval
    merged_intervals.emplace_back(current_start, current_end);

    return merged_intervals;
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return -1;
    }

    int port = atoi(argv[1]);

    // auto begin = std::chrono::system_clock::now();
    // auto duration = begin.time_since_epoch();
    // auto begin_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len = sizeof(cliaddr);
    int agreed_mtu;
    int timeout = TIMEOUT;

    // size_t protocol_header_size = sizeof(int) * 3 + sizeof(uint32_t);
    // size_t max_data_size;

    // UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

    // set non block socket
    if (-1 == set_sock_nonblock(sockfd)) {
        close(sockfd);
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));
    servaddr.sin_family = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    // bind socket
    if (bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        std::cerr << "Bind failed." << std::endl;
        return -1;
    }

    std::ofstream file;
    
    // expect next seq number (used to write to the file)
    int expected_seq_num = 0;
    // current max received seq num
    int max_received_num = 0;

    bool trans_finish = false;
    long filesize;

    // receiver window, x[seq] = data 。。
    std::map<int, std::vector<char>> recv_buffer;

    // check error packet
    std::set<int> wrong_seqs_set;

    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll create fail");
        close(sockfd);
        return -1;
    }

    // add to listen list
    struct epoll_event ev;
    ev.events = EPOLLIN; // listen read event
    ev.data.fd = sockfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
        perror("epoll_ctl fail");
        close(sockfd);
        close(epfd);
        return -1;
    } 

    bool handshake_finish = false;
    std::string file_name_str;

    while (!handshake_finish) {
        struct epoll_event events[1];
        // Blocking wait
        int nfds = epoll_wait(epfd, events, 1, timeout);
        if (nfds == -1) {
            perror("epoll_wait fail");
            close(sockfd);
            close(epfd);
            return -1;
        }

        if (events[0].events & EPOLLIN) {
            std::vector<char> buffer(MAX_PACKET_SIZE);
            ssize_t recv_size = recvfrom(sockfd, buffer.data(), buffer.size(), 0,
                                         (struct sockaddr*)&cliaddr, &len);
            if (recv_size > 0) {
                buffer.resize(recv_size);
                HandshakeMessage hs_req = deserialize_handshake(buffer, file_name_str);
                if (hs_req.type == HANDSHAKE_REQUEST) {

                    // to be used
                    agreed_mtu = hs_req.mtu;
                    timeout = agreed_mtu;
                    filesize = hs_req.file_size;

                    std::cout << "Handshake Connect, filename: " << file_name_str << std::endl;
                    std::cout << "Filesize: " << filesize << " agreed_mtu" << agreed_mtu << std::endl;
                    
                    // decide a mtu

                    file.open(file_name_str, std::ios::out | std::ios::binary);
                    if (!file.is_open()) {
                        perror("file open fail");
                        close(sockfd);
                        return -1;
                    }
                    
                    // handshake response
                    HandshakeMessage hs_resp;
                    hs_resp.type = HANDSHAKE_RESPONSE;
                    hs_resp.window_size = WINDOW_SIZE;
                    hs_resp.file_name_length = 0;
                    hs_resp.mtu = agreed_mtu;
                    hs_resp.file_size = filesize;

                    std::vector<char> hs_resp_data = serialize_handshake(hs_resp, "");
                    sendto(sockfd, hs_resp_data.data(), hs_resp_data.size(), 0,
                           (struct sockaddr*)&cliaddr, len);
                    handshake_finish = true;
                    
                }
            }
        }
    }

    int ack_header_size = sizeof(int) * 3;

    // control time
    auto last_SACK_time = std::chrono::steady_clock::now();
    const std::chrono::milliseconds ack_interval(timeout);

    // whether receive END or not.
    bool receive_END = false;

    while (!trans_finish) {

        struct epoll_event events[1];
        int nfds = epoll_wait(epfd, events, 1, timeout);
        if (nfds == -1) {
            perror("epoll_wait fail");
            close(sockfd);
            close(epfd);
            return -1;
        } else if (nfds == 0) {
            // epoll_wait timeout
            std::vector<std::pair<int, int>> missing_intervals;
            missing_intervals.clear();
            if (receive_END == true && expected_seq_num * agreed_mtu >= filesize) {
                Ack ack;
                ack.type = END_OF_TRANSMISSION;
                ack.acked = expected_seq_num;
                ack.interval_count = 0;
                // 序列化 ACK
                std::vector<char> serialized_ack = serialize_Ack(ack, missing_intervals);
                // 发送 ACK
                ssize_t sent_size = sendto(sockfd, serialized_ack.data(), serialized_ack.size(), 0, (struct sockaddr*)&cliaddr, len);
                if (sent_size < 0) {
                    perror("fail to send ACK");
                }
                trans_finish = true;
                continue;
            }

            if (expected_seq_num >= max_received_num) {
                // just timeout, so send an ACK.
                send_ack(sockfd, cliaddr, len, expected_seq_num-1, missing_intervals);
            } else {
                // there is a range of packet. send SACK
                for (int seq = expected_seq_num; seq < expected_seq_num + WINDOW_SIZE && seq <= max_received_num; ++seq) {
                    if (recv_buffer.find(seq) == recv_buffer.end() && wrong_seqs_set.find(seq) == wrong_seqs_set.end()) {
                        missing_intervals.emplace_back(seq, seq);
                    }
                }
                // add CRC
                for (const int& corrupted_seq : wrong_seqs_set) {
                    missing_intervals.emplace_back(corrupted_seq, corrupted_seq);
                }
                missing_intervals = merge_intervals(missing_intervals);
                int interval_size = sizeof(int) * 2;
                int max_intervals = (agreed_mtu - ack_header_size) / (interval_size * 2); // 直接一半，冗余给序列化变化
                size_t total_intervals = missing_intervals.size();
                // send SACK group by group
                for (size_t i = 0; i < total_intervals; i += max_intervals) {
                    // current missing intervals
                    auto end_it = std::min(missing_intervals.begin() + i + max_intervals, missing_intervals.end());
                    std::vector<std::pair<int, int>> current_intervals(missing_intervals.begin() + i, end_it);

                    send_ack(sockfd, cliaddr, len, expected_seq_num-1, current_intervals);
                }
                missing_intervals.clear();
                last_SACK_time = std::chrono::steady_clock::now();   
            }
        } else {
            if (events[0].events & EPOLLIN) {
                // receive Data Packet
                std::vector<char> buffer(MAX_PACKET_SIZE);
                int count = 0;
                while((count = recvfrom(sockfd, buffer.data(), buffer.size(), 0, (struct sockaddr*)&cliaddr, &len)) > 0) {
                    buffer.resize(count);
                    char *data = nullptr;
                    Packet packet = deserialize_packet(buffer, data);
                    std::vector<std::pair<int,int>> missing_intervals;
                    
                    if (packet.type == DATA_PACKET) {
                        //  std::cout << "Receive data packet, Seq Num: " << packet.seq_num << std::endl;
                        
                        //  check CRC32
                        uint32_t crc32_calculated = calculate_CRC32(data, packet.size);
                        //  std::cout << "Receiver: Seq Num = " << packet.seq_num << ", Data Size = " << packet.size << ", Received CRC32 = " << packet.crc32 << ", Calculated CRC32 = " << crc32_calculated << std::endl;
                        if (packet.crc32 == crc32_calculated) {
                            // correct packet

                            // if it is in wrong_set, erase it.
                            if (wrong_seqs_set.find(packet.seq_num) != wrong_seqs_set.end()) {
                                wrong_seqs_set.erase(packet.seq_num);
                            }
                            if (packet.seq_num == expected_seq_num) {
                                // write the file immediately
                                std::vector<char> tmp = std::vector<char>(data, data + packet.size);
                                file.write(tmp.data(), tmp.size());
                                expected_seq_num++;
                                // try to write continuously
                                // check the buffer
                                while (recv_buffer.find(expected_seq_num) != recv_buffer.end()) {

                                    file.write(recv_buffer[expected_seq_num].data(), recv_buffer[expected_seq_num].size());
                                    //  std::cout << "Write the packet to file, Seq Num: " << expected_seq_num << std::endl;
                                    // erase the packet number written from bufer.
                                    recv_buffer.erase(expected_seq_num);
                                    expected_seq_num++;
                                }
                                if (max_received_num < expected_seq_num) max_received_num = expected_seq_num;
                                // ACK
                                send_ack(sockfd, cliaddr, len, packet.seq_num, missing_intervals);
                            } else if (packet.seq_num > expected_seq_num) {
                                // higher than expect. store it to buffer.
                                if (recv_buffer.find(packet.seq_num) == recv_buffer.end()) {
                                    recv_buffer[packet.seq_num] = std::vector<char>(data, data + packet.size);
                                    if (packet.seq_num > max_received_num) {
                                        max_received_num = packet.seq_num;
                                    }
                                    // timeout retransmission. build missing intervals
                                    if (std::chrono::steady_clock::now() > last_SACK_time + ack_interval) {
                                        for (int seq = expected_seq_num; seq < expected_seq_num + WINDOW_SIZE && seq <= max_received_num; ++seq) {
                                            if (recv_buffer.find(seq) == recv_buffer.end() && wrong_seqs_set.find(seq) == wrong_seqs_set.end()) {
                                                missing_intervals.emplace_back(seq, seq);
                                            }
                                        }
                                        for (const int& corrupted_seq : wrong_seqs_set) {
                                            missing_intervals.emplace_back(corrupted_seq, corrupted_seq);
                                        }
                                        missing_intervals = merge_intervals(missing_intervals);
                                        // std::cout << "inside missing_intervals, number of pairs" << missing_intervals.size() << std::endl;
                                        int interval_size = sizeof(int) * 2;
                                        int max_intervals = (agreed_mtu - ack_header_size) / (interval_size * 2); // 直接一半，冗余给序列化变化
                                        size_t total_intervals = missing_intervals.size();
                                        // group by group
                                        for (size_t i = 0; i < total_intervals; i += max_intervals) {
                                            auto end_it = std::min(missing_intervals.begin() + i + max_intervals, missing_intervals.end());
                                            std::vector<std::pair<int, int>> current_intervals(missing_intervals.begin() + i, end_it);

                                            send_ack(sockfd, cliaddr, len, expected_seq_num-1, current_intervals);
                                        }
                                        missing_intervals.clear();
                                        last_SACK_time = std::chrono::steady_clock::now(); 
                                    }
                                    // Dont' have to deal with it here.
                                }
                            } else {
                                // expect_num > packet.seq_num
                                // The previous ACK may loss
                                send_ack(sockfd, cliaddr, len, expected_seq_num-1, missing_intervals);
                            }

                        } else {
                            std::cerr << "CRC32 check error， Seq：" << packet.seq_num << std::endl;
                            wrong_seqs_set.insert(packet.seq_num);
                            // restransmit it immediately. single 
                            missing_intervals.emplace_back(packet.seq_num, packet.seq_num);
                            send_ack(sockfd, cliaddr, len, packet.seq_num - 1, missing_intervals);
                        }
                    } else if (packet.type == END_OF_TRANSMISSION){
                        if (packet.seq_num == expected_seq_num) {
                            receive_END = true;
                            // True End.
                            Ack ack;
                            ack.type = END_OF_TRANSMISSION;
                            ack.acked = expected_seq_num;
                            ack.interval_count = 0;

                            std::vector<char> serialized_ack = serialize_Ack(ack, missing_intervals);

                            ssize_t sent_size = sendto(sockfd, serialized_ack.data(), serialized_ack.size(), 0, (struct sockaddr*)&cliaddr, len);
                            if (sent_size < 0) {
                                perror("Faield to send");
                            }
                        }
                        
                    }

                }
            }
        }
    }
    std::cout << "File Received." << std::endl;

    file.close();
    close(sockfd);
    close(epfd);

    return 0;
}
