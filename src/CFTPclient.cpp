#include "CFTPclient.h"

#define DATA_SIZE 1400
#define WINDOW_SIZE 5 // 滑动窗口大小
#define TIMEOUT 20000     // 超时重传时间（us）

// 定义握手结构体，用于在传输前与服务器交换信息
struct Handshake
{
    char filename[256];    // 文件名，最多256字符
    long filesize;         // 文件大小（字节）
    int mtu;               // 客户端的MTU（最大传输单元）
    uint32_t crc32_hash;   // 文件的CRC32校验值，用于完整性验证
};

// 定义数据块结构体，包含数据块序号和实际数据
struct DataBlock
{
    int block_num;         // 数据块的序号
    char data[DATA_SIZE];  // 实际数据内容，最大1400字节
};

// 定义ACK结构体，用于服务器反馈确认信息
struct Ack
{
    int highest_received_block;       // 服务器已确认接收到的最高数据块序号
    bool missing_blocks[WINDOW_SIZE]; // 服务器反馈的缺失数据块位图，标识哪些数据块需要重传
};

// 获取文件大小的函数，返回文件的字节数
long getFileSize(const char *filepath)
{
    struct stat stat_buf;
    if (stat(filepath, &stat_buf) != 0)
    {
        return -1; // 获取失败，返回-1
    }
    return stat_buf.st_size; // 返回文件大小
}

// 从文件路径中提取文件名的函数
std::string getFileName(const std::string &filePath)
{
    size_t pos = filePath.find_last_of("/\\"); // 查找最后一个路径分隔符的位置
    if (pos != std::string::npos)
    {
        return filePath.substr(pos + 1); // 返回文件名部分
    }
    return filePath; // 如果没有分隔符，返回整个路径
}

// 发送数据包的函数，返回发送是否成功
bool send_packet(int sockfd, DataBlock &block, int data_size, struct sockaddr_in &servaddr, socklen_t len)
{
    // 发送数据块，包括块序号和实际数据
    return sendto(sockfd, &block, sizeof(block.block_num) + data_size, 0, (struct sockaddr *)&servaddr, len) > 0;
}

// 预先计算 CRC32 查找表
uint32_t crc32_table[256];

// 初始化 CRC32 查找表，使用CRC32的多项式生成
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

// 计算单个数据块的 CRC32 校验值
uint32_t calculate_crc32(const std::vector<char>& data, uint32_t crc = 0xFFFFFFFF) {
    for (size_t i = 0; i < data.size(); i++) {
        uint8_t byte = data[i]; // 获取数据的每一个字节
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF]; // 查表并更新CRC值
    }
    return crc;
}

// 计算整个文件的 CRC32 校验值
uint32_t calculate_file_crc32(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary); // 以二进制方式打开文件
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << file_path << std::endl;
        return 0;
    }

    init_crc32_table(); // 初始化CRC32查找表

    uint32_t crc = 0xFFFFFFFF; // 初始值
    std::vector<char> buffer(4096); // 每次读取4KB的数据

    while (file.good()) {
        file.read(buffer.data(), buffer.size()); // 读取数据到缓冲区
        std::streamsize bytes_read = file.gcount(); // 实际读取的字节数
        if (bytes_read > 0) {
            buffer.resize(bytes_read); // 调整缓冲区大小以匹配读取的数据
            crc = calculate_crc32(buffer, crc); // 更新CRC32值
        }
    }

    return crc ^ 0xFFFFFFFF; // 返回最终的CRC32校验值
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

    const char *server_ip = argv[1];    // 服务器IP地址
    int port = atoi(argv[2]);           // 服务器端口号
    const char *file_path = argv[3];    // 要发送的文件路径

    int sockfd;                         // 套接字文件描述符
    struct sockaddr_in servaddr;        // 服务器地址结构
    DataBlock block;                    // 数据块结构
    Handshake handshake;                // 握手信息结构
    Ack ack;                            // ACK结构
    int client_mtu = 1400;              // 客户端的MTU，假设为1400字节
    int agreed_mtu;                     // 与服务器协商后的MTU

    // 获取文件大小
    long filesize = getFileSize(file_path);
    if (filesize < 0)
    {
        std::cerr << "Failed to get file size." << std::endl;
        return -1;
    }

    // 创建UDP套接字
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

    // 设置服务器地址信息
    memset(&servaddr, 0, sizeof(servaddr));    // 清空结构体
    servaddr.sin_family = AF_INET;             // 使用IPv4
    servaddr.sin_port = htons(port);           // 设置端口号（网络字节序）
    servaddr.sin_addr.s_addr = inet_addr(server_ip); // 设置服务器IP地址

    // 准备握手信息
    std::string file_name = getFileName(file_path); // 获取文件名
    strncpy(handshake.filename, file_name.c_str(), sizeof(handshake.filename) - 1); // 复制文件名到握手结构
    handshake.filename[sizeof(handshake.filename) - 1] = '\0'; // 确保字符串以'\0'结尾
    handshake.filesize = filesize;           // 设置文件大小
    handshake.mtu = client_mtu;              // 设置客户端MTU
    handshake.crc32_hash = calculate_file_crc32(file_path); // 计算文件的CRC32校验值

    // 发送握手信息给服务器
    sendto(sockfd, &handshake, sizeof(handshake), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    std::cout << "Handshake sent with file info and MTU." << std::endl;

    // 接收服务器返回的协商后的MTU
    recvfrom(sockfd, &agreed_mtu, sizeof(agreed_mtu), 0, nullptr, nullptr);
    std::cout << "Agreed MTU received: " << agreed_mtu << std::endl;

    // 使用协商后的MTU进行数据发送
    std::ifstream file(file_path, std::ios::binary); // 以二进制方式打开文件
    if (!file.is_open())
    {
        std::cerr << "Failed to open file." << std::endl;
        close(sockfd);
        return -1;
    }

    int base = 0;         // 滑动窗口的起始序号
    int next_seq_num = 0; // 下一个要发送的数据块序号
    socklen_t len = sizeof(servaddr);

    while (base * agreed_mtu < filesize)
    {
        // 在窗口内发送数据包
        std::cout << "****************************" << std::endl;
        std::cout << "Before send, next_seq_num is " << next_seq_num << std::endl;
        std::cout << "base+WINDOW_SIZE:" << base+WINDOW_SIZE << std::endl;
        // std::cout << "next_seq_num * agreed_mtu:" << next_seq_num * agreed_mtu << std::endl;
        std::cout << "****************************" << std::endl;
        while (next_seq_num < base + WINDOW_SIZE && next_seq_num * agreed_mtu < filesize)
        {

            std::cout << "before seekg, move pointer to : " << next_seq_num*agreed_mtu << std::endl;
            file.seekg(next_seq_num * agreed_mtu); // 移动文件读取指针到相应位置

            // 计算本次读取的数据大小，最后一个数据块可能小于MTU
            int bytes_to_read = std::min(agreed_mtu, static_cast<int>(filesize - next_seq_num * agreed_mtu));
            std::cout << "before file read, bytes_to_read:" << bytes_to_read << std::endl;
            file.read(block.data, bytes_to_read); // 读取数据到数据块
            std::streamsize bytes_read = file.gcount(); // 实际读取的字节数

            block.block_num = next_seq_num; // 设置数据块序号
            send_packet(sockfd, block, bytes_read, servaddr, len); // 发送数据包
            std::cout << "Sent block " << block.block_num << " with " << bytes_read << " bytes." << std::endl;
            next_seq_num++; // 序号加1
        }

        std::cout << "********* Out Window send while loop*********" << std::endl;

        // 使用 select 函数等待ACK或超时处理
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0; // 设置超时时间
        timeout.tv_usec = TIMEOUT;  // 20ms

        // 获取当前时间点
        auto now = std::chrono::system_clock::now();
        // 将时间点转换为 time_t 类型
        std::time_t currentTime = std::chrono::system_clock::to_time_t(now);

        // 打印当前时间（格式化输出）debug用
        std::cout << "Current time: " << std::ctime(&currentTime);
        std::cout << "timeout.tv_usec:" << timeout.tv_usec << std::endl;
        if (sockfd < 0) {
            std::cerr << "Invalid socket file descriptor" << std::endl;
            return -1;
        }

        int result = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        std::cout << "select return result:" << result << std::endl;

        if (result > 0 && FD_ISSET(sockfd, &readfds))
        {
            // 收到ACK
            recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&servaddr, &len);
            std::cout << "******************ACK received****************" << std::endl;
            std::cout << "ack.highest_received_block:"  << ack.highest_received_block << std::endl; 
            std::cout << "missing_blocks bool array: ";
            for(int i=0;i<sizeof(ack.missing_blocks);i++){
                std::cout<<ack.missing_blocks[i] << ",";
            }

            // 更新窗口起始序号
            if (ack.highest_received_block >= base) {
                base = ack.highest_received_block + 1;
            }

            // 重传ACK中标记为缺失的包
            for (int i = 0; i < WINDOW_SIZE; i++)
            {
                int seq_num_to_resend = base + i; // 计算需要重传的序号
                if (ack.missing_blocks[i] && seq_num_to_resend < next_seq_num) // 确保该序号已发送过
                {
                    if (seq_num_to_resend * agreed_mtu < filesize)
                    {
                        file.seekg(seq_num_to_resend * agreed_mtu); // 定位到相应位置
                        int bytes_to_read = std::min(agreed_mtu, static_cast<int>(filesize - seq_num_to_resend * agreed_mtu));
                        file.read(block.data, bytes_to_read);
                        std::streamsize bytes_read = file.gcount();
                        block.block_num = seq_num_to_resend;
                        send_packet(sockfd, block, bytes_read, servaddr, len); // 重传数据包
                        std::cout << "Resent block " << block.block_num << " with " << bytes_read << " bytes due to missing." << std::endl;
                    }
                }
            }
        }
        else if (result == 0)
        {
            // 超时处理，重传窗口内的所有包
            std::cout << "Timeout: resending window from base " << base << std::endl;
            for (int i = base; i < next_seq_num; i++)
            {
                std::cout << "for-resend porition I:" << i << " while next_seq_num is " << next_seq_num << std::endl;
                file.seekg(i * agreed_mtu); // 定位到相应位置
                int bytes_to_read = std::min(agreed_mtu, static_cast<int>(filesize - i * agreed_mtu));
                file.read(block.data, bytes_to_read);
                std::streamsize bytes_read = file.gcount();
                block.block_num = i;
                send_packet(sockfd, block, bytes_read, servaddr, len); // 重传数据包
                std::cout << "Resent block " << block.block_num << " with " << bytes_read << " bytes due to timeout." << std::endl;
            }
        }
        else
        {
            // select 函数出错
            std::cerr << "Error in select." << std::endl;
            break;
        }
    }

    // 发送结束标志，通知服务器文件发送完毕
    memset(block.data, 0, sizeof(block.data)); // 清空数据缓冲区
    strcpy(block.data, "END");                 // 设置结束标志
    block.block_num = next_seq_num;            // 给结束包分配一个序号
    sendto(sockfd, &block, sizeof(block.block_num) + strlen("END"), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

    std::cout << "File transfer completed. Sent END signal." << std::endl;

    file.close(); // 关闭文件
    close(sockfd); // 关闭套接字
    return 0;
}
