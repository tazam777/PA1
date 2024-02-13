#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define WINDOW_SIZE 4
#define BUFFER_SIZE 1024
#define TIMEOUT 2 // Timeout in seconds, for demonstration purposes

void die(const char *s) {
    perror(s);
    exit(1);
}

void send_ack(int sockfd, struct sockaddr_in *cli_addr, int ack_num) {
    if (sendto(sockfd, &ack_num, sizeof(ack_num), 0, (struct sockaddr *)cli_addr, sizeof(*cli_addr)) == -1) {
        die("sendto() ack");
    }
}

void receive_file(int sockfd) {
    struct sockaddr_in si_other;
    socklen_t slen = sizeof(si_other);
    char buf[BUFFER_SIZE];
    int expected_seq = 0, recv_len;
    FILE* file_out = fopen("received.txt", "wb");
    if (file_out == NULL) {
        die("Failed to open file for writing");
    }

    while (1) {
        memset(buf, 0, BUFFER_SIZE);
        if ((recv_len = recvfrom(sockfd, buf, BUFFER_SIZE, 0, (struct sockaddr *)&si_other, &slen)) == -1) {
            die("recvfrom()");
        }

        int seq_num = *(int*)buf;
        if (seq_num == expected_seq) {
            if (fwrite(buf + sizeof(int), 1, recv_len - sizeof(int), file_out) < 1) {
                die("fwrite()");
            }
            expected_seq++;
        }

        // Send ACK
        if (sendto(sockfd, &seq_num, sizeof(int), 0, (struct sockaddr*)&si_other, slen) == -1) {
            die("sendto()");
        }

        if (recv_len < BUFFER_SIZE - sizeof(int)) {
            printf("File transfer complete.\n");
            break;
        }
    }

    fclose(file_out);
}





void handle_get_command(int sockfd, struct sockaddr_in *cli_addr, char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        // File doesn't exist, send a message back to the client
        char *msg = "File not present";
        sendto(sockfd, msg, strlen(msg) + 1, 0, (struct sockaddr *)cli_addr, sizeof(*cli_addr));
        die("fopen");
        return;
    }

    // File exists, start sending it using Go-Back-N ARQ
     char packet[BUFFER_SIZE], window[WINDOW_SIZE][BUFFER_SIZE];
    int seq_num = 0, base = 0, window_packets[WINDOW_SIZE] = {0};
    socklen_t slen = sizeof(*cli_addr);
    struct timeval tv = {TIMEOUT, 0};
    
   while (!feof(file) || base != seq_num) {
        while (seq_num < base + WINDOW_SIZE && !feof(file)) {
            int packet_size = fread(packet + sizeof(int), 1, BUFFER_SIZE - sizeof(int), file);
            *(int*)packet = seq_num;
            memcpy(window[seq_num % WINDOW_SIZE], packet, packet_size + sizeof(int));
            window_packets[seq_num % WINDOW_SIZE] = packet_size + sizeof(int);

            if (sendto(sockfd, packet, packet_size + sizeof(int), 0, (struct sockaddr*)cli_addr, slen) == -1) {
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
                if (sendto(sockfd, window[idx], window_packets[idx], 0, (struct sockaddr*)cli_addr, slen) == -1) {
                    die("sendto() in resending");
                }
            }
        } else {
            printf("Received ACK: %d\n", ack);
            base = ack + 1;
        }
    }

    fclose(file);
}



int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    char buf[BUFFER_SIZE], command[10], filename[BUFFER_SIZE - 10];

    // Create a UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == -1) {
        die("socket");
    }

    memset(&si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind socket to port
    if (bind(sockfd, (struct sockaddr*)&si_me, sizeof(si_me)) == -1) {
        die("bind");
    }

    printf("Server listening on port %d\n", port);

    // Listen for incoming commands
    while (1) {
        memset(buf, 0, BUFFER_SIZE); // Clear buffer
        if (recvfrom(sockfd, buf, BUFFER_SIZE, 0, (struct sockaddr *)&si_other, &slen) == -1) {
            die("recvfrom()");
        }

        // Assuming the command and possibly a filename come in the same packet
        sscanf(buf, "%s %s", command, filename); // This is a simplistic parsing approach
        printf("Received command: '%s', Filename: '%s'\n", command, filename);

        if (strcmp(command, "put") == 0) {
            printf("Received 'put' command. Starting file reception...\n");
            receive_file(sockfd);
        } else if (strcmp(command, "get") == 0) {
            printf("Received 'get' command. Sending file '%s'...\n", filename);
            handle_get_command(sockfd, &si_other, filename);
        } else {
            printf("Unknown or unsupported command received: %s\n", command);
        }
    }

    close(sockfd);
    return 0;
}
