#include "CFTPserver.h"

// 定义服务器的最大传输单元（MTU）和每个数据块的大小
#define MAX_PACKET_SIZE 1400
#define WINDOW_SIZE 2000  // 滑动窗口的大小
#define TIMEOUT 10 // ms

enum MessageType {
    HANDSHAKE_REQUEST,
    HANDSHAKE_RESPONSE,
    DATA_PACKET,
    ACK_PACKET,
    END_OF_TRANSMISSION // tranmission end.
};

// 定义握手结构体，包含文件名、文件大小、客户端MTU和CRC32校验值
struct HandshakeMessage {
    MessageType type;
    int window_size;
    int mtu;
    int64_t file_size;
    int file_name_length;  // 文件名长度
};

struct Packet {
    MessageType type;
    int seq_num;        // 序列号
    uint32_t crc32;     // 校验和
    int size;           // 数据大小（字节数）
    // 数据不作为结构体成员，单独处理
};

struct Ack {
    MessageType type;
    int acked;              // 累计确认序列号
    int interval_count;     // 缺失区间的数量
    // SACK缺失区间列表不在这里
};

// 计算CRC32校验和
uint32_t calculate_CRC32(const char* data, size_t length) {
    return crc32(0L, reinterpret_cast<const Bytef*>(data), length);
}

// 序列化ACK
std::vector<char> serialize_Ack(const Ack& ack, const std::vector<std::pair<int, int>>& missing_intervals) {
    size_t total_size = sizeof(int) * 3 + missing_intervals.size() * sizeof(int) * 2;
    std::vector<char> buffer(total_size);

    size_t offset = 0;

    // 序列化 type
    int type_network = htonl(ack.type);
    memcpy(buffer.data() + offset, &type_network, sizeof(int));
    offset += sizeof(int);

    // 序列化 acked
    int acked_network = htonl(ack.acked);
    memcpy(buffer.data() + offset, &acked_network, sizeof(int));
    offset += sizeof(int);

    // 序列化 interval_count
    int interval_count_network = htonl(ack.interval_count);
    memcpy(buffer.data() + offset, &interval_count_network, sizeof(int));
    offset += sizeof(int);

    // 序列化缺失区间列表
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

// 反序列化 Packet
Packet deserialize_packet(const std::vector<char>& buffer, char*& data) {
    Packet packet;
    size_t offset = 0;
    
    // 反序列化 type
    int type_network;
    memcpy(&type_network, buffer.data() + offset, sizeof(int));
    packet.type = static_cast<MessageType>(ntohl(type_network));
    offset += sizeof(int);

    // 反序列化 seq_num
    int seq_num_network;
    memcpy(&seq_num_network, buffer.data() + offset, sizeof(int));
    packet.seq_num = ntohl(seq_num_network);
    offset += sizeof(int);

    // 反序列化 crc32
    uint32_t crc32_network;
    memcpy(&crc32_network, buffer.data() + offset, sizeof(uint32_t));
    packet.crc32 = ntohl(crc32_network);
    offset += sizeof(uint32_t);

    // 反序列化 size
    int size_network;
    memcpy(&size_network, buffer.data() + offset, sizeof(int));
    packet.size = ntohl(size_network);
    offset += sizeof(int);

    // 反序列化 data
    if (packet.size > 0) {
        data = new char[packet.size];
        memcpy(data, buffer.data() + offset, packet.size);
    } else {
        data = nullptr;
    }

    return packet;
}

// 序列化HandShakeMessage
std::vector<char> serialize_handshake(const HandshakeMessage& msg, const std::string& file_name) {
    size_t total_size = sizeof(int) * 4 + sizeof(int64_t) + file_name.size();
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

void send_ack(int sockfd, struct sockaddr_in& cliaddr, socklen_t len, int acked_num, const std::vector<std::pair<int, int>>& missing_intervals) {
    Ack ack;
    ack.type = ACK_PACKET;
    ack.acked = acked_num;
    ack.interval_count = missing_intervals.size();
    
    // 序列化 ACK
    std::vector<char> serialized_ack = serialize_Ack(ack, missing_intervals);

    // 发送 ACK
    ssize_t sent_size = sendto(sockfd, serialized_ack.data(), serialized_ack.size(), 0, (struct sockaddr*)&cliaddr, len);
    if (sent_size < 0) {
        perror("发送ACK失败");
        return ;
    }

    //  std::cout << "Sent ACK，Accumu acked Seq: " << ack.acked << " sent_size: " << sent_size << std::endl;
    // if (ack.interval_count > 0) {
    //      std::cout << "，Missing Intervals Count: " << ack.interval_count;
    //     for (const auto& interval : missing_intervals) {
    //          std::cout << " (" << interval.first << ", " << interval.second << ")";
    //     }
    // }
    //  std::cout << std::endl;
}

/**
 * @brief 合并连续的缺失序列号为区间
 * 
 * @param missing_intervals 原始的缺失序列号列表，每个元素为一个序列号对 (start, end)
 * @return std::vector<std::pair<int, int>> 合并后的缺失区间列表
 */
std::vector<std::pair<int, int>> merge_intervals(const std::vector<std::pair<int, int>>& intervals) {
    std::vector<std::pair<int, int>> merged_intervals;

    if (intervals.empty()) {
        return merged_intervals;
    }

    // 复制并排序区间
    std::vector<std::pair<int, int>> sorted_intervals = intervals;
    std::sort(sorted_intervals.begin(), sorted_intervals.end(),
              [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                  return a.first < b.first;
              });

    int current_start = sorted_intervals[0].first;
    int current_end = sorted_intervals[0].second;

    for (size_t i = 1; i < sorted_intervals.size(); ++i) {
        if (sorted_intervals[i].first <= current_end + 1) {
            // 合并区间
            current_end = std::max(current_end, sorted_intervals[i].second);
        } else {
            // 不可合并，保存当前区间并开始新的区间
            merged_intervals.emplace_back(current_start, current_end);
            current_start = sorted_intervals[i].first;
            current_end = sorted_intervals[i].second;
        }
    }

    // 添加最后一个区间
    merged_intervals.emplace_back(current_start, current_end);

    return merged_intervals;
}


// 服务器程序的主函数
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return -1;
    }

    int port = atoi(argv[1]); // 从命令行参数中获取端口号

    // auto begin = std::chrono::system_clock::now();
    // auto duration = begin.time_since_epoch();
    // auto begin_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    int sockfd; // 服务器的socket文件描述符
    struct sockaddr_in servaddr, cliaddr; // 服务器和客户端的地址结构
    socklen_t len = sizeof(cliaddr); // 客户端地址长度
    int agreed_mtu;
    int timeout = TIMEOUT;

    // size_t protocol_header_size = sizeof(int) * 3 + sizeof(uint32_t);
    // size_t max_data_size;

    // 创建UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

    // 设置非阻塞模式
    if (-1 == set_sock_nonblock(sockfd)) {
        close(sockfd);
        return -1;
    }

    // 服务器地址初始化
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));
    servaddr.sin_family = AF_INET; // 使用IPv4协议
    servaddr.sin_addr.s_addr = INADDR_ANY; // 接受来自任意地址的连接
    servaddr.sin_port = htons(port); // 设置服务器的端口号

    // 绑定socket到指定的端口
    if (bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        std::cerr << "Bind failed." << std::endl;
        return -1;
    }

    // 打开输出文件
    std::ofstream file;
    
    // 按顺序期望接到的下一个序列号
    int expected_seq_num = 0;
    // 当前成功收到的最大序列号
    int max_received_num = 0;

    bool trans_finish = false;
    long filesize;

    // receiver window, x[seq] = data 。缓存收到的data数据。
    std::map<int, std::vector<char>> recv_buffer;
    // 这里可以得知已经成功发到的包号。

    // 记录校验失败的序列号集合
    std::set<int> wrong_seqs_set;

    // 创建epoll实例
    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll创建失败");
        close(sockfd);
        return -1;
    }

    // 将Scoket加入epoll监控列表
    struct epoll_event ev;
    ev.events = EPOLLIN; // 监控可读事件
    ev.data.fd = sockfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
        perror("epoll_ctl失败");
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
            perror("epoll_wait失败");
            close(sockfd);
            close(epfd);
            return -1;
        }

        if (events[0].events & EPOLLIN) {
            // 接收握手请求
            std::vector<char> buffer(MAX_PACKET_SIZE);
            ssize_t recv_size = recvfrom(sockfd, buffer.data(), buffer.size(), 0,
                                         (struct sockaddr*)&cliaddr, &len);
            if (recv_size > 0) {
                buffer.resize(recv_size);
                HandshakeMessage hs_req = deserialize_handshake(buffer, file_name_str);
                if (hs_req.type == HANDSHAKE_REQUEST) {
                    // 接到握手req，文件名已知
                    //  std::cout << "接收到握手请求，文件名：" << file_name_str << "MTU:" << hs_req.mtu <<std::endl;

                    agreed_mtu = hs_req.mtu;
                    timeout = agreed_mtu;
                    filesize = hs_req.file_size;

                    std::cout << "Handshake Connect, filename: " << file_name_str << std::endl;
                    std::cout << "Filesize: " << filesize << " agreed_mtu" << agreed_mtu << std::endl;
                    
                    // decide a mtu

                    file.open(file_name_str, std::ios::out | std::ios::binary);
                    if (!file.is_open()) {
                        perror("文件打开失败");
                        close(sockfd);
                        return -1;
                    }
                    
                    // 发送握手响应
                    HandshakeMessage hs_resp;
                    hs_resp.type = HANDSHAKE_RESPONSE;
                    hs_resp.window_size = WINDOW_SIZE;
                    hs_resp.file_name_length = 0;
                    hs_resp.mtu = agreed_mtu;
                    hs_resp.file_size = filesize;

                    std::vector<char> hs_resp_data = serialize_handshake(hs_resp, "");
                    sendto(sockfd, hs_resp_data.data(), hs_resp_data.size(), 0,
                           (struct sockaddr*)&cliaddr, len);
                    //  std::cout << "发送握手响应" << std::endl;
                    handshake_finish = true;
                    
                }
            }
        }
    }

    int ack_header_size = sizeof(int) * 3;

    // 引入时间控制变量
    auto last_SACK_time = std::chrono::steady_clock::now();
    const std::chrono::milliseconds ack_interval(timeout); // 500 毫秒
    bool receive_END = false;

    while (!trans_finish) {
        // 使用epoll等待事件
        struct epoll_event events[1];
        int nfds = epoll_wait(epfd, events, 1, timeout);
        if (nfds == -1) {
            perror("epoll_wait失败");
            close(sockfd);
            close(epfd);
            return -1;
        } else if (nfds == 0) {
            // epoll_wait 超时
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
                    perror("发送ACK失败");
                }
                trans_finish = true;
                continue;
            }

            if (expected_seq_num >= max_received_num) {
                // 一般情况，发个ACK刺激一下。interval?
                // 此处missing_intervals为空。
                send_ack(sockfd, cliaddr, len, expected_seq_num-1, missing_intervals);
            } else {
                // 说明真的有一段时间啥也没干了，超时了（如果收到结束数据包的话应该已经出循环了，可是没有）
                // 构建发SACK刺激一下
                // 统计当前窗口内的缺失包
                for (int seq = expected_seq_num; seq < expected_seq_num + WINDOW_SIZE && seq <= max_received_num; ++seq) {
                    if (recv_buffer.find(seq) == recv_buffer.end() && wrong_seqs_set.find(seq) == wrong_seqs_set.end()) {
                        missing_intervals.emplace_back(seq, seq);
                    }
                }
                // 添加 CRC 校验失败的序列号
                for (const int& corrupted_seq : wrong_seqs_set) {
                    missing_intervals.emplace_back(corrupted_seq, corrupted_seq);
                }
                missing_intervals = merge_intervals(missing_intervals);
                int interval_size = sizeof(int) * 2;
                int max_intervals = (agreed_mtu - ack_header_size) / (interval_size * 2); // 直接一半，冗余给序列化变化
                int total_intervals = missing_intervals.size();
                int sent_intervals = 0;
                // 分批发送缺失区间
                for (size_t i = 0; i < total_intervals; i += max_intervals) {
                    // 截取当前批次的缺失区间
                    auto end_it = std::min(missing_intervals.begin() + i + max_intervals, missing_intervals.end());
                    std::vector<std::pair<int, int>> current_intervals(missing_intervals.begin() + i, end_it);

                    send_ack(sockfd, cliaddr, len, expected_seq_num-1, current_intervals);
                }
                missing_intervals.clear();
                last_SACK_time = std::chrono::steady_clock::now();   
            }
        } else {
            // 处理所有触发的事件,这里只有一个
            if (events[0].events & EPOLLIN) {
                // 收到了数据包
                std::vector<char> buffer(MAX_PACKET_SIZE);
                int count = 0;
                while((count = recvfrom(sockfd, buffer.data(), buffer.size(), 0, (struct sockaddr*)&cliaddr, &len)) > 0) {
                    // 全部处理
                    buffer.resize(count);
                    // 反序列化
                    char *data = nullptr;
                    Packet packet = deserialize_packet(buffer, data);
                    //  std::cout << "监听接到了数据" << std::endl;
                    std::vector<std::pair<int,int>> missing_intervals;
                    
                    if (packet.type == DATA_PACKET) {
                        //  std::cout << "接到了数据包，包号 Seq Num: " << packet.seq_num << std::endl;
                        // 校验CRC32
                        uint32_t crc32_calculated = calculate_CRC32(data, packet.size);
                        //  std::cout << "Receiver: Seq Num = " << packet.seq_num << ", Data Size = " << packet.size << ", Received CRC32 = " << packet.crc32 << ", Calculated CRC32 = " << crc32_calculated << std::endl;
                        if (packet.crc32 == crc32_calculated) {
                            // 校验成功
                            // 如果该序列号之前就已经在wrong_seqs_set中了，移除它
                            if (wrong_seqs_set.find(packet.seq_num) != wrong_seqs_set.end()) {
                                wrong_seqs_set.erase(packet.seq_num);
                            }
                            if (packet.seq_num == expected_seq_num) {
                                // 写文件
                                std::vector<char> tmp = std::vector<char>(data, data + packet.size);
                                file.write(tmp.data(), tmp.size());
                                expected_seq_num++;
                                // 开始尝试写入连续数据包
                                // 检查缓存，只要缓存中存在了连续的下一个seq号：expected_seq_num，就写
                                while (recv_buffer.find(expected_seq_num) != recv_buffer.end()) {
                                    // 找到了期待的下一个seq号。
                                    // 没检查连续
                                    file.write(recv_buffer[expected_seq_num].data(), recv_buffer[expected_seq_num].size());
                                    //  std::cout << "Write the packet to file, Seq Num: " << expected_seq_num << std::endl;
                                    // 移除已经写入的包
                                    recv_buffer.erase(expected_seq_num);
                                    expected_seq_num++;
                                }
                                if (max_received_num < expected_seq_num) max_received_num = expected_seq_num;
                                // 直接ACK
                                send_ack(sockfd, cliaddr, len, packet.seq_num, missing_intervals);
                            } else if (packet.seq_num > expected_seq_num) {
                                // 缓存没有，那就是比expected_seq高，就可以放进缓存。
                                if (recv_buffer.find(packet.seq_num) == recv_buffer.end()) {
                                    // 如果没找到，就加入缓存。
                                    recv_buffer[packet.seq_num] = std::vector<char>(data, data + packet.size);
                                    if (packet.seq_num > max_received_num) {
                                        max_received_num = packet.seq_num;
                                    }
                                    // 之加入缓存，等一等。
                                    // 如果超时了就发一下，不超时不发.
                                    if (std::chrono::steady_clock::now() > last_SACK_time + ack_interval) {
                                        // 超时了，重发。
                                        for (int seq = expected_seq_num; seq < expected_seq_num + WINDOW_SIZE && seq <= max_received_num; ++seq) {
                                            if (recv_buffer.find(seq) == recv_buffer.end() && wrong_seqs_set.find(seq) == wrong_seqs_set.end()) {
                                                missing_intervals.emplace_back(seq, seq);
                                            }
                                        }
                                        // 添加 CRC 校验失败的序列号
                                        for (const int& corrupted_seq : wrong_seqs_set) {
                                            missing_intervals.emplace_back(corrupted_seq, corrupted_seq);
                                        }
                                        missing_intervals = merge_intervals(missing_intervals);
                                        // std::cout << "missing_intervals里有多少个pair" << missing_intervals.size() << std::endl;
                                        int interval_size = sizeof(int) * 2;
                                        int max_intervals = (agreed_mtu - ack_header_size) / (interval_size * 2); // 直接一半，冗余给序列化变化
                                        int total_intervals = missing_intervals.size();
                                        int sent_intervals = 0;
                                        // 分批发送缺失区间
                                        for (size_t i = 0; i < total_intervals; i += max_intervals) {
                                            // 截取当前批次的缺失区间
                                            auto end_it = std::min(missing_intervals.begin() + i + max_intervals, missing_intervals.end());
                                            std::vector<std::pair<int, int>> current_intervals(missing_intervals.begin() + i, end_it);

                                            send_ack(sockfd, cliaddr, len, expected_seq_num-1, current_intervals);
                                        }
                                        missing_intervals.clear();
                                        last_SACK_time = std::chrono::steady_clock::now(); 
                                    }
                                    // 不超时就不管，丢给超时重传来处理。
                                }
                            } else {
                                // expect_num > packet.seq_num
                                // 说明之前的ACK可能丢失了，client没有更新base，重新把当前期待的下一个expected发回去acked一下
                                // 此处missing_intervals为空。
                                send_ack(sockfd, cliaddr, len, expected_seq_num-1, missing_intervals);
                            }

                        } else {
                            std::cerr << "CRC32 校验失败， Seq：" << packet.seq_num << std::endl;
                            wrong_seqs_set.insert(packet.seq_num);
                            // 立刻重传一个单点的SACK
                            missing_intervals.emplace_back(packet.seq_num, packet.seq_num);
                            send_ack(sockfd, cliaddr, len, packet.seq_num - 1, missing_intervals);
                        }
                    } else if (packet.type == END_OF_TRANSMISSION){
                        if (packet.seq_num == expected_seq_num) {
                            receive_END = true;
                            // 接收完了，结束
                            // 直接一个简单的ACK
                            Ack ack;
                            ack.type = END_OF_TRANSMISSION;
                            ack.acked = expected_seq_num;
                            ack.interval_count = 0;
                            // 序列化 ACK
                            std::vector<char> serialized_ack = serialize_Ack(ack, missing_intervals);
                            // 发送 ACK
                            ssize_t sent_size = sendto(sockfd, serialized_ack.data(), serialized_ack.size(), 0, (struct sockaddr*)&cliaddr, len);
                            if (sent_size < 0) {
                                perror("发送ACK失败");
                            }
                        }
                        
                    }

                    delete[] data;

                }
                // 如果没有读到，放之任之让超时来处理。
            }
        }
    }


    file.close();
    close(sockfd);
    close(epfd);

    return 0;
}
