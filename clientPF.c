// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#define WINDOW_SIZE 4

#define BUFFER_SIZE 1024
#define TIMEOUT 2 // Timeout in seconds

void die(const char *s) {
    perror(s);
    exit(1);
}

void send_file(int sockfd, struct sockaddr_in *si_other,const char *file_name)
 {

    FILE *file_in = fopen(file_name, "rb");
    if (file_in == NULL) {
        die("fopen");
    }

    char packet[BUFFER_SIZE], window[WINDOW_SIZE][BUFFER_SIZE];
    int seq_num = 0, base = 0, window_packets[WINDOW_SIZE] = {0};
    socklen_t slen = sizeof(*si_other);
    struct timeval tv = {TIMEOUT, 0};
    
    while (!feof(file_in) || base != seq_num) {
        while (seq_num < base + WINDOW_SIZE && !feof(file_in)) {
            int packet_size = fread(packet + sizeof(int), 1, BUFFER_SIZE - sizeof(int), file_in);
            *(int*)packet = seq_num;
            memcpy(window[seq_num % WINDOW_SIZE], packet, packet_size + sizeof(int));
            window_packets[seq_num % WINDOW_SIZE] = packet_size + sizeof(int);

            if (sendto(sockfd, packet, packet_size + sizeof(int), 0, (struct sockaddr*)si_other, slen) == -1) {
                die("sendto()");
            }
            if (base == seq_num) {
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            }
            seq_num++;
        }

        int ack;
        if (recv(sockfd, &ack, sizeof(int), 0) == -1) {
            printf("Timeout, resending window\n");
            for (int i = base; i < seq_num; i++) {
                int idx = i % WINDOW_SIZE;
                if (sendto(sockfd, window[idx], window_packets[idx], 0, (struct sockaddr*)si_other, slen) == -1) {
                    die("sendto() in resending");
                }
            }
        } else {
            printf("Received ACK: %d\n", ack);
            base = ack + 1;
        }
    }

    fclose(file_in);
}


void request_file(int sockfd, struct sockaddr_in *serv_addr, const char *filename) {
    // Send "get" command and filename to server
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "get %s", filename);
    sendto(sockfd, command, strlen(command) + 1, 0, (struct sockaddr *)serv_addr, sizeof(*serv_addr));

    // Prepare to receive file or message from server
    char buffer[BUFFER_SIZE];
    int expected_seq = 0;
    FILE *file_out = fopen("download.txt", "wb");
    if (file_out == NULL) {
        die("Failed to open file for writing");
    }

    while (1) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&from, &fromlen);
        if (recv_len < 0) {
            die("recvfrom()");
        }
        buffer[recv_len] = '\0'; // Null-terminate received string

        // Check for "File not present" message
        if (strcmp(buffer + sizeof(int), "File not present") == 0) {
            printf("Server: %s\n", buffer + sizeof(int));
            fclose(file_out);
            remove("download.txt"); // Delete the file as it's not valid
            break;
        }

        // Process received file packet
        int seq_num = *(int *)buffer;
        if (seq_num == expected_seq) {
            fwrite(buffer + sizeof(int), 1, recv_len - sizeof(int), file_out);
            expected_seq++;
        }

        // Send ACK
        sendto(sockfd, &seq_num, sizeof(seq_num), 0, (struct sockaddr *)&from, fromlen);
        
        if (recv_len < BUFFER_SIZE) {
            printf("File transfer complete.\n");
            break;
        }
    }

    fclose(file_out);
}





int main(int argc, char *argv[]) {
    printf("Supported commands: put, get, ls, delete\n");
    printf("Write the input in this format: <server_ip> <port> <command> <filename>\n");

    // Check for the correct number of arguments
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <command> <filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *command = argv[3];
    const char *file_name = argv[4];

    // Create a socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == -1) {
        die("socket creation failed");
    }

    // Set up the server address structure
    struct sockaddr_in si_other;
    memset(&si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(port);
    if (inet_aton(server_ip, &si_other.sin_addr) == 0) {
        die("inet_aton() failed");
    }

    if (strcmp(command, "put") == 0) {
        send_file(sockfd, &si_other, file_name);
    } else if (strcmp(command, "get") == 0) {
        request_file(sockfd, &si_other, file_name);
    } else {
        fprintf(stderr, "Unsupported command: %s\n", command);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    close(sockfd);
    return 0;
}
