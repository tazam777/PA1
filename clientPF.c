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

int main(int argc, char *argv[]) {
    printf("Supported commands: put, get, ls, delete\n");
    printf("Write the input in this format: <server_ip> <port> <command> <filename>\n");

    // Check for the correct number of arguments
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <command> <filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Check if the command is "put"
    if (strcmp(argv[3], "put") == 0) {
        // Command-specific logic for "put"
        const char *server_ip = argv[1];
        int port = atoi(argv[2]);
        const char *file_name = argv[4];

        // Socket creation, command sending, and file sending logic
        // ...
    } 
    else if (strcmp(argv[3], "get") == 0) {
        // Logic for "get" command
    }
    else if (strcmp(argv[3], "ls") == 0) {
        // Logic for "ls" command
    }
    else if (strcmp(argv[3], "delete") == 0) {
        // Logic for "delete" command
    }
    else {
        // If the command is not recognized
        fprintf(stderr, "Unsupported command: %s\n", argv[3]);
        exit(EXIT_FAILURE);
    }

    return 0;
}
