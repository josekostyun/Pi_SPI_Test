#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <iostream>
#include <cstring>

#define SPI_CHANNEL 0        // /dev/spidev0.0
#define SPI_SPEED   1000000  // 1 MHz (stable for MSP430)
#define CS_PIN      25       // GPIO25 (Pin 22) used as Chip Select

int main() {
    // Initialize GPIO using BCM numbering
    wiringPiSetupGpio();

    // Configure CS pin
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH); // CS idle high

    // Initialize SPI in Mode 0
    if (wiringPiSPISetupMode(SPI_CHANNEL, SPI_SPEED, 0) < 0) {
        std::cerr << "SPI setup failed. Make sure SPI is enabled with: sudo raspi-config" << std::endl;
        return -1;
    }

    // Test packet to send to MSP
    const char msg[] = "$PH,035500123,00802,013,#";

    std::cout << "Sending SPI message: " << msg << std::endl;

    // Start SPI frame
    digitalWrite(CS_PIN, LOW);

    // Send message one byte at a time (prevents gaps)
    for (size_t i = 0; i < strlen(msg); i++) {
        wiringPiSPIDataRW(SPI_CHANNEL, (unsigned char*)&msg[i], 1);
    }

    // End SPI frame
    digitalWrite(CS_PIN, HIGH);

    std::cout << "Done." << std::endl;
    return 0;
}
