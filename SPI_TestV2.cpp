#include <iostream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <mutex>
#include <thread>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

static const char *SPI_DEVICE = "/dev/spidev0.0";
static const uint32_t SPI_SPEED = 1000000;   // 1 MHz
static const uint8_t SPI_BITS = 8;
static const uint8_t SPI_MODE = SPI_MODE_0;

int spi_fd = -1;
bool is_connected = false;
bool listener_running = true;

uint64_t g_timestamp_ms = 0;
bool g_time_synced = false;

std::mutex spi_mutex;

// ----------------------------------------
// SPI Init
// ----------------------------------------
bool spi_init() {
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        std::cerr << "[SPI] Failed to open device" << std::endl;
        return false;
    }

    ioctl(spi_fd, SPI_IOC_WR_MODE, &SPI_MODE);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &SPI_BITS);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED);

    is_connected = true;
    std::cout << "[SPI] Initialized: " << SPI_DEVICE << std::endl;
    return true;
}

// ----------------------------------------
// Send $PH pothole packet to MSP
// ----------------------------------------
bool send_pothole_data(uint64_t timestamp_ms, float area_sqin, float depth_in) {
    std::lock_guard<std::mutex> lock(spi_mutex);

    if (!is_connected || spi_fd < 0) {
        std::cerr << "[SPI] Not connected" << std::endl;
        return false;
    }

    uint32_t timestamp_field = timestamp_ms % 10000000000ULL;
    uint32_t area_field = static_cast<uint32_t>(std::round(area_sqin * 10.0f));
    uint8_t depth_field = static_cast<uint8_t>(std::round(depth_in * 10.0f));

    if (area_field > 99999) area_field = 99999;
    if (depth_field > 255) depth_field = 255;

    char message[26] = {0};
    snprintf(message, sizeof(message), "$PH,%010u,%05u,%03u#",
             timestamp_field, area_field, depth_field);

    struct spi_ioc_transfer transfer {};
    transfer.tx_buf = (unsigned long)message;
    transfer.len = 25;
    transfer.speed_hz = SPI_SPEED;
    transfer.bits_per_word = SPI_BITS;

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &transfer) < 0) {
        std::cerr << "[SPI] Transfer failed" << std::endl;
        return false;
    }

    std::cout << "[SPI] Sent: " << std::string(message, 25) << std::endl;
    return true;
}

// ----------------------------------------
// Send timestamp echo back to MSP
// ----------------------------------------
void spi_echo_timestamp(uint64_t new_time) {
    char msg[16];
    snprintf(msg, sizeof(msg), "@TS,%010lu#", (unsigned long)new_time);

    struct spi_ioc_transfer xfer {};
    xfer.tx_buf = (unsigned long)msg;
    xfer.len = strlen(msg);
    xfer.speed_hz = SPI_SPEED;
    xfer.bits_per_word = SPI_BITS;
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &xfer);

    std::cout << "[SPI] → Echoed Timestamp Back: " << msg << std::endl;
}

// ----------------------------------------
// SPI Listener Thread (MSP → Pi)
// ----------------------------------------
void spi_listener() {
    const int TIMESYNC_MSG_LEN = 15;
    char buffer[TIMESYNC_MSG_LEN];
    int buffer_idx = 0;

    std::cout << "[SPI LISTENER] Thread started" << std::endl;

    while (listener_running) {
        struct pollfd pfd;
        pfd.fd = spi_fd;
        pfd.events = POLLIN;

        if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
            char byte;
            struct spi_ioc_transfer transfer {};
            memset(&transfer, 0, sizeof(transfer));
            transfer.rx_buf = (unsigned long)&byte;
            transfer.len = 1;
            transfer.speed_hz = SPI_SPEED;
            transfer.bits_per_word = SPI_BITS;
            ioctl(spi_fd, SPI_IOC_MESSAGE(1), &transfer);

            // Start of timestamp packet
            if (byte == '@') {
                buffer_idx = 0;
                buffer[buffer_idx++] = byte;
            }
            // Continue filling
            else if (buffer_idx > 0 && buffer_idx < TIMESYNC_MSG_LEN) {
                buffer[buffer_idx++] = byte;

                // End of timestamp packet
                if (buffer_idx == TIMESYNC_MSG_LEN && byte == '#') {
                    uint64_t new_time = 0;
                    for (int i = 4; i < 14; i++) {
                        new_time = new_time * 10 + (buffer[i] - '0');
                    }

                    g_timestamp_ms = new_time;
                    g_time_synced = true;
                    std::cout << "[SPI] ✓ Time updated: " << new_time << " ms" << std::endl;

                    // Echo it back to MSP
                    spi_echo_timestamp(new_time);

                    buffer_idx = 0;
                }
            }
        }
    }

    std::cout << "[SPI LISTENER] Thread stopped" << std::endl;
}

// ----------------------------------------
// MAIN
// ----------------------------------------
int main() {
    if (!spi_init()) return 1;

    std::thread listener(spi_listener);

    std::cout << "[SYSTEM] Running. Press CTRL+C to exit.\n";

    // Example send (optional test)
    send_pothole_data(1234567890ULL, 12.3f, 1.7f);

    while (1)
        usleep(100000);
}
