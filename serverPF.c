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

void send_ack(int sockfd, struct sockaddr_in *cli_addr, int ack_num) {
    if (sendto(sockfd, &ack_num, sizeof(ack_num), 0, (struct sockaddr *)cli_addr, sizeof(*cli_addr)) == -1) {
        die("sendto() ack");
    }
}

void receive_file(int sockfd) {
    struct sockaddr_in si_other;
    socklen_t slen = sizeof(si_other);
    char buffer[BUFFER_SIZE];
    int expected_seq = 0, recv_len;
    FILE* file_out = fopen("received.txt", "wb");
    if (file_out == NULL) {
        die("Failed to open file for writing");
    }

    const int window_size = WINDOW_SIZE;
    struct timeval tv = {TIMEOUT, 0}; // Timeout structure for select
    fd_set readfds; // Set of file descriptors for select

    while (1) {
        FD_ZERO(&readfds); // Clear the set
        FD_SET(sockfd, &readfds); // Add our descriptor to the set

        int activity = select(sockfd + 1, &readfds, NULL, NULL, &tv); // Wait for activity
        if ((activity < 0) && (errno != EINTR)) {
            die("select error");
        }

        if (activity == 0) {
            // Timeout occurred, logic to handle timeout
            printf("Timeout, but no need to resend in Go-Back-N receiver.\n");
            continue; // Go-Back-N receiver does not resend packets, so we just continue listening
        }

        if (FD_ISSET(sockfd, &readfds)) {
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&from, &fromlen);
            if (recv_len < 0) {
                die("recvfrom()");
            }

            int seq_num = *(int *)buffer; // Extract sequence number
            if (seq_num == expected_seq) {
                fwrite(buffer + sizeof(int), 1, recv_len - sizeof(int), file_out);
                expected_seq++; // Increment for the next expected packet
            }

            // Always send ACK for the most recently received in-order packet
            sendto(sockfd, &expected_seq, sizeof(expected_seq), 0, (struct sockaddr *)&from, fromlen);

            if (recv_len < BUFFER_SIZE) {
                printf("File transfer complete.\n");
                break; // End of file reached
            }
        }
    }

    fclose(file_out);
}
void handle_get_command(int sockfd, struct sockaddr_in *cli_addr, char *filename) {
    // Open the file for reading
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        // If the file doesn't exist, inform the client
        char *msg = "File not present";
        sendto(sockfd, msg, strlen(msg) + 1, 0, (struct sockaddr *)cli_addr, sizeof(*cli_addr));
        return;
    }

    // Initialize the window for Go-Back-N
    char packet[BUFFER_SIZE], window[WINDOW_SIZE][BUFFER_SIZE];
    int seq_num = 0, base = 0, window_packets[WINDOW_SIZE] = {0};
    socklen_t slen = sizeof(*cli_addr);
    struct timeval tv = {TIMEOUT, 0}; // Timeout for ACKs
    
    fd_set readfds; // Set of socket file descriptors for select()
    int activity;

    // Loop to read file contents and send them to the client
    while (!feof(file) || base != seq_num) {
        FD_ZERO(&readfds);  // Clear the socket set
        FD_SET(sockfd, &readfds);  // Add sockfd to the socket set
        
        // Fill the window and send packets
        while (seq_num < base + WINDOW_SIZE && !feof(file)) {
            int packet_size = fread(packet + sizeof(int), 1, BUFFER_SIZE - sizeof(int), file);
            *(int *)packet = seq_num;
            memcpy(window[seq_num % WINDOW_SIZE], packet, packet_size + sizeof(int));
            window_packets[seq_num % WINDOW_SIZE] = packet_size + sizeof(int);

            if (sendto(sockfd, packet, packet_size + sizeof(int), 0, (struct sockaddr *)cli_addr, slen) == -1) {
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
                if (sendto(sockfd, window[idx], window_packets[idx], 0, (struct sockaddr *)cli_addr, slen) == -1) {
                    die("sendto() in resending");
                }
            }
        } else {
            // If ACK is received, update the base
            int ack;
            if (recvfrom(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)cli_addr, &slen) == -1) {
                die("recvfrom() ack");
            }
            printf("Received ACK for packet %d\n", ack);
            base = ack + 1;
        }
    }

    // Once the file is completely sent, close the file
    fclose(file);
    printf("File '%s' has been sent successfully.\n", filename);
}

void handle_delete_command(int sockfd, struct sockaddr_in *cli_addr, char *filename) {
    // Attempt to delete the file
    if (remove(filename) == 0) {
        // If file deletion is successful
        char *msg = "File deleted successfully";
        sendto(sockfd, msg, strlen(msg) + 1, 0, (struct sockaddr *)cli_addr, sizeof(*cli_addr));
    } else {
        // If the file does not exist or cannot be deleted
        char *msg = "File not present or cannot be deleted";
        sendto(sockfd, msg, strlen(msg) + 1, 0, (struct sockaddr *)cli_addr, sizeof(*cli_addr));
    }
}

void handle_ls_command(int sockfd, struct sockaddr_in *cli_addr) {
    DIR *d;
    struct dirent *dir;
    d = opendir(".");  // Open the current working directory
    if (d) {
        char filesList[4096] = "";  // Assuming the list fits in 4096 bytes
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {  // If the entry is a regular file
                strcat(filesList, dir->d_name);
                strcat(filesList, "\n");  // Separate file names with a newline
            }
        }
        closedir(d);

        // Send the list of files to the client
        sendto(sockfd, filesList, strlen(filesList) + 1, 0, (struct sockaddr *)cli_addr, sizeof(*cli_addr));
    } else {
        char *msg = "Failed to open directory.";
        sendto(sockfd, msg, strlen(msg) + 1, 0, (struct sockaddr *)cli_addr, sizeof(*cli_addr));
    }
}

void handle_exit_command(int sockfd, struct sockaddr_in *cli_addr) {
    char *goodbyeMsg = "Gentlemen, it has been a privilege playing with you tonight.Until next time See yaa!!!";
    sendto(sockfd, goodbyeMsg, strlen(goodbyeMsg) + 1, 0, (struct sockaddr *)cli_addr, sizeof(*cli_addr));
    serverRunning = 0; // Signal the server to stop
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
//The Socket creation and Binding code 
    int port = atoi(argv[1]);
    struct sockaddr_in si_me, si_other;
    socklen_t slen = sizeof(si_other);
    char buf[BUFFER_SIZE], command[10], filename[BUFFER_SIZE - 10];

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == -1) {
        die("socket");
    }

    memset(&si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&si_me, sizeof(si_me)) == -1) {
        die("bind");
    }

    printf("Server listening on port %d\n", port);

    while (serverRunning) {
        memset(buf, 0, BUFFER_SIZE); // Clear buffer
        if (recvfrom(sockfd, buf, BUFFER_SIZE, 0, (struct sockaddr *)&si_other, &slen) == -1) {
            die("recvfrom()");
        }
         

    // Print the raw buffer contents for debug purpose as inititially the commands were not reaching buffer
    printf("Debug - Received buffer: '%s'\n", buf);

        sscanf(buf, "%s %s", command, filename); // Extract command and filename
        printf("Received command: '%s', Filename: '%s'\n", command, filename);

       if (strncmp(buf, "put", 3) == 0) {
            printf("PUT command received. Starting file reception...\n");
            receive_file(sockfd); 
        } else if (strcmp(command, "get") == 0) {
            handle_get_command(sockfd, &si_other, filename);
        } else if (strcmp(command, "delete") == 0) {
            handle_delete_command(sockfd, &si_other, filename);
        } else if (strcmp(command, "ls") == 0) {
            handle_ls_command(sockfd, &si_other);
        } else if (strcmp(command, "exit") == 0) {
            handle_exit_command(sockfd, &si_other);
        } else {
            printf("Unknown or unsupported command received.\n");
        }
    }

    printf("Server shutting down...\n");
    close(sockfd);
    return 0;
}