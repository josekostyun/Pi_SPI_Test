import spidev
import time
import sys
import termios
import tty
import select

def key_pressed():
    dr, _, _ = select.select([sys.stdin], [], [], 0)
    return dr != []

# Setup SPI
spi = spidev.SpiDev()
spi.open(0, 0)  # bus 0, device 0 (CE0)
spi.max_speed_hz = 500000
spi.mode = 0b00

print("Press any key to stop...\n")

# Set terminal to raw mode to detect keypresses
fd = sys.stdin.fileno()
old_settings = termios.tcgetattr(fd)
tty.setcbreak(fd)

try:
    counter = 0
    while True:
        if key_pressed():
            print("Stopped by user.")
            break
        to_send = [counter & 0xFF]
        response = spi.xfer(to_send)
        print(f"Sent: {to_send[0]}, Received: {response[0]}")
        counter = (counter + 1) % 256
        time.sleep(0.5)
finally:
    termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    spi.close()
