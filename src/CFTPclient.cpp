#include "CFTPclient.h"

#define WINDOW_SIZE 4000 // 滑动窗口大小
#define TIMEOUT 10   // 超时重传时间（ms）
#define MTU 1400

enum MessageType {
    HANDSHAKE_REQUEST,
    HANDSHAKE_RESPONSE,
    DATA_PACKET,
    ACK_PACKET,
    END_OF_TRANSMISSION // tranmission end.
};

// 定义握手结构体，用于在传输前与服务器交换信息
struct HandshakeMessage
{
    MessageType type;
    int window_size;
    int mtu;
    int file_name_length;
    // other extensible parameter.
};

struct Ack {
    MessageType type;
    int acked;              // 累计确认序列号
    int interval_count;     // 缺失区间的数量
    // SACK缺失区间列表不在这里
};

// packet header
struct Packet {                 // 定义数据包
    MessageType type;
    int seq_num;                // 包号/序号
    uint32_t crc32;             // 检查位
    int size;                   // 数据大小(字节数)
    // 数据额外定义
    // std::vector<char> data;     
};

// 序列化Packet
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

// 反序列化 Ack
Ack deserialize_Ack(const std::vector<char>& buffer, std::vector<std::pair<int, int>>& missing_intervals) {
    Ack ack;
    size_t offset = 0;

    // 反序列化 type
    int type_network;
    memcpy(&type_network, buffer.data() + offset, sizeof(int));
    ack.type = static_cast<MessageType>(ntohl(type_network));
    offset += sizeof(int);

    // 反序列化 acked
    int acked_network;
    memcpy(&acked_network, buffer.data() + offset, sizeof(int));
    ack.acked = ntohl(acked_network);
    offset += sizeof(int);

    // 反序列化 interval_count
    int interval_count_network;
    memcpy(&interval_count_network, buffer.data() + offset, sizeof(int));
    ack.interval_count = ntohl(interval_count_network);
    offset += sizeof(int);

    // 反序列化缺失区间列表
    missing_intervals.clear();
    for (int i = 0; i < ack.interval_count; ++i) {
        int start_network, end_network;
        memcpy(&start_network, buffer.data() + offset, sizeof(int));
        offset += sizeof(int);

        memcpy(&end_network, buffer.data() + offset, sizeof(int));
        offset += sizeof(int);

        int start = ntohl(start_network);
        int end = ntohl(end_network);

        missing_intervals.emplace_back(start, end);
    }

    return ack;
}

// 序列化HandShakeMessage
std::vector<char> serialize_handshake(const HandshakeMessage& msg, const std::string& file_name) {
    size_t total_size = sizeof(int) * 4 + file_name.size();
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

    // 序列化 file_name_length
    int file_name_length_network = htonl(file_name.size());
    memcpy(buffer.data() + offset, &file_name_length_network, sizeof(int));
    offset += sizeof(int);

    // 序列化文件名
    memcpy(buffer.data() + offset, file_name.data(), file_name.size());

    return buffer;
}

// 反序列化handshake
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

// 获取文件大小的函数，返回文件的字节数
long getFileSize(const char *filepath) {
    struct stat stat_buf;
    if (stat(filepath, &stat_buf) != 0)
    {
        return -1; // 获取失败，返回-1
    }
    return stat_buf.st_size; // 返回文件大小
}

// 从文件路径中提取文件名的函数
std::string getFileName(const std::string &filePath) {
    size_t pos = filePath.find_last_of("/\\"); // 查找最后一个路径分隔符的位置
    if (pos != std::string::npos)
    {
        return filePath.substr(pos + 1); // 返回文件名部分
    }
    return filePath; // 如果没有分隔符，返回整个路径
}

// 设置socket非阻塞
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

long long get_timestamp_millis() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;

    // auto now = std::chrono::system_clock::now();
    // auto duration = now.time_since_epoch();
    // return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// 计算CRC32校验和
uint32_t calculate_CRC32(const char* data, size_t length) {
    return crc32(0L, reinterpret_cast<const Bytef*>(data), length);
}

void resend_packet(int sockfd, struct sockaddr_in& servaddr, socklen_t len, int rseq_num, std::map<int, std::vector<char>>& fly_packets, std::map<int, timeval>& timeout_table) {
    int seq_num = rseq_num;
    ssize_t sent_size = sendto(sockfd, fly_packets[seq_num].data(), fly_packets[seq_num].size(),
                                        0, (struct sockaddr*)&servaddr, len);
    std::cout << "发送新包，包号" << seq_num << std::endl;           
    if (sent_size < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::cerr << "发送失败，发送缓冲区已满，将稍后重试" << std::endl;
        } else {
            std::cerr << "发送失败，错误信息：" << strerror(errno) << std::endl;
        }
    }
    // 记录发送时间，构建timeout_table。
    struct timeval send_time;
    gettimeofday(&send_time, NULL);
    timeout_table[seq_num] = send_time;
}


// 主函数，客户端程序入口
int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        // 如果参数数量不正确，输出使用方法
        std::cerr << "Usage: " << argv[0] << " <IP address> <port> <file_path>" << std::endl;
        return -1;
    }

    //下方3行代码：获取并持久化程序开始的时刻
    auto begin = std::chrono::system_clock::now();
    auto duration = begin.time_since_epoch();
    auto begin_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();


    const char *server_ip = argv[1];    // 服务器IP地址
    int port = atoi(argv[2]);           // 服务器端口号
    const char *file_path = argv[3];    // 要发送的文件路径

    int sockfd;                         // 套接字文件描述符
    struct sockaddr_in servaddr;        // 服务器地址结构
    Ack ack;                            // ACK结构

    // 从系统读。

    int agreed_mtu;                     // 与服务器协商后的MTU

    // 获取文件大小
    long filesize = getFileSize(file_path);
    if (filesize < 0) {
        std::cerr << "Failed to get file size." << std::endl;
        return -1;
    }

    // 创建UDP套接字
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

        // 设置非阻塞socket
    if (-1 == set_sock_nonblock(sockfd)) {
        close(sockfd);
        return -1;
    }

    // 设置服务器地址信息
    memset(&servaddr, 0, sizeof(servaddr));    // 清空结构体
    servaddr.sin_family = AF_INET;             // 使用IPv4
    servaddr.sin_port = htons(port);           // 设置端口号（网络字节序）
    servaddr.sin_addr.s_addr = inet_addr(server_ip); // 设置服务器IP地址
    socklen_t len = sizeof(servaddr);

    // 打开要发送的文件，二进制方式
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file." << std::endl;
        close(sockfd);
        return -1;
    }

    // 创建 epoll 实例
    int epfd = epoll_create1(0);  // 参数为 0 表示默认行为
    if (epfd == -1) {
        std::cerr << "Failed to create epoll instance: " << strerror(errno) << std::endl;
        close(sockfd);
        return -1;
    }

    // 注册套接字到 epoll 实例，监听 EPOLLIN 事件（可读事件）
    struct epoll_event ev;
    ev.events = EPOLLIN;  // 可读事件
    ev.data.fd = sockfd;  // 套接字描述符

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
        std::cerr << "Failed to add socket to epoll: " << strerror(errno) << std::endl;
        close(epfd);
        close(sockfd);
        return -1;
    }

    bool handshake_finish = false;
    int window_size = WINDOW_SIZE;  // 默认窗口大小
    std::string file_name_str = file_path; // 将文件名转换为字符串

    int base = 0;           // 滑动窗口的起始序号
    int next_seq_num = 0;   // 下一个要发送的数据块序号，但还没发。
    bool is_last_packet = false;    // 判断是不是最后一个packet
    bool trans_finish = false;        // 判断是否传输完毕
    int resent_count = 0; //定义一个变量用于计数重传包数（包括一切情况导致的重传）

    // 用于超时重传，记录某个packet的发送时间，发送时尽快发，不影响；需要判断超时时计算
    std::map<int, timeval> timeout_table;
    
    // 缓存已发送但未确认的packet，fly bytes。值是序列化后的数据
    std::map<int, std::vector<char>> fly_packets;

    // 待重传的队列
    std::set<int> retransmit_set;
    
    // 可动态调整or可从输入读
    int timeout = TIMEOUT;

    int test_rtt = 0;


    // HandShake
    while (!handshake_finish) {
        HandshakeMessage hs_req;
        hs_req.type = HANDSHAKE_REQUEST;
        hs_req.window_size = window_size;
        hs_req.mtu = MTU;
        hs_req.file_name_length = file_name_str.size();

        auto sent = std::chrono::system_clock::now();
        auto duration = sent.time_since_epoch();
        auto sent_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

        std::vector<char> hs_req_data = serialize_handshake(hs_req, file_name_str);
        sendto(sockfd, hs_req_data.data(), hs_req_data.size(), 0,
            (struct sockaddr*)&servaddr, len);
        std::cout << "发送握手请求, 文件名：" << file_name_str << std::endl;

        // Wait for handshake response
        struct epoll_event events[1];
        // 发送前先瞅一眼有没有ack，有的话处理，更新window再发，没的话do nothing
        int nfds = epoll_wait(epfd, events, 1, timeout);
        if (nfds == -1) {
            std::cerr << "epoll wait failed." << std::endl;
            close(sockfd);
            close(epfd);
            return -1;
        } else if (nfds == 0) {
            // 超时，重新发送握手请求
            std::cout << "握手超时，重新发送握手请求" << std::endl;
        } else {
            if (events[0].events & EPOLLIN) {
                // 接收握手响应
                std::vector<char> buffer(1024);
                ssize_t recv_size = recvfrom(sockfd, buffer.data(), buffer.size(), 0,
                                             (struct sockaddr*)&servaddr, &len);
                if (recv_size > 0) {
                    auto recv = std::chrono::system_clock::now();
                    auto duration2 = recv.time_since_epoch();
                    auto recv_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration2).count();
                    
                    // 测一个模糊的rtt
                    test_rtt = recv_millis - sent_millis;

                    buffer.resize(recv_size);
                    std::string dummy_file_name;    // response中不含文件名
                    HandshakeMessage handshake_resp = deserialize_handshake(buffer, dummy_file_name);
                    if (handshake_resp.type == HANDSHAKE_RESPONSE) {
                        // 握手成功，发送第一个数据包作为握手确认
                        handshake_finish = true;
                        window_size = handshake_resp.window_size; // 更新窗口大小
                        agreed_mtu = handshake_resp.mtu;
                        std::cout << "握手成功，窗口大小：" << window_size << " MTU: " << agreed_mtu << std::endl;

                        // 读取第一个数据包
                        size_t proto_header_size = sizeof(int) *3 + sizeof(uint32_t);
                        size_t desired_size = agreed_mtu - proto_header_size; // 每个数据包1KB
                        char* data_buffer = new char[desired_size];
                        file.read(data_buffer, desired_size);
                        size_t data_size = file.gcount();

                        // 创建第一个数据包
                        Packet packet;
                        packet.type = DATA_PACKET;
                        packet.seq_num = 0; // 从0开始
                        packet.size = data_size;
                        packet.crc32 = calculate_CRC32(data_buffer, data_size);

                        // 序列化
                        std::vector<char> serialized_data = serialize_packet(packet, data_buffer);

                        // 发送数据包
                        ssize_t sent_size = sendto(sockfd, serialized_data.data(), serialized_data.size(), 0,
                                                   (struct sockaddr*)&servaddr, len);
                        if (sent_size < 0) {
                            perror("发送失败");
                            close(sockfd);
                            close(epfd);
                            return -1;
                        }

                        // 记录发送时间
                        timeval send_time;
                        gettimeofday(&send_time, NULL);
                        timeout_table[0] = send_time;

                        // 将数据包存入缓存
                        fly_packets[0] = serialized_data;
                        std::cout << "发送数据包，序列号：" << packet.seq_num << std::endl;

                        delete[] data_buffer;
                        next_seq_num = 1; // 更新下一个序列号
                        base = 0;         // 初始化窗口起始序列号
                    }
                }
            }
        }

    }

    while (!trans_finish)
    {
        timeout = (test_rtt + timeout) / 2;
        // 发送新的数据包
        std::cout << "准备发新包, 下一个准备发的新包是next_seq_num is " << next_seq_num << std::endl;
        std::cout << "base+WINDOW_SIZE:" << base+WINDOW_SIZE << std::endl;
        std::cout << "is_last_packet:" << is_last_packet << std::endl;

        // 全丢了怎么办？没有一个回来，那就没有ACK触发，也需要重传。
        if (next_seq_num < base + WINDOW_SIZE && !is_last_packet)
        {
            // 每个数据包只从file读一次，放进我们的缓存，而不是每次移动指针重新去读！
            size_t protocol_header_size = sizeof(int) * 3 + sizeof(uint32_t); // 根据您的协议头部大小计算
            size_t desired_size = agreed_mtu - protocol_header_size; // 每个数据包的数据部分大小
            char* data_buffer = new char[desired_size];
            file.read(data_buffer, desired_size);
            size_t data_size = file.gcount();

            if (data_size == 0) {
                // File read complete
                is_last_packet = true;
                // Don't forget to release memory.
                delete[] data_buffer;
                std::cout << "文件读取完成" << std::endl;
                // break;
            } else {
                Packet packet;
                packet.type = DATA_PACKET;
                packet.seq_num = next_seq_num;
                packet.size = data_size;
                packet.crc32 = calculate_CRC32(data_buffer, data_size);
                // 发送端
                std::cout << "发送新包，包号" << packet.seq_num << ", Size = " << data_size << ", CRC32 = " << packet.crc32 << std::endl;

                // 序列化
                std::vector<char> serialized_data = serialize_packet(packet, data_buffer);
                
                // 发送数据
                ssize_t sent_size = sendto(sockfd, serialized_data.data(), serialized_data.size(),
                                            0, (struct sockaddr*)&servaddr, len);
                
                if (sent_size < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::cerr << "发送失败，发送缓冲区已满，将稍后重试" << std::endl;
                        // 可以在此处等待一段时间，或者记录需要重传的包，稍后再尝试发送
                        delete[] data_buffer;
                        continue;
                    } else {
                        std::cerr << "发送失败，错误信息：" << strerror(errno) << std::endl;
                        delete[] data_buffer;
                        return -1;
                    }
                }

                // 记录发送时间，构建timeout_table。
                struct timeval send_time;
                gettimeofday(&send_time, NULL);
                timeout_table[next_seq_num] = send_time;

                // 存入缓存
                fly_packets[next_seq_num] = serialized_data;

                // Never forgot delete[]
                delete[] data_buffer;
                next_seq_num++;
            }
            
        }

        struct epoll_event events[1];
        int nfds = epoll_wait(epfd, events, 1, timeout);
        std::cout << "epoll_wait return result:" << nfds << std::endl;
        if (nfds == -1) {
            std::cerr << "Error in epoll_wait: " << strerror(errno) << std::endl;
            close(sockfd);
            close(epfd);
            return -1;
        } else if (nfds == 0) {
            // 超时重新传。
            for (auto ite = fly_packets.begin(); ite != fly_packets.end(); ite++) {
                timeval now;
                gettimeofday(&now, NULL);
                int seq = ite->first;
                timeval send_time = timeout_table[seq];
                long elasped_time = (now.tv_sec - send_time.tv_sec) * 1000 +
                                    (now.tv_usec - send_time.tv_usec) / 1000;
                if (elasped_time >= 1000) {
                    // 超时了，重传数据包
                    ssize_t sent_size = sendto(sockfd, ite->second.data(), ite->second.size(),
                                        0, (struct sockaddr*)&servaddr, len);
                    if (sent_size < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            std::cerr << "重传失败，发送缓冲区已满，将稍后重试" << std::endl;
                            // 可以在此处等待一段时间，或者记录需要重传的包，稍后再尝试发送
                            continue; // 继续下一次循环，稍后重试
                        } else {
                            std::cerr << "重传失败，错误信息：" << strerror(errno) << std::endl;
                            close(sockfd);
                            close(epfd);
                            return -1;
                        }
                    }
                    // 重传后立刻更新timeout_table
                    gettimeofday(&timeout_table[seq], NULL);
                    std::cout << "elasped_time" << elasped_time << " Resent Packet, Seq Num: " << seq << std::endl;
                } else {
                    // break;
                }
            }
        } else {
            // 收到了ACK
            if (events[0].events & EPOLLIN) {
                // 套接字可读，处理读事件
                // 收到ACK
                ssize_t count = 0;
                // 这里应该能容下一个大的变长的ACK
                std::vector<char> ack_buffer(1024);
                std::vector<std::pair<int,int>> missing_intervals;
                if ((count = recvfrom(sockfd, ack_buffer.data(), ack_buffer.size(), 0, (struct sockaddr *)&servaddr, &len)) > 0) {
                    ack_buffer.resize(count);
                    // 反序列化
                    ack = deserialize_Ack(ack_buffer, missing_intervals);
                    
                    if (ack.type == ACK_PACKET) {
                        std::cout << "收到了ACK，ACK确认的是acked:" << ack.acked << std::endl;
                        std::cout << "缺失的区间数量:" << ack.interval_count << std::endl;
                        std::cout << "旧的Base值（窗口左侧）:" << base << std::endl;
                        std::cout << "下一个要发的新包号" << next_seq_num << std::endl;
                        std::cout << "收到的missing 区间" << std::endl;
                        for(const auto& interval : missing_intervals) {
                            std::cout << "(" << interval.first << "," << interval.second << ")" << std::endl;
                        }

                        // 更新窗口
                        if (ack.acked >= base) {
                            // 移除已确认的数据包
                            for(int seq = base; seq <= ack.acked; seq++) {
                                fly_packets.erase(seq);
                                timeout_table.erase(seq);
                            }
                            base = ack.acked + 1;
                        }

                        if (ack.interval_count > 0) {
                            struct timeval now;
                            gettimeofday(&now, NULL);
                            // 处理缺失的区间
                            for (const auto& interval : missing_intervals) {
                                for (int seq = interval.first; seq <= interval.second; seq++) {
                                    timeval send_time = timeout_table[seq];
                                    long elasped_time = (now.tv_sec - send_time.tv_sec) * 1000 +
                                                        (now.tv_usec - send_time.tv_usec) / 1000;
                                    if (seq >= base && seq < next_seq_num && elasped_time > 1000) {
                                        // 重发单个包。仅超时时重发
                                        resend_packet(sockfd, servaddr, len, seq, fly_packets, timeout_table);
                                    }
                                }
                            }
                        }

                        if (is_last_packet && base == next_seq_num) {
                            trans_finish = true;
                        }
                    
                    }

                } 
                else if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    std::cout << "当前批次已读取完毕。" << std::endl;
                } else {
                    // do nothing, for now.
                }
                    
            }

        }

    }

    // 通知接收端结束
    Packet end_packet;
    end_packet.type = END_OF_TRANSMISSION;
    end_packet.seq_num = next_seq_num;
    end_packet.size = 0;
    end_packet.crc32 = 0;
    std::vector<char> end_packet_data = serialize_packet(end_packet, nullptr);
    sendto(sockfd, end_packet_data.data(), end_packet_data.size(), 0, (struct sockaddr*)&servaddr, len);

    file.close();
    close(sockfd);
    close(epfd);
    std::cout << "文件传输完成" << std::endl;

    //下方5行代码：获取并持久化程序结束的时刻，并计算速率
    auto end = std::chrono::system_clock::now();
    auto duration2 = end.time_since_epoch();
    auto end_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration2).count();
    std::string transfering_duration_millis_str = std::to_string(end_millis - begin_millis);
    double bandwidth = (filesize * 8.0) / ((end_millis - begin_millis) / 1000.0) / 1e6;

    std::cout << "客户端已经发完了整个文件！文件大小：" << filesize / 1000 / 1000 <<  "MiB！用时：" << transfering_duration_millis_str << "毫秒！平均速率：" << bandwidth << "Mib/s！总共重传包数：" << resent_count << std::endl;
    std::cout << "File transfer completed. Sent END signal." << std::endl;

    return 0;
}
