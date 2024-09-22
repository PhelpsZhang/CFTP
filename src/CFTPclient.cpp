#include "CFTPclient.h"

#define DATA_SIZE 1400
#define WINDOW_SIZE 100 // Sliding window size
#define TIMEOUT 20    // Timeout for retransmission (ms)
#define MAX_EVENTS 10  // Maximum number of events

// Define handshake structure to exchange information with the server before transmission
struct Handshake
{
    char filename[256];    // Filename, up to 256 characters
    long filesize;         // File size (bytes)
    int mtu;               // Client's MTU (Maximum Transmission Unit)
    uint32_t crc32_hash;   // CRC32 checksum for file integrity verification
};

// Define data block structure, containing block number and actual data
struct DataBlock
{
    int block_num;         // Block number
    uint32_t crc32;        // CRC32 checksum of the block (Note: this line was originally below the data array, but since the data array length is not fixed, it might be truncated if the packet size is not MTU, so it was moved above the data)
    char data[DATA_SIZE];  // Actual data content, up to 1400 bytes
};

// Define ACK structure for server feedback confirmation
struct Ack
{
    int highest_received_block;       // Highest block number received by the server
    bool missing_blocks[WINDOW_SIZE]; // Bitmap of missing blocks, indicating which blocks need to be retransmitted
};

// Function to get file size, returns file size in bytes
long getFileSize(const char *filepath)
{
    struct stat stat_buf;
    if (stat(filepath, &stat_buf) != 0)
    {
        return -1; // Failed to get file size, return -1
    }
    return stat_buf.st_size; // Return file size
}

// Function to extract filename from file path
std::string getFileName(const std::string &filePath)
{
    size_t pos = filePath.find_last_of("/\\"); // Find the position of the last path separator
    if (pos != std::string::npos)
    {
        return filePath.substr(pos + 1); // Return the filename part
    }
    return filePath; // If no separator, return the entire path
}

// Function to send data packet, returns whether the send was successful
bool send_packet(int sockfd, DataBlock &block, int data_size, struct sockaddr_in &servaddr, socklen_t len)
{
    // Send the data block, including block number and actual data
    return sendto(sockfd, &block, sizeof(block.block_num) + data_size + sizeof(block.crc32), 0, (struct sockaddr *)&servaddr, len) > 0;
}

// Precompute CRC32 lookup table
uint32_t crc32_table[256];

// Initialize the CRC32 lookup table, using the CRC32 polynomial
void init_crc32_table() {
    uint32_t polynomial = 0xEDB88320; // CRC32 polynomial
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 8; j > 0; j--) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial; // If the least significant bit is 1, perform XOR operation
            } else {
                crc >>= 1; // Otherwise, shift right
            }
        }
        crc32_table[i] = crc;
    }
}

// Calculate the CRC32 checksum of a single data block
uint32_t calculate_crc32(const std::vector<char>& data, uint32_t crc = 0xFFFFFFFF) {
    for (size_t i = 0; i < data.size(); i++) {
        uint8_t byte = data[i]; // Get each byte of the data
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF]; // Lookup and update the CRC value
    }
    return crc;
}

// Calculate the CRC32 checksum of the entire file
uint32_t calculate_file_crc32(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary); // Open file in binary mode
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << file_path << std::endl;
        return 0;
    }

    init_crc32_table(); // Initialize CRC32 lookup table

    uint32_t crc = 0xFFFFFFFF; // Initial value
    std::vector<char> buffer(4096); // Read 4KB of data at a time

    while (file.good()) {
        file.read(buffer.data(), buffer.size()); // Read data into buffer
        std::streamsize bytes_read = file.gcount(); // Get the number of bytes read
        if (bytes_read > 0) {
            buffer.resize(bytes_read); // Resize buffer to match the amount of data read
            crc = calculate_crc32(buffer, crc); // Update the CRC32 value
        }
    }

    return crc ^ 0xFFFFFFFF; // Return the final CRC32 checksum
}

// Calculate the CRC32 checksum of a data block
uint32_t calculate_block_crc32(const char* data, size_t length) {
    init_crc32_table(); // Initialize the CRC32 lookup table

    uint32_t crc = 0xFFFFFFFF; // Initial value
    std::vector<char> buffer(data, data + length); // Convert the data block into a vector

    crc = calculate_crc32(buffer, crc); // Calculate the CRC32 checksum

    return crc ^ 0xFFFFFFFF; // Return the final CRC32 checksum
}


// Main function, client program entry point
int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        // If the number of arguments is incorrect, output usage instructions
        std::cerr << "Usage: " << argv[0] << " <IP address> <port> <file_path>" << std::endl;
        return -1;
    }

    // The following 3 lines of code record and persist the start time of the program
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();


    const char *server_ip = argv[1];    // Server IP address
    int port = atoi(argv[2]);           // Server port number
    const char *file_path = argv[3];    // Path of the file to send

    int sockfd;                         // Socket file descriptor
    struct sockaddr_in servaddr;        // Server address structure
    DataBlock block;                    // Data block structure
    Handshake handshake;                // Handshake information structure
    Ack ack;                            // ACK structure
    int client_mtu = 1400;              // Client's MTU, assumed to be 1400 bytes
    int agreed_mtu;                     // Agreed MTU after negotiation with the server

    // Get file size
    long filesize = getFileSize(file_path);
    if (filesize < 0)
    {
        std::cerr << "Failed to get file size." << std::endl;
        return -1;
    }

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

    int resent_count = 0; // Define a variable to count retransmitted packets (including all causes of retransmission)

    // Set server address information
    memset(&servaddr, 0, sizeof(servaddr));    // Clear structure
    servaddr.sin_family = AF_INET;             // Use IPv4
    servaddr.sin_port = htons(port);           // Set port number (network byte order)
    servaddr.sin_addr.s_addr = inet_addr(server_ip); // Set server IP address

    // Prepare handshake information
    std::string file_name = getFileName(file_path); // Get the filename
    strncpy(handshake.filename, file_name.c_str(), sizeof(handshake.filename) - 1); // Copy filename to handshake structure
    handshake.filename[sizeof(handshake.filename) - 1] = '\0'; // Ensure string ends with '\0'
    handshake.filesize = filesize;           // Set file size
    handshake.mtu = client_mtu;              // Set client's MTU
    handshake.crc32_hash = calculate_file_crc32(file_path); // Calculate the CRC32 checksum of the file

    // Send handshake information to the server
    sendto(sockfd, &handshake, sizeof(handshake), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    std::cout << "Handshake sent with file info and MTU." << std::endl;

    // Receive the agreed MTU from the server
    recvfrom(sockfd, &agreed_mtu, sizeof(agreed_mtu), 0, nullptr, nullptr);
    std::cout << "Agreed MTU received: " << agreed_mtu << std::endl;

    // Use the agreed MTU for data transmission
    std::ifstream file(file_path, std::ios::binary); // Open file in binary mode
    if (!file.is_open())
    {
        std::cerr << "Failed to open file." << std::endl;
        close(sockfd);
        return -1;
    }

    int base = 0;         // Starting sequence number of the sliding window
    int next_seq_num = 0; // Sequence number of the next block to send
    socklen_t len = sizeof(servaddr);

    // Create epoll instance
    int epfd = epoll_create1(0);  // Parameter 0 indicates default behavior
    if (epfd == -1) {
        std::cerr << "Failed to create epoll instance: " << strerror(errno) << std::endl;
        close(sockfd);
        return -1;
    }

    // Register socket to epoll instance, listening for EPOLLIN event (readable event)
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;  // Readable event
    ev.data.fd = sockfd;  // Socket file descriptor

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
        std::cerr << "Failed to add socket to epoll: " << strerror(errno) << std::endl;
        close(epfd);
        close(sockfd);
        return -1;
    }

    while (base * agreed_mtu < filesize)
    {
        // Send data packets within the window
        std::cout << "****************************" << std::endl;
        std::cout << "Before send, next_seq_num is " << next_seq_num << std::endl;
        std::cout << "base+WINDOW_SIZE:" << base+WINDOW_SIZE << std::endl;
        std::cout << "****************************" << std::endl;
        while (next_seq_num < base + WINDOW_SIZE && next_seq_num * agreed_mtu < filesize)
        {

            std::cout << "before seekg, move pointer to : " << next_seq_num*agreed_mtu << std::endl;
            file.seekg(next_seq_num * agreed_mtu); // Move file pointer to the corresponding position

            // Calculate the size of data to read, the last block might be smaller than MTU
            int bytes_to_read = std::min(agreed_mtu, static_cast<int>(filesize - next_seq_num * agreed_mtu));
            std::cout << "before file read, bytes_to_read:" << bytes_to_read << std::endl;
            file.read(block.data, bytes_to_read); // Read data into data block
            std::streamsize bytes_read = file.gcount(); // Get the actual number of bytes read

            block.block_num = next_seq_num; // Set data block number

            std::vector<char> data_vector(block.data, block.data + bytes_read);
            block.crc32 = calculate_block_crc32(block.data, bytes_read);
            
            
            send_packet(sockfd, block, bytes_read, servaddr, len); // Send data packet
            std::cout << "Sent block " << block.block_num << " with " << bytes_read << " bytes." << std::endl;
            std::cout << "Data packet sent! Block number: " << block.block_num << " CRC32 at sender side: " << block.crc32 << std::endl;
            next_seq_num++; // Increment sequence number
        }

        std::cout << "********* Out Window send while loop*********" << std::endl;


        // Get current time
        auto now = std::chrono::system_clock::now();
        // Convert time point to time_t type
        std::time_t currentTime = std::chrono::system_clock::to_time_t(now);

        // Print current time (formatted output) for debugging
        std::cout << "Current time: " << std::ctime(&currentTime);

        int nfds = epoll_wait(epfd, events, MAX_EVENTS, TIMEOUT);
        std::cout << "epoll_wait return result:" << nfds << std::endl;

        if (nfds == -1) {
            std::cerr << "Error in epoll_wait: " << strerror(errno) << std::endl;
            break;
        }

        if (nfds == 0) {
            // Timeout handling: retransmit all packets in the window
            std::cout << "Timeout: retransmitting all packets in window." << std::endl;
            // Timeout handling logic goes here, retransmission can be performed
            next_seq_num = base;
        } else {

            // Process all triggered events
            for (int i = 0; i < nfds; ++i) {
                if (events[i].events & EPOLLIN) {
                    // Socket is readable, handle read event
                    // ACK received
                    recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&servaddr, &len);
                    std::cout << "******************ACK received****************" << std::endl;
                    std::cout << "ack.highest_received_block:"  << ack.highest_received_block << std::endl; 
                    std::cout << "missing_blocks bool array: ";
                    for(int i=0;i<sizeof(ack.missing_blocks);i++){
                        std::cout<<ack.missing_blocks[i] << ",";
                    }

                    // Update window base
                    if (ack.highest_received_block >= base) {
                        base = ack.highest_received_block + 1;
                    }

                    // Retransmit packets marked as missing in the ACK
                    for (int i = 0; i < WINDOW_SIZE; i++)
                    {
                        int seq_num_to_resend = base + i; // Calculate the sequence number to retransmit
                        if (ack.missing_blocks[i] && seq_num_to_resend < next_seq_num) // Ensure the sequence number has been sent
                        {
                            if (seq_num_to_resend * agreed_mtu < filesize)
                            {
                                file.seekg(seq_num_to_resend * agreed_mtu); // Move file pointer to the corresponding position
                                int bytes_to_read = std::min(agreed_mtu, static_cast<int>(filesize - seq_num_to_resend * agreed_mtu));
                                file.read(block.data, bytes_to_read);
                                std::streamsize bytes_read = file.gcount();
                                block.block_num = seq_num_to_resend;

                                // Calculate the CRC32 checksum of the data block
                                std::vector<char> data_vector(block.data, block.data + bytes_read);
                                block.crc32 = calculate_crc32(data_vector);

                                send_packet(sockfd, block, bytes_read, servaddr, len); // Retransmit data packet
                                resent_count++;
                                std::cout << "Resent block " << block.block_num << " with " << bytes_read << " bytes and CRC32: " << block.crc32 << " due to missing." << std::endl;
                            }
                        }
                    }
                }
            }

        }
    }

    // Send end signal to notify the server that the file transmission is complete
    memset(block.data, 0, sizeof(block.data)); // Clear data buffer
    strcpy(block.data, "END");                 // Set end signal
    block.block_num = next_seq_num;            // Assign a sequence number to the end signal
    sendto(sockfd, &block, sizeof(block.block_num) + strlen("END"), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));


    std::cout << std::endl;

    // The following 5 lines of code record and persist the end time of the program, and calculate the transfer rate
    now = std::chrono::system_clock::now();
    duration = now.time_since_epoch();
    auto end_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    std::string transfering_duration_millis_str = std::to_string(end_millis - millis);
    double bandwidth = (filesize * 8.0) / ((end_millis - millis) / 1000.0) / 1e6;

    std::cout << "File transfer complete! File size: " << filesize / 1000 / 1000 <<  "MiB! Time taken: " << transfering_duration_millis_str << " milliseconds! Average speed: " << bandwidth << " Mib/s! Total retransmitted packets: " << resent_count << std::endl;
    std::cout << "File transfer completed. Sent END signal." << std::endl;

    file.close(); // Close the file
    close(sockfd); // Close the socket
    return 0;
}
