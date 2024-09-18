# CFTP
EE542 Lab4 customize FTP program

# How to Use

If you do not have `make` and `g++`, you need to install it.
```bash
sudo apt install make g++
```

```bash
# to compile the cpp file.
make 
# to clean the executable file.
make clean
```

The src will output 2 executable files.

CFTPclient

CFTPserver

You have to put them to two different node and alter the defined ip address and port number(Temporarily).

# CFTPserver Command Line Guidance

```bash 
./CFTPserver -h <local_host> -p <udp_port>
```

- `-h <local_host>`: Specify the local host address for the server.
- `-p <udp_port>`: Specify the UDP port number for the server.


# CFTPclient Command Line Guidance

```bash
./CFTPclient -h <target_host> -p <target_port> -f <file_path> -t <target_file_path> -w <window_size>
```

Notice that the local_host and udp_port was hardcoded  as "127.0.0.1" and 44045 at CFTPclient.cpp.

- `-h <target_host>`: Specify the target host address for the client.
- `-p <target_port>`: Specify the target port number for the client.
- `-f <file_path>`: Specify the file path for the client.
- `-t <target_file_path>`: Specify the target file path for the client.
- `-w <window_size>`: Specify the window size for the client.



