#include "CFTPserver.h"

// 定义服务器的最大传输单元（MTU）和每个数据块的大小
#define SERVER_MTU 1400
#define DATA_SIZE 1400
#define WINDOW_SIZE 100  // 滑动窗口的大小

// 定义握手结构体，包含文件名、文件大小、客户端MTU和CRC32校验值
struct Handshake {
    char filename[256];   // 文件名，最多256字符
    long filesize;        // 文件大小（字节）
    int mtu;              // 客户端的MTU
    uint32_t crc32_hash;  // 用于校验文件的CRC32哈希值
};

// 定义数据块结构体，包含数据块序号和实际数据
struct DataBlock {
    int block_num;        // 数据块序号
    uint32_t crc32;       // 数据块的CRC32校验值 注意，这一行原本是在data数组下面的，但是由于data数组长度不定，万一包大小不是MTU，那么就可能被提前截断而读不到了。因此，我们不得不挪到data上面来。
    char data[DATA_SIZE]; // 数据块内容，最大大小为1400字节
};

// 定义ACK结构体，包含确认的最高序号和缺失包的位图
struct Ack {
    int highest_received_block;    // 已确认的最高数据块序号
    bool missing_blocks[WINDOW_SIZE];  // 缺失数据包的位图，表示哪些包没有收到
};

// CRC32查找表，用于加速CRC32校验的计算
uint32_t crc32_table[256];

// 初始化CRC32查找表，使用CRC32的多项式生成每个字节的校验值
void init_crc32_table() {
    uint32_t polynomial = 0xEDB88320; // CRC32 多项式
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 8; j > 0; j--) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial; // 如果最低位为1，进行异或运算
            } else {
                crc >>= 1; // 否则直接右移
            }
        }
        crc32_table[i] = crc;
    }
}

// 计算单个数据块的CRC32校验值，使用查找表进行加速
uint32_t calculate_crc32(const std::vector<char>& data, uint32_t crc = 0xFFFFFFFF) {
    for (size_t i = 0; i < data.size(); i++) {
        uint8_t byte = data[i]; // 获取数据的每一个字节
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF]; // 查表并更新CRC值
    }
    return crc;
}

// 计算整个文件的CRC32校验值
uint32_t calculate_file_crc32(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary); // 打开文件以二进制模式读取
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << file_path << std::endl;
        return 0;
    }

    init_crc32_table(); // 初始化CRC32查找表

    uint32_t crc = 0xFFFFFFFF; // 初始值
    std::vector<char> buffer(4096); // 每次读取4KB数据

    while (file.good()) {
        file.read(buffer.data(), buffer.size()); // 从文件中读取数据
        std::streamsize bytes_read = file.gcount(); // 获取实际读取的字节数
        if (bytes_read > 0) {
            buffer.resize(bytes_read); // 调整缓冲区大小以适应读取的数据量
            crc = calculate_crc32(buffer, crc); // 更新CRC32校验值
        }
    }

    return crc ^ 0xFFFFFFFF; // 返回最终的CRC32校验值
}

// 计算数据块的CRC32校验值
uint32_t calculate_block_crc32(const char* data, size_t length) {
    init_crc32_table(); // 初始化CRC32查找表

    uint32_t crc = 0xFFFFFFFF; // 初始值
    std::vector<char> buffer(data, data + length); // 将数据块转换为vector

    crc = calculate_crc32(buffer, crc); // 计算CRC32校验值

    return crc ^ 0xFFFFFFFF; // 返回最终的CRC32校验值
}

// 服务器程序的主函数
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return -1;
    }

    int port = atoi(argv[1]); // 从命令行参数中获取端口号

    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    int sockfd; // 服务器的socket文件描述符
    struct sockaddr_in servaddr, cliaddr; // 服务器和客户端的地址结构
    Handshake handshake; // 握手信息
    DataBlock block; // 数据块
    Ack ack; // ACK确认信息

    // 创建UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

    // 服务器地址初始化
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET; // 使用IPv4协议
    servaddr.sin_addr.s_addr = INADDR_ANY; // 接受来自任意地址的连接
    servaddr.sin_port = htons(port); // 设置服务器的端口号

    // 绑定socket到指定的端口
    if (bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        std::cerr << "Bind failed." << std::endl;
        return -1;
    }

    socklen_t len = sizeof(cliaddr); // 客户端地址长度

    // 接收客户端的握手信息，包含文件名、文件大小等
    recvfrom(sockfd, &handshake, sizeof(handshake), 0, (struct sockaddr*)&cliaddr, &len);
    std::cout << "Handshake received: " << std::endl;
    std::cout << "Filename: " << handshake.filename << std::endl;
    std::cout << "Filesize: " << handshake.filesize << std::endl;
    std::cout << "Client MTU: " << handshake.mtu << std::endl;

    // 确定使用的最小MTU
    int agreed_mtu = std::min(SERVER_MTU, handshake.mtu);
    std::cout << "Agreed MTU: " << agreed_mtu << std::endl;

    // 计算文件需要传输的总数据块数
    int total_blocks = (handshake.filesize + agreed_mtu - 1) / agreed_mtu;
    std::cout << "Total blocks to receive: " << total_blocks << std::endl;

    // 将协商好的MTU发送回客户端
    sendto(sockfd, &agreed_mtu, sizeof(agreed_mtu), 0, (struct sockaddr*)&cliaddr, len);

    // 创建一个临时文件来保存接收到的数据
    std::string millis_str = std::to_string(millis);
    std::string temp_file_name = "received_file" + millis_str;
    std::ofstream file(temp_file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open output file." << std::endl;
        close(sockfd);
        return -1;
    }

   int expected_seq_num = 0; // 期望的下一个数据块序号
    std::map<int, std::string> buffer; // 缓存乱序到达的包
    int received_count = 0; // 计数器，用于控制发送ACK的频率

    while (true) {
        memset(block.data, 0, sizeof(block.data)); // 清空数据缓冲区
        block.crc32=888; //注意，上面那一行我们用memset清空了数据缓冲区。但是，crc32这个字段并没有被清空。为了清空，我们赋任意值即可。赋值888仅仅为了能够帮助我们快速锁定问题，没有特殊含义。
        int n = recvfrom(sockfd, &block, sizeof(block), 0, (struct sockaddr*)&cliaddr, &len);
        // 只要来了数据包就该++count，不然无法触发当前的ACK策略
        received_count++;

        // 计算接收到的数据块的CRC32校验值
        uint32_t received_crc32 = calculate_block_crc32(block.data, n - sizeof(block.block_num) - sizeof(block.crc32));
        std::cout << "收到数据包了！包号：" << block.block_num << "预期CRC32：" << block.crc32 << "实际CRC32:" << received_crc32  << std::endl;

        // 校验数据块的CRC32值
        if (received_crc32 != block.crc32) {
            std::cerr << "CRC32 mismatch for block " << block.block_num << ". Expected: " << block.crc32 << ", Received: " << received_crc32 << std::endl;
            continue; // 忽略该数据块
        }

        std::cout << "expected_seq_num:" << expected_seq_num << std::endl;
        std::cout << "block.block_num:" << block.block_num << std::endl;
        // 如果接收到期望的包，按顺序写入文件
        if (block.block_num == expected_seq_num) {
            std::cout << "Received expected block " << block.block_num << std::endl;
            file.write(block.data, n - sizeof(block.block_num) - sizeof(block.crc32)); // 写入数据
            expected_seq_num++;

            // 检查缓存中是否有后续的数据包
            while (buffer.count(expected_seq_num)) {
                std::cout << "Writing buffered block " << expected_seq_num << std::endl;
                file.write(buffer[expected_seq_num].c_str(), agreed_mtu); // 从缓存写入数据
                buffer.erase(expected_seq_num);
                expected_seq_num++;
                received_count++;
            }
        } else if (block.block_num > expected_seq_num) {
            // 如果接收到的包序号大于期望的，缓存该包
            std::cout << "Buffered out-of-order block " << block.block_num << std::endl;
            buffer[block.block_num] = std::string(block.data, n - sizeof(block.block_num) - sizeof(block.crc32));
            
        }
        
        // 检查是否已经收到最后一个数据包
        if (block.block_num == total_blocks - 1) {
            std::cout << "Received last block (" << block.block_num << "). Sending ACK." << std::endl;

            // 构建最后的ACK并发送
            ack.highest_received_block = block.block_num;
            memset(ack.missing_blocks, 0, sizeof(ack.missing_blocks)); // 没有缺失块
            sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&cliaddr, len);

            std::cout << "File transfer completed." << std::endl;
            break;
        }

        // 每收到WINDOW_SIZE个包后发送ACK
        std::cout << "received_count:" << received_count << std::endl;
        if (received_count >= WINDOW_SIZE) {
            ack.highest_received_block = expected_seq_num - 1; // 收到的最高序号

            // 设置ACK中的缺失块信息
            memset(ack.missing_blocks, 0, sizeof(ack.missing_blocks));
            for (int i = 0; i < WINDOW_SIZE; i++) {
                if (buffer.count(expected_seq_num + i) == 0) {
                    ack.missing_blocks[i] = true;  // 标记为缺失包
                }
            }

            // 发送ACK给客户端
            sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&cliaddr, len);
            std::cout << "Sent ACK for block " << ack.highest_received_block << std::endl;

            // 重置计数器
            received_count = 0;
        }
    }

    file.close();
    close(sockfd);

    // 文件传输完成后，将临时文件重命名为正确的文件名，并验证CRC32校验
    if (std::rename(temp_file_name.c_str(), handshake.filename) == 0) {
        uint32_t crc32_hash = calculate_file_crc32(handshake.filename);
        std::cout << "File renamed successfully to " << handshake.filename << std::endl;
        std::cout << "Origin whole file's CRC32 is " << handshake.crc32_hash << std::endl;
        std::cout << "Copied file's CRC32 is " << crc32_hash << std::endl;
        if (crc32_hash == handshake.crc32_hash) {
            std::cout << "恭喜！文件校验通过！" << std::endl;
        } else {
            std::cout << "遗憾！文件校验失败！继续Debug！" << std::endl;
        }
    } else {
        std::perror("Error renaming file");
    }

    return 0;
}
