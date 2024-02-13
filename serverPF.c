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


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    char buf[BUFFER_SIZE];

    // Create a UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == -1) {
        die("socket");
    }

    // Zero out the structure
    memset((char *)&si_me, 0, sizeof(si_me));

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
        if (recvfrom(sockfd, buf, BUFFER_SIZE, 0, (struct sockaddr *)&si_other, &slen) == -1) {
            die("recvfrom()");
        }
        printf("Received command: '%s'\n", buf);

        // Check if the command is "put"
        if (strncmp(buf, "put", 3) == 0) {
            printf("Received 'put' command. Starting file reception...\n");
            receive_file(sockfd); // Call the function to receive the file
            break; // Optional: break if you only expect to receive one file
        } else {
            printf("Unknown command received.\n");
        }
    }

    close(sockfd);
    return 0;
}