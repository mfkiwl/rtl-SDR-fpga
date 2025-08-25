#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>

#define DEST_PORT 25344
#define PACKET_SIZE 1028
#define MESSAGE "Hello this is Qian from Zybo"

void build_packet(uint8_t *packet, uint32_t counter) {
    // Set the first 4 bytes as the frame counter (little endian)
    packet[0] = (counter >> 0) & 0xFF;
    packet[1] = (counter >> 8) & 0xFF;
    packet[2] = (counter >> 16) & 0xFF;
    packet[3] = (counter >> 24) & 0xFF;

    // Copy message
    const char *msg = MESSAGE;
    size_t msg_len = strlen(msg);
    memcpy(packet + 4, msg, msg_len);

    // Pad rest with ASCII '1' (0x31)
    memset(packet + 4 + msg_len, '1', PACKET_SIZE - 4 - msg_len);
}

int main(void) {
    printf("Content-type: text/html\r\n\r\n");
    printf("<pre>\n");

    // Get QUERY_STRING from CGI environment
    char *query = getenv("QUERY_STRING");
    if (!query) {
        printf("No QUERY_STRING found.\n");
        printf("</pre>\n");
        return 1;
    }

    // Extract ip and n values
    char dest_ip[64] = {0};
    int num_packets = 0;

    // Example QUERY_STRING: "ip=192.168.1.2&n=10"
    sscanf(query, "ip=%63[^&]&n=%d", dest_ip, &num_packets);

    if (strlen(dest_ip) == 0 || num_packets <= 0) {
        printf("Invalid parameters. QUERY_STRING: %s\n", query);
        printf("</pre>\n");
        return 1;
    }

    printf("Sending %d packets to %s:%d...\n", num_packets, dest_ip, DEST_PORT);

    // Create UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        printf("</pre>\n");
        return 1;
    }

    // Destination address setup
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DEST_PORT);
    if (inet_pton(AF_INET, dest_ip, &dest_addr.sin_addr) <= 0) {
        perror("invalid IP address");
        printf("</pre>\n");
        return 1;
    }

    // Allocate and send packets
    uint8_t packet[PACKET_SIZE];
    for (uint32_t i = 0; i < (uint32_t)num_packets; i++) {
        build_packet(packet, i);

        ssize_t sent = sendto(sockfd, packet, PACKET_SIZE, 0,
                              (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent != PACKET_SIZE) {
            perror("sendto failed");
        }
        usleep(1000); // Small delay
    }

    close(sockfd);
    printf("Finished sending packets.\n");
    printf("</pre>\n");
    return 0;
}
