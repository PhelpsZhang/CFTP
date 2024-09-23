#include "CFTPclient.h"

#define WINDOW_SIZE 1000 // Sliding Window Size
#define TIMEOUT 10
#define MTU 1472
#define MAX_RESEND_TIMES 10   // 10 times resend.
#define MAX_CONN_WAIT 10000   // 10 sec

enum MessageType {
    HANDSHAKE_REQUEST,
    HANDSHAKE_RESPONSE,
    DATA_PACKET,
    ACK_PACKET,
    END_OF_TRANSMISSION // tranmission end.
};

// Handshake message 
struct HandshakeMessage
{
    MessageType type;
    int window_size;
    int mtu;
    int64_t file_size;
    int file_name_length;
    // other extensible parameter.
};

struct Ack {
    MessageType type;
    int acked;              // ACKed
    int interval_count;     // count of missing interval
    // Selective ACK extend
};

// packet header
struct Packet {          
    MessageType type;
    int seq_num;                
    uint32_t crc32;             
    int size;                  
    // data content extend 
};

// Serialize Packet
std::vector<char> serialize_packet(const Packet& packet, const char* data) {
    size_t total_size = sizeof(int)*4 + packet.size;
    std::vector<char> buffer(total_size);

    size_t offset = 0;

    // 序列化 type
    int type_network = htonl(packet.type);
    memcpy(buffer.data() + offset, &type_network, sizeof(int));
    offset += sizeof(int);

    // 序列化 seq_num
    int seq_num_network = htonl(packet.seq_num);
    memcpy(buffer.data() + offset, &seq_num_network, sizeof(int));
    offset += sizeof(int);

    // 序列化 crc32
    uint32_t crc32_network = htonl(packet.crc32);
    memcpy(buffer.data() + offset, &crc32_network, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // 序列化 size
    int size_network = htonl(packet.size);
    memcpy(buffer.data() + offset, &size_network, sizeof(int));
    offset += sizeof(int);

    // 序列化 data
    if (packet.size > 0 && data != nullptr) {
        memcpy(buffer.data() + offset, data, packet.size);
    }

    return buffer;
}

// deserialize Ack
Ack deserialize_Ack(const std::vector<char>& buffer, std::vector<std::pair<int, int>>& missing_intervals) {
    Ack ack;
    size_t offset = 0;

    int type_network;
    memcpy(&type_network, buffer.data() + offset, sizeof(int));
    ack.type = static_cast<MessageType>(ntohl(type_network));
    offset += sizeof(int);

    //  acked
    int acked_network;
    memcpy(&acked_network, buffer.data() + offset, sizeof(int));
    ack.acked = ntohl(acked_network);
    offset += sizeof(int);

    //  interval_count
    int interval_count_network;
    memcpy(&interval_count_network, buffer.data() + offset, sizeof(int));
    ack.interval_count = ntohl(interval_count_network);
    offset += sizeof(int);

    // interval
    for (int i = 0; i < ack.interval_count; ++i) {
        int start_network, end_network;
        memcpy(&start_network, buffer.data() + offset, sizeof(int));
        offset += sizeof(int);

        memcpy(&end_network, buffer.data() + offset, sizeof(int));
        offset += sizeof(int);

        int start = ntohl(start_network);
        int end = ntohl(end_network);
        // std::cout << "Deserialized, start: " << start << ", end: " << end << std::endl; // 调试信息
        missing_intervals.emplace_back(start, end);
    }

    return ack;
}

// serialize HandShakeMessage
std::vector<char> serialize_handshake(const HandshakeMessage& msg, const std::string& file_name) {
    size_t total_size = sizeof(int) * 4 + sizeof(long) + file_name.size();
    std::vector<char> buffer(total_size);

    size_t offset = 0;

    // 序列化 type
    int type_network = htonl(msg.type);
    memcpy(buffer.data() + offset, &type_network, sizeof(int));
    offset += sizeof(int);

    // 序列化 window_size
    int window_size_network = htonl(msg.window_size);
    memcpy(buffer.data() + offset, &window_size_network, sizeof(int));
    offset += sizeof(int);

    // MTU
    int mtu_network = htonl(msg.mtu);
    memcpy(buffer.data() + offset, &mtu_network, sizeof(int));
    offset += sizeof(int);

    // 序列化 file_size
    int64_t file_size_network = htonl(msg.file_size);
    memcpy(buffer.data() + offset, &file_size_network, sizeof(int64_t));
    offset += sizeof(int64_t);

    // 序列化 file_name_length
    int file_name_length_network = htonl(file_name.size());
    memcpy(buffer.data() + offset, &file_name_length_network, sizeof(int));
    offset += sizeof(int);

    // 序列化文件名
    memcpy(buffer.data() + offset, file_name.data(), file_name.size());

    return buffer;
}

// deserialize handshake
HandshakeMessage deserialize_handshake(const std::vector<char>& buffer, std::string& file_name) {
    HandshakeMessage msg;
    size_t offset = 0;

    // 反序列化 type
    int type_network;
    memcpy(&type_network, buffer.data() + offset, sizeof(int));
    msg.type = static_cast<MessageType>(ntohl(type_network));
    offset += sizeof(int);

    // 反序列化 window_size
    int window_size_network;
    memcpy(&window_size_network, buffer.data() + offset, sizeof(int));
    msg.window_size = ntohl(window_size_network);
    offset += sizeof(int);

    // MTU
    int mtu_network;
    memcpy(&mtu_network, buffer.data() + offset, sizeof(int));
    msg.mtu = ntohl(mtu_network);
    offset += sizeof(int);

    // 反序列化 file_size
    int64_t file_size_network;
    memcpy(&file_size_network, buffer.data() + offset, sizeof(int64_t));
    msg.file_size = ntohl(file_size_network);
    offset += sizeof(int64_t);

    // 反序列化 file_name_length
    int file_name_length_network;
    memcpy(&file_name_length_network, buffer.data() + offset, sizeof(int));
    int file_name_length = ntohl(file_name_length_network);
    offset += sizeof(int);

    // 反序列化文件名
    if (file_name_length > 0) {
        file_name.assign(buffer.data() + offset, file_name_length);
    } else {
        file_name.clear();
    }

    return msg;
}

// get the size of file in bytes
long getFileSize(const char *filepath) {
    struct stat stat_buf;
    if (stat(filepath, &stat_buf) != 0)
    {
        return -1; // 获取失败，返回-1
    }
    return stat_buf.st_size; // 返回文件大小
}

// get filename from filepath
std::string getFileName(const std::string &filePath) {
    size_t pos = filePath.find_last_of("/\\"); // 查找最后一个路径分隔符的位置
    if (pos != std::string::npos)
    {
        return filePath.substr(pos + 1); // 返回文件名部分
    }
    return filePath; // 如果没有分隔符，返回整个路径
}

// set NON-Blocking 
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

// long long get_timestamp_millis() {
//     struct timespec ts;
//     clock_gettime(CLOCK_REALTIME, &ts);
//     return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;

//     // auto now = std::chrono::system_clock::now();
//     // auto duration = now.time_since_epoch();
//     // return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
// }

// CRC32
uint32_t calculate_CRC32(const char* data, size_t length) {
    return crc32(0L, reinterpret_cast<const Bytef*>(data), length);
}

void resend_packet(int sockfd, struct sockaddr_in& servaddr, socklen_t len, int rseq_num, std::map<int, std::vector<char>>& fly_packets, std::map<int, timeval>& timeout_table) {
    int seq_num = rseq_num;
    ssize_t sent_size = sendto(sockfd, fly_packets[seq_num].data(), fly_packets[seq_num].size(),
                                        0, (struct sockaddr*)&servaddr, len);
     std::cout << "resent a packet, seq_num" << seq_num << std::endl;           
    // if (sent_size < 0) {
    //     // std::cerr << "sendto failed: " << strerror(errno) << std::endl;
    //     if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // std::cerr << "resend_packet. rseq_num:" << rseq_num  << std::endl;
    //         // usleep(100);
    //     } else {
    //         std::cerr << "Resent failed, error info：" << strerror(errno) << std::endl;
    //     }
    // }
    // build timeout_table。
    struct timeval send_time;
    gettimeofday(&send_time, NULL);
    timeout_table[seq_num] = send_time;
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        // argument correction
        std::cerr << "Usage: " << argv[0] << " <IP address> <port> <file_path>" << std::endl;
        return -1;
    }

    // get the start time of program
    auto begin = std::chrono::system_clock::now();
    auto duration = begin.time_since_epoch();
    auto begin_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();


    const char *server_ip = argv[1];    
    int port = atoi(argv[2]);           
    const char *file_path = argv[3];    

    int sockfd;                         
    struct sockaddr_in servaddr;        
    Ack ack;                            

    int agreed_mtu;                     

    long filesize = getFileSize(file_path);
    if (filesize < 0) {
        std::cerr << "Failed to get file size." << std::endl;
        return -1;
    }

    // create udp socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

    // Set Non-blocking socket
    // if (-1 == set_sock_nonblock(sockfd)) {
    //     close(sockfd);
    //     return -1;
    // }

    // server address info
    memset(&servaddr, 0, sizeof(servaddr));    
    servaddr.sin_family = AF_INET;             
    servaddr.sin_port = htons(port);           
    servaddr.sin_addr.s_addr = inet_addr(server_ip);
    socklen_t len = sizeof(servaddr);


    struct timeval recv_timeout;
    recv_timeout.tv_sec = 0;  // 5秒超时
    recv_timeout.tv_usec = 1000; // 1ms

    // if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0) {
    //     std::cerr << "Failed to set socket timeout." << std::endl;
    //     close(sockfd);
    //     return -1;
    // }

    // open file
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file." << std::endl;
        close(sockfd);
        return -1;
    }

    // create epoll instance
    int epfd = epoll_create1(0);  // 0 for default
    if (epfd == -1) {
        std::cerr << "Failed to create epoll instance: " << strerror(errno) << std::endl;
        close(sockfd);
        return -1;
    }

    // register socket to events list, listen EPOLLIN event
    struct epoll_event ev;
    ev.events = EPOLLIN;  // things to read
    ev.data.fd = sockfd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
        std::cerr << "Failed to add socket to epoll: " << strerror(errno) << std::endl;
        close(epfd);
        close(sockfd);
        return -1;
    }
    struct epoll_event events[1];

    bool handshake_finish = false;
    int window_size = WINDOW_SIZE;
    std::string file_name_str = getFileName(file_path);

    int base = 0;           // Sliding window left bound [)
    int next_seq_num = 0;   // next new packet number to send 
    bool is_last_packet = false;
    bool trans_finish = false;
    int resent_count = 0;   // count the retransmit times.

    // record each packet's timeout.
    std::map<int, timeval> timeout_table;
    
    // "fly bytes" buffer and used to retranmit. the value are data serialized.
    std::map<int, std::vector<char>> fly_packets;

    // timeout variable, from define or input. To be adjusted
    int timeout = TIMEOUT;
    int test_rtt = 0;

    // HandShake
    while (!handshake_finish) {
        HandshakeMessage hs_req;
        hs_req.type = HANDSHAKE_REQUEST;
        hs_req.window_size = window_size;
        hs_req.mtu = MTU;
        hs_req.file_size = filesize;
        hs_req.file_name_length = file_name_str.size();

        auto sent = std::chrono::system_clock::now();
        auto duration = sent.time_since_epoch();
        auto sent_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

        std::vector<char> hs_req_data = serialize_handshake(hs_req, file_name_str);
        sendto(sockfd, hs_req_data.data(), hs_req_data.size(), 0,
            (struct sockaddr*)&servaddr, len);
        
        // Wait for handshake response
        int nfds = epoll_wait(epfd, events, 1, 3000);   // 3 seconds
        if (nfds == -1) {
            std::cerr << "epoll wait failed." << std::endl;
            close(sockfd);
            close(epfd);
            return -1;
        } else if (nfds == 0) {
            // timeout. while loop to resend again.
        } else {
            if (events[0].events & EPOLLIN) {
                // handshake response
                std::vector<char> buffer(MTU);
                ssize_t recv_size = recvfrom(sockfd, buffer.data(), buffer.size(), 0,
                                             (struct sockaddr*)&servaddr, &len);
                if (recv_size > 0) {
                    auto recv = std::chrono::system_clock::now();
                    auto duration2 = recv.time_since_epoch();
                    auto recv_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration2).count();
                    
                    // measure a approximate rtt
                    test_rtt = recv_millis - sent_millis;
                    std::cout << "Test RTT:" << test_rtt << std::endl;

                    buffer.resize(recv_size);
                    std::string dummy_file_name;
                    HandshakeMessage handshake_resp = deserialize_handshake(buffer, dummy_file_name);
                    if (handshake_resp.type == HANDSHAKE_RESPONSE) {
                        // Successful Handshake
                        handshake_finish = true;

                        window_size = handshake_resp.window_size;
                        agreed_mtu = handshake_resp.mtu;
                        
                        hs_req.type = HANDSHAKE_RESPONSE;
                        std::vector<char> hs_req_data = serialize_handshake(hs_req, file_name_str);
                        ssize_t sent_size = sendto(sockfd, hs_req_data.data(), hs_req_data.size(), 0,
                                (struct sockaddr*)&servaddr, len);
                        if (sent_size < 0) {
                            perror("Failed to send");
                            close(sockfd);
                            close(epfd);
                            return -1;
                        }
                        next_seq_num = 0; // update next seq
                    }
                }
            }
        }

    }

    size_t protocol_header_size = sizeof(int) * 3 + sizeof(uint32_t); // 根据您的协议头部大小计算
    size_t desired_size = agreed_mtu - protocol_header_size; // 每个数据包的数据部分大小
    
    timeval last_retrans_window;
    gettimeofday(&last_retrans_window, NULL);

    auto ite = fly_packets.begin();
    bool update_ite_flag = false;

    while (!trans_finish)
    {
        std::cout << "base:" << base << " next_seq_num: " << next_seq_num << std::endl;
        std::cout << "timeout:" << timeout << "test_rtt" << test_rtt<< std::endl;
        timeout = TIMEOUT;

        while (next_seq_num < base + WINDOW_SIZE && !is_last_packet)
        {
            // 每个数据包只从file读一次，放进我们的缓存，而不是每次移动指针重新去读！
            char* data_buffer = new char[desired_size];
            file.read(data_buffer, desired_size);
            size_t data_size = file.gcount();

            if (data_size == 0) {
                // File read complete
                is_last_packet = true;  // 此时next_seq_num - 1才是真正的最后一个包的包号。 那么结束判定就是is_last_packet == true && next_seq_num -1 == ack.acked 
                // 这里可发可不发。写上反而重复了。
            } else {
                Packet packet;
                packet.type = DATA_PACKET;
                packet.seq_num = next_seq_num;
                packet.size = data_size;
                packet.crc32 = calculate_CRC32(data_buffer, data_size);

                std::vector<char> serialized_data = serialize_packet(packet, data_buffer);
                
                ssize_t sent_size = sendto(sockfd, serialized_data.data(), serialized_data.size(),
                                            0, (struct sockaddr*)&servaddr, len);
                std::cout << "New Packet:" << next_seq_num << std::endl;

                struct timeval send_time;
                gettimeofday(&send_time, NULL);
                timeout_table[next_seq_num] = send_time;

                fly_packets[next_seq_num] = serialized_data;

                delete[] data_buffer;
                next_seq_num++;
            }
            
        }

        int nfds = epoll_wait(epfd, events, 1, test_rtt);
        std::cout << "epoll_wait return value:" << nfds << std::endl;
        if (nfds == -1) {
            std::cerr << "Error in epoll_wait: " << strerror(errno) << std::endl;
            close(sockfd);
            close(epfd);
            return -1;
        } else if (nfds == 0) {
        // Timeout and retransmit
        
            timeval now;
            gettimeofday(&now, NULL);
            // if (update_ite_flag == true || ite == fly_packets.end()) {
            //     ite = fly_packets.begin();
            //     update_ite_flag = false;
            // }

            // int seq = ite->first;
            // timeval send_time = timeout_table[seq];
            // // std::cout << "seq"<< seq << " before timeout_table" << send_time.tv_sec*1000 + send_time.tv_usec/1000 << std::endl;
            // long elasped_time = (now.tv_sec - send_time.tv_sec) * 1000 +
            //                          (now.tv_usec - send_time.tv_usec) / 1000;
            // std::cout << "elasped_time:" << elasped_time << " timeout: " << timeout << std::endl;
            // if (elasped_time >= timeout) {
            //     resent_count++;
            //     ssize_t sent_size = sendto(sockfd, ite->second.data(), ite->second.size(),
            //               0, (struct sockaddr*)&servaddr, len);
            
            //     gettimeofday(&timeout_table[seq], NULL);
            // }
            // ite++;

            // long interval_of_windowretrans = (now.tv_sec - last_retrans_window.tv_sec) * 1000 +
            //         (now.tv_usec - last_retrans_window.tv_usec) / 1000;
            // if (interval_of_windowretrans < MAX_CONN_WAIT) {
            //     continue;
            // }
            // std::cout << "Timeout Retrans, Now the fly_packets count: " << fly_packets.size() << std::endl;
            
            
            for (auto ite = fly_packets.begin(); ite != fly_packets.end(); ite++) {

                int seq = ite->first;
                timeval send_time = timeout_table[seq];
                // std::cout << "seq"<< seq << " before timeout_table" << send_time.tv_sec*1000 + send_time.tv_usec/1000 << std::endl;
                long elasped_time = (now.tv_sec - send_time.tv_sec) * 1000 +
                                    (now.tv_usec - send_time.tv_usec) / 1000;
                std::cout << "elasped_time:" << elasped_time << " timeout: " << timeout << std::endl;
                if (elasped_time >= timeout) {
                    resent_count++;
                    std::cout << "Timeout Retrans. Seq Num:"<< seq << std::endl;
                    ssize_t sent_size = sendto(sockfd, ite->second.data(), ite->second.size(),
                                        0, (struct sockaddr*)&servaddr, len);
                    // update timeout_table after retransmit
                    gettimeofday(&timeout_table[seq], NULL);
                    // std::cout << "seq"<<seq << " new timeout_table" << timeout_table[seq].tv_sec*1000 + timeout_table[seq].tv_usec/1000 << std::endl;
                }
            }
            // timeval now2;
            // gettimeofday(&now2, NULL);
            // long test = (now2.tv_sec - now.tv_sec) * 1000 +
            //                     (now2.tv_usec - now.tv_usec) / 1000;
            // std::cout << "走一圈重传循环花费：" << test << " ms ***********" << std::endl;
        } else {
            // Get an ACK
            if (events[0].events & EPOLLIN) {
                ssize_t count = 0;
                std::vector<char> ack_buffer(MTU);
                std::vector<std::pair<int,int>> missing_intervals;
                missing_intervals.clear();
                if((count = recvfrom(sockfd, ack_buffer.data(), ack_buffer.size(), 0, (struct sockaddr *)&servaddr, &len)) > 0) {
                    std::cout << "进入recvfrom返回的count：" << count << std::endl;
                    ack = deserialize_Ack(ack_buffer, missing_intervals);
                    std::cout << "收到的ACK的interval count:" << ack.interval_count << std::endl; 
                    std::cout << "收到的ACK的aced:" << ack.acked << std::endl;
                    if (ack.type == ACK_PACKET) {

                        // Update window left bound
                        if (ack.acked >= base) {
                            // erase the packet which are acked.
                            for(int seq = base; seq <= ack.acked; seq++) {
                                fly_packets.erase(seq);
                                timeout_table.erase(seq);
                            }
                            base = ack.acked + 1;
                            update_ite_flag = true;
                        }

                        if (ack.interval_count > 0) {
                            struct timeval now;
                            gettimeofday(&now, NULL);
                            // deal with missing interval
                            for (const auto& interval : missing_intervals) {
                                for (int seq = interval.first; seq <= interval.second; seq++) {
                                    timeval send_time = timeout_table[seq];
                                    long elasped_time = (now.tv_sec - send_time.tv_sec) * 1000 +
                                                        (now.tv_usec - send_time.tv_usec) / 1000;
                                    if (seq >= base && seq < next_seq_num && elasped_time > timeout) {
                                        // resend single packet.
                                        resent_count++;
                                        resend_packet(sockfd, servaddr, len, seq, fly_packets, timeout_table);
                                    }
                                }
                            }
                        }
                        

                        // The last packet has been send once AND the final packet(next_seq_num - 1) has been acked.
                        if (is_last_packet == true && next_seq_num -1 == ack.acked) {
                            trans_finish = true;
                        }
                    
                    } 
                    // else if (ack.type == END_OF_TRANSMISSION) {
                    //     // End util receive the END signal.
                    //     trans_finish = true;
                    // }

                } 
                    
            }

        }



    }

    // Final End
    bool is_End = false;
    bool transfer_result = true;
    int end_resent_times = 0;
    while (!is_End) {

        Packet end_packet;
        end_packet.type = END_OF_TRANSMISSION;
        end_packet.seq_num = next_seq_num;
        end_packet.size = 0;
        end_packet.crc32 = 0;
        std::vector<char> end_packet_data = serialize_packet(end_packet, nullptr);

        // 1st 
        sendto(sockfd, end_packet_data.data(), end_packet_data.size(), 0, (struct sockaddr*)&servaddr, len);
        
        int nfds = epoll_wait(epfd, events, 1, timeout); // this timeout should be at least 1 RTT
        if (nfds == -1) {
            std::cerr << "Error in epoll_wait: " << strerror(errno) << std::endl;
            close(sockfd);
            close(epfd);
            return -1;
        } else if (nfds == 0) {
            // while lopp to resend.
            if (end_resent_times > MAX_RESEND_TIMES) {
                // Connect fail
                is_End = false;
                transfer_result = false;
                // FAILED. The ONLY FAIL situation.
            }
            end_resent_times++;
        } else {
            // get ACK
            ssize_t count = 0;
            std::vector<char> ack_buffer(MTU);
            std::vector<std::pair<int,int>> missing_intervals;
            missing_intervals.clear();

            // 2nd
            if((count = recvfrom(sockfd, ack_buffer.data(), ack_buffer.size(), 0, (struct sockaddr *)&servaddr, &len)) > 0) {
                ack = deserialize_Ack(ack_buffer, missing_intervals);
                if (ack.type == END_OF_TRANSMISSION) {
                    bool condition = true;
                    // resend again.
                    while (condition) {
                        // 3rd
                        sendto(sockfd, end_packet_data.data(), end_packet_data.size(), 0, (struct sockaddr*)&servaddr, len);
                        int ews = epoll_wait(epfd, events, 1, timeout*2); // this should be 2MSL
                        if (ews == -1) {
                            perror("epoll_wait failed");
                            return -1;
                        } else if (ews == 0) {
                            condition = false;
                            is_End = true;
                        } else {
                            // get ACK
                            ssize_t count = 0;
                            std::vector<char> ack_buffer(MTU);
                            missing_intervals.clear();
                            // don't have to.
                            if((count = recvfrom(sockfd, ack_buffer.data(), ack_buffer.size(), 0, (struct sockaddr *)&servaddr, &len)) > 0) {
                                ack = deserialize_Ack(ack_buffer, missing_intervals);
                                if (ack.type == END_OF_TRANSMISSION) {
                                    condition = false;
                                    is_End = true;
                                }
                            }
                        }
                    }
                }

            }
        }
                

    }

    file.close();
    close(sockfd);
    close(epfd);

    if (transfer_result == false) {
        std::cout << "File tranmission End because of bad network connection!" << std::endl;
    }

    std::cout << "File tranmission Complete!" << std::endl;

    auto end = std::chrono::system_clock::now();
    auto duration2 = end.time_since_epoch();
    auto end_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration2).count();
    std::string transfering_duration_millis_str = std::to_string(end_millis - begin_millis);
    double bandwidth = (filesize * 8.0) / ((end_millis - begin_millis) / 1000.0) / 1e6;

    std::cout << "File size：" << filesize / 1000 / 1000 <<  "MiB！Time cost：" << transfering_duration_millis_str << "ms！Average Speed：" << bandwidth << "Mib/s！Retransmission：" << resent_count << std::endl;

    return 0;
}
