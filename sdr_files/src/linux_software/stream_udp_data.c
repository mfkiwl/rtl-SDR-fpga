// Build: gcc -O2 -Wall -Wextra -o radio_udp radio_udp.c
// Run:   ./radio_udp <dest_ip> <dest_port>
// Example: ./radio_udp 192.168.1.5 5000

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// ======= Hardware mapping =======
#define FIFO_BASE_ADDRESS          0x43C10000
#define MAP_SIZE                   4096

// Offsets are in 32-bit words (not bytes)
#define RX_FIFO_OCCUPANCY_OFFSET   7   // 0x1C / 4
#define RX_FIFO_DATA_OFFSET        8   // 0x20 / 4

// ======= Framing =======
#define FRAME_SAMPLES              256            // 256 complex I/Q samples per frame
#define BYTES_PER_SAMPLE           4              // 16-bit I + 16-bit Q
#define FRAME_DATA_BYTES           (FRAME_SAMPLES * BYTES_PER_SAMPLE)  // 1024
#define FRAME_BYTES                (4 + FRAME_DATA_BYTES)              // 1028

// ======= Helpers to write little-endian regardless of host =======
static inline void put_le32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v      & 0xFF);
    dst[1] = (uint8_t)((v>>8) & 0xFF);
    dst[2] = (uint8_t)((v>>16)& 0xFF);
    dst[3] = (uint8_t)((v>>24)& 0xFF);
}
static inline void put_le16(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v      & 0xFF);
    dst[1] = (uint8_t)((v>>8) & 0xFF);
}

// ======= Map physical address =======
static volatile uint32_t* map_phys_u32(off_t phys_addr, size_t bytes) {
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("open /dev/mem");
        return NULL;
    }
    void *map_base = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, phys_addr);
    close(mem_fd);
    if (map_base == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
    return (volatile uint32_t*)map_base;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <dest_ip> <dest_port>\n", argv[0]);
        return 1;
    }
    const char *dest_ip = argv[1];
    int dest_port = atoi(argv[2]);
    if (dest_port <= 0 || dest_port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }

    // Map FIFO
    volatile uint32_t *fifo = map_phys_u32(FIFO_BASE_ADDRESS, MAP_SIZE);
    if (!fifo) return 1;

    // UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)dest_port);
    if (inet_pton(AF_INET, dest_ip, &dst.sin_addr) != 1) {
        perror("inet_pton");
        return 1;
    }

    // Frame buffer
    uint8_t frame[FRAME_BYTES];
    uint32_t frame_counter = 0;

    printf("Streaming UDP frames to %s:%d … (Ctrl-C to stop)\n", dest_ip, dest_port);

    while (true) {
        // bytes 0-3: little-endian 32-bit counter
        put_le32(frame, frame_counter);

        // bytes 4..: 256 complex samples (each FIFO word: [31:16]=Q, [15:0]=I usually)
        size_t write_off = 4; // start after counter
        for (int i = 0; i < FRAME_SAMPLES; ++i) {
            // Busy-wait until at least one word available
            while (fifo[RX_FIFO_OCCUPANCY_OFFSET] == 0) {
                // spin (no usleep as requested)
            }
            uint32_t w = fifo[RX_FIFO_DATA_OFFSET];

            // Extract I and Q as signed 16-bit (bit layout assumption: I=low 16, Q=high 16)
            // If your radio swaps them, just flip the put_le16 order below.
            uint16_t I = (uint16_t)(w & 0xFFFF);
            uint16_t Q = (uint16_t)((w >> 16) & 0xFFFF);

            // Pack as little-endian 16-bit I, then 16-bit Q
            put_le16(&frame[write_off + 0], I);
            put_le16(&frame[write_off + 2], Q);
            write_off += 4;
        }

        // Send the 1028-byte frame
        ssize_t sent = sendto(sock, frame, FRAME_BYTES, 0,
                              (struct sockaddr*)&dst, sizeof(dst));
        if (sent != FRAME_BYTES) {
            perror("sendto");
            // You can choose to break here, but continuing allows transient errors
        }

        frame_counter++; // wraps naturally at 2^32
    }

    // Not reached in normal run
    munmap((void*)fifo, MAP_SIZE);
    close(sock);
    return 0;
}
