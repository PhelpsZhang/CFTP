#include "CFTPserver.h"

// Define the server's Maximum Transmission Unit (MTU) and the size of each data block
#define SERVER_MTU 1400
#define DATA_SIZE 1400
#define WINDOW_SIZE 100  // Sliding window size

// Define the handshake structure, which contains the filename, file size, client MTU, and CRC32 checksum
struct Handshake {
    char filename[256];   // Filename, up to 256 characters
    long filesize;        // File size (bytes)
    int mtu;              // Client's MTU
    uint32_t crc32_hash;  // CRC32 checksum for verifying the file
};

// Define the data block structure, which contains the block number and actual data
struct DataBlock {
    int block_num;        // Block number
    uint32_t crc32;       // CRC32 checksum for the block (Note: this line was originally below the data array, but since the data array length is not fixed, if the packet size is not MTU, it might be truncated before being read, so it was moved above the data)
    char data[DATA_SIZE]; // Content of the data block, maximum size is 1400 bytes
};

// Define the ACK structure, which contains the highest confirmed block number and a bitmap for missing packets
struct Ack {
    int highest_received_block;    // Highest confirmed block number
    bool missing_blocks[WINDOW_SIZE];  // Bitmap of missing packets, indicating which packets were not received
};

// CRC32 lookup table for speeding up CRC32 checksum calculation
uint32_t crc32_table[256];

// Initialize the CRC32 lookup table, generating a checksum for each byte using the CRC32 polynomial
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

// Calculate the CRC32 checksum of a single data block, using the lookup table to speed up calculation
uint32_t calculate_crc32(const std::vector<char>& data, uint32_t crc = 0xFFFFFFFF) {
    for (size_t i = 0; i < data.size(); i++) {
        uint8_t byte = data[i]; // Get each byte of the data
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF]; // Lookup and update CRC value
    }
    return crc;
}

// Calculate the CRC32 checksum of the entire file
uint32_t calculate_file_crc32(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary); // Open the file in binary mode for reading
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << file_path << std::endl;
        return 0;
    }

    init_crc32_table(); // Initialize the CRC32 lookup table

    uint32_t crc = 0xFFFFFFFF; // Initial value
    std::vector<char> buffer(4096); // Read 4KB of data at a time

    while (file.good()) {
        file.read(buffer.data(), buffer.size()); // Read data from the file
        std::streamsize bytes_read = file.gcount(); // Get the actual number of bytes read
        if (bytes_read > 0) {
            buffer.resize(bytes_read); // Resize buffer to match the amount of data read
            crc = calculate_crc32(buffer, crc); // Update the CRC32 checksum
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

// Main function for the server program
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return -1;
    }

    int port = atoi(argv[1]); // Get the port number from the command-line arguments

    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    int sockfd; // Server socket file descriptor
    struct sockaddr_in servaddr, cliaddr; // Server and client address structures
    Handshake handshake; // Handshake information
    DataBlock block; // Data block
    Ack ack; // ACK confirmation

    // Create a UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

    // Initialize the server address
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET; // Use IPv4 protocol
    servaddr.sin_addr.s_addr = INADDR_ANY; // Accept connections from any address
    servaddr.sin_port = htons(port); // Set the server's port number

    // Bind the socket to the specified port
    if (bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        std::cerr << "Bind failed." << std::endl;
        return -1;
    }

    socklen_t len = sizeof(cliaddr); // Client address length

    // Receive the client's handshake information, which includes the filename, file size, etc.
    recvfrom(sockfd, &handshake, sizeof(handshake), 0, (struct sockaddr*)&cliaddr, &len);
    std::cout << "Handshake received: " << std::endl;
    std::cout << "Filename: " << handshake.filename << std::endl;
    std::cout << "Filesize: " << handshake.filesize << std::endl;
    std::cout << "Client MTU: " << handshake.mtu << std::endl;

    // Determine the minimum MTU to use
    int agreed_mtu = std::min(SERVER_MTU, handshake.mtu);
    std::cout << "Agreed MTU: " << agreed_mtu << std::endl;

    // Calculate the total number of data blocks needed to transmit the file
    int total_blocks = (handshake.filesize + agreed_mtu - 1) / agreed_mtu;
    std::cout << "Total blocks to receive: " << total_blocks << std::endl;

    // Send the negotiated MTU back to the client
    sendto(sockfd, &agreed_mtu, sizeof(agreed_mtu), 0, (struct sockaddr*)&cliaddr, len);

    // Create a temporary file to store the received data
    std::string millis_str = std::to_string(millis);
    std::string temp_file_name = "received_file" + millis_str;
    std::ofstream file(temp_file_name, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open output file." << std::endl;
        close(sockfd);
        return -1;
    }

    int expected_seq_num = 0; // The next expected data block number
    std::map<int, std::string> buffer; // Buffer for out-of-order packets
    int received_count = 0; // Counter to control the frequency of ACKs

    while (true) {
        memset(block.data, 0, sizeof(block.data)); // Clear the data buffer
        block.crc32 = 888; // Note: the line above clears the data buffer, but the crc32 field is not cleared, so we assign an arbitrary value to help debug. The value 888 is used for quick identification and has no special meaning.
        int n = recvfrom(sockfd, &block, sizeof(block), 0, (struct sockaddr*)&cliaddr, &len);
        // Increment the count whenever a packet is received to trigger the current ACK strategy
        received_count++;

        // Calculate the CRC32 checksum of the received data block
        uint32_t received_crc32 = calculate_block_crc32(block.data, n - sizeof(block.block_num) - sizeof(block.crc32));
        std::cout << "Data packet received! Block number: " << block.block_num << " Expected CRC32: " << block.crc32 << " Actual CRC32: " << received_crc32 << std::endl;

        // Validate the CRC32 value of the data block
        if (received_crc32 != block.crc32) {
            std::cerr << "CRC32 mismatch for block " << block.block_num << ". Expected: " << block.crc32 << ", Received: " << received_crc32 << std::endl;
            continue; // Ignore the data block
        }

        std::cout << "expected_seq_num: " << expected_seq_num << std::endl;
        std::cout << "block.block_num: " << block.block_num << std::endl;
        // If the expected packet is received, write it to the file in sequence
        if (block.block_num == expected_seq_num) {
            std::cout << "Received expected block " << block.block_num << std::endl;
            file.write(block.data, n - sizeof(block.block_num) - sizeof(block.crc32)); // Write the data
            expected_seq_num++;

            // Check the buffer for any subsequent packets
            while (buffer.count(expected_seq_num)) {
                std::cout << "Writing buffered block " << expected_seq_num << std::endl;
                file.write(buffer[expected_seq_num].c_str(), agreed_mtu); // Write data from the buffer
                buffer.erase(expected_seq_num);
                expected_seq_num++;
                received_count++;
            }
        } else if (block.block_num > expected_seq_num) {
            // If a packet with a higher sequence number than expected is received, buffer it
            std::cout << "Buffered out-of-order block " << block.block_num << std::endl;
            buffer[block.block_num] = std::string(block.data, n - sizeof(block.block_num) - sizeof(block.crc32));
        }
        
        // Check if the last data block has been received
        if (block.block_num == total_blocks - 1) {
            std::cout << "Received last block (" << block.block_num << "). Sending ACK." << std::endl;

            // Construct the final ACK and send it
            ack.highest_received_block = block.block_num;
            memset(ack.missing_blocks, 0, sizeof(ack.missing_blocks)); // No missing blocks
            sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&cliaddr, len);

            std::cout << "File transfer completed." << std::endl;
            break;
        }

        // Send an ACK after receiving WINDOW_SIZE packets
        std::cout << "received_count: " << received_count << std::endl;
        if (received_count >= WINDOW_SIZE) {
            ack.highest_received_block = expected_seq_num - 1; // Highest received sequence number

            // Set the missing block information in the ACK
            memset(ack.missing_blocks, 0, sizeof(ack.missing_blocks));
            for (int i = 0; i < WINDOW_SIZE; i++) {
                if (buffer.count(expected_seq_num + i) == 0) {
                    ack.missing_blocks[i] = true;  // Mark the block as missing
                }
            }

            // Send the ACK to the client
            sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&cliaddr, len);
            std::cout << "Sent ACK for block " << ack.highest_received_block << std::endl;

            // Reset the counter
            received_count = 0;
        }
    }

    file.close();
    close(sockfd);

    // After the file transfer is complete, rename the temporary file to the correct filename and verify the CRC32 checksum
    if (std::rename(temp_file_name.c_str(), handshake.filename) == 0) {
        uint32_t crc32_hash = calculate_file_crc32(handshake.filename);
        std::cout << "File renamed successfully to " << handshake.filename << std::endl;
        std::cout << "Original file's CRC32: " << handshake.crc32_hash << std::endl;
        std::cout << "Copied file's CRC32: " << crc32_hash << std::endl;
        if (crc32_hash == handshake.crc32_hash) {
            std::cout << "Congratulations! File verification passed!" << std::endl;
        } else {
            std::cout << "Unfortunately, file verification failed! Continue debugging!" << std::endl;
        }
    } else {
        std::perror("Error renaming file");
    }

    return 0;
}
