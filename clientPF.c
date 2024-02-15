#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h> 
#include <errno.h>
#include <sys/time.h>
int serverRunning = 1; 
#define WINDOW_SIZE 4
#define BUFFER_SIZE 1024
#define TIMEOUT 2 

void die(const char *s) {
    perror(s);
    exit(1);
}

void send_file(int sockfd, struct sockaddr_in *si_other,const char *filename)
 {  char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "put %s", filename);
    
    sendto(sockfd, command, strlen(command) + 1, 0, (struct sockaddr *)si_other, sizeof(*si_other));
     printf("Sending file to server: %s\n", filename); 
    FILE *file_in = fopen(filename, "rb");
    if (file_in == NULL) {
        die("fopen");
    }

    char packet[BUFFER_SIZE], window[WINDOW_SIZE][BUFFER_SIZE];
    int seq_num = 0, base = 0, window_packets[WINDOW_SIZE] = {0};
    socklen_t slen = sizeof(*si_other);
    struct timeval tv = {TIMEOUT, 0};

    fd_set readfds; // Set of socket file descriptors for select()
    int activity;

    // Loop to read file contents and send them to the client
    while (!feof(file_in) || base != seq_num) {
        FD_ZERO(&readfds);  // Clear the socket set
        FD_SET(sockfd, &readfds);  // Add sockfd to the socket set
        
        // Fill the window and send packets
        while (seq_num < base + WINDOW_SIZE && !feof(file_in)) {
            int packet_size = fread(packet + sizeof(int), 1, BUFFER_SIZE - sizeof(int), file_in);
            *(int *)packet = seq_num;
            memcpy(window[seq_num % WINDOW_SIZE], packet, packet_size + sizeof(int));
            window_packets[seq_num % WINDOW_SIZE] = packet_size + sizeof(int);

            if (sendto(sockfd, packet, packet_size + sizeof(int), 0, (struct sockaddr *)si_other, slen) == -1) {
                die("sendto()");
            }
            seq_num++;
        }

          // Set timeout for ACK reception using select
        tv.tv_sec = TIMEOUT;
        tv.tv_usec = 0;
        activity = select(sockfd + 1, &readfds, NULL, NULL, &tv);

        // If timeout or error occurs, go back to the start of the window and resend
        if (activity < 0 && errno != EINTR) {
            printf("Select error.\n");
        } else if (activity == 0) {
            printf("Timeout occurred. Resending window.\n");
            for (int i = base; i < seq_num; i++) {
                int idx = i % WINDOW_SIZE;
                if (sendto(sockfd, window[idx], window_packets[idx], 0, (struct sockaddr *)si_other, slen) == -1) {
                    die("sendto() in resending");
                }
            }
        } else {
            // If ACK is received, update the base
            int ack;
            if (recvfrom(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)si_other, &slen) == -1) {
                die("recvfrom() ack");
            }
            printf("Received ACK for packet %d\n", ack);
            base = ack + 1;
        }
    }

    // Once the file is completely sent, close the file
    fclose(file_in);
    printf("File '%s' has been sent successfully.\n", filename);
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

    // Set up for Go-Back-N
    const int window_size = WINDOW_SIZE;
    int window_base = 0;
    struct timeval tv = {TIMEOUT, 0}; // Timeout structure for select
    fd_set readfds; // Set of file descriptors for select

    // Receiving loop
    while (1) {
        FD_ZERO(&readfds); // Clear the set
        FD_SET(sockfd, &readfds); // Add our descriptor to the set

        int activity = select(sockfd + 1, &readfds, NULL, NULL, &tv); // Wait for activity
        if ((activity < 0) && (errno != EINTR)) {
            die("select error");
        }

        if (activity == 0) {
            // Timeout occurred, resend all packets in the window
            printf("Timeout, resending from base: %d\n", window_base);
            // Logic to resend packets in the window
            continue; // Skip this loop iteration and wait for next packet
        }

        if (FD_ISSET(sockfd, &readfds)) {
            // Did this do that we have something to read from the socket
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            int recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&from, &fromlen);
            if (recv_len < 0) {
                die("recvfrom()");
            }

            int seq_num = *(int *)buffer; // Extract sequence number
            if (seq_num < window_base || seq_num >= window_base + window_size) {
                // Packet is outside the window, ignore it
                continue;
            }

            if (seq_num == expected_seq) {
                // This is the packet we're expecting, write it to file
                fwrite(buffer + sizeof(int), 1, recv_len - sizeof(int), file_out);
                expected_seq++; // Expect the next packet
                window_base = expected_seq; // Move the window
            }

            // Always send ACK for the received packet
            sendto(sockfd, &seq_num, sizeof(seq_num), 0, (struct sockaddr *)&from, fromlen);

            if (recv_len < BUFFER_SIZE) {
                printf("File transfer complete.\n");
                break; // We've reached the end of the file
            }
        }
    }

    fclose(file_out);
}

void request_delete(int sockfd, struct sockaddr_in *serv_addr, const char *filename) {
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "delete %s", filename);
    sendto(sockfd, command, strlen(command), 0, (struct sockaddr *)serv_addr, sizeof(*serv_addr));

    char buffer[BUFFER_SIZE];
    recvfrom(sockfd, buffer, BUFFER_SIZE, 0, NULL, NULL);
    printf("%s\n", buffer); // Print the server's response to the command
}


void request_ls(int sockfd, struct sockaddr_in *serv_addr) {
    char command[BUFFER_SIZE] = "ls";
    sendto(sockfd, command, strlen(command), 0, (struct sockaddr *)serv_addr, sizeof(*serv_addr));

    // Buffer to receive the list of files
    char filesList[4096];
    recvfrom(sockfd, filesList, sizeof(filesList), 0, NULL, NULL);
    printf("Files in the server's current directory:\n%s", filesList);
}


void send_exit_command(int sockfd, struct sockaddr_in *serv_addr) {
    char exitCommand[BUFFER_SIZE] = "exit";
    // Send the "exit" command to the server
    sendto(sockfd, exitCommand, strlen(exitCommand), 0, (struct sockaddr *)serv_addr, sizeof(*serv_addr));

    // Wait for the server's goodbye message
    char serverReply[BUFFER_SIZE];
    memset(serverReply, 0, BUFFER_SIZE); // Clear the buffer
    recvfrom(sockfd, serverReply, BUFFER_SIZE, 0, NULL, NULL);
    printf("Server says: %s\n", serverReply);
}

int main(int argc, char *argv[]) {
 //Usage /<filename.c> <server_ip> <command> <filename> 
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <command> <filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *command = argv[3];
    const char *file_name = argv[4];
//Socket creation and Binding code
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == -1) {
        die("socket creation failed");
    }

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
    } else if (strcmp(command, "delete") == 0) {
        request_delete(sockfd, &si_other, file_name);
    }
     else if (strcmp(command, "ls") == 0) {
    request_ls(sockfd, &si_other);
    }
    else if (strcmp(command, "exit") == 0) {
        send_exit_command(sockfd, &si_other);
        close(sockfd); // It's safe to close the socket here if exiting
        exit(0); // Exit after sending the exit command and receiving a response
    }
    else {
        fprintf(stderr, "Unsupported command: %s\n", command);
    }

    close(sockfd);
    return 0;
}