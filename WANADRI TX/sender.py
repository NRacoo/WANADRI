import serial
import time
import os
import sys

# CONFIGURATION
SERIAL_PORT = '/dev/ttyUSB0' # Check with ls /dev/tty*
BAUD_RATE = 115200
IMAGE_PATH = 'image.jpg'
CHUNK_SIZE = 200 # Bytes per packet

def send_image(image_path):
    if not os.path.exists(image_path):
        print("Image not found")
        return

    # Initialize Serial
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    time.sleep(2) # Wait for ESP32 to reset

    # Read image in binary
    with open(image_path, "rb") as f:
        img_data = f.read()

    total_size = len(img_data)
    total_packets = (total_size // CHUNK_SIZE) + 1
    print(f"Sending {total_size} bytes in {total_packets} packets...")

    # Send a specialized 'START' packet with file size
    start_msg = f"START:{total_size}".encode()
    ser.write(start_msg)
    
    # Wait for ESP32 to bridge it and return ACK
    while True:
        if ser.readline().strip() == b"ACK":
            break

    # Send chunks
    for i in range(total_packets):
        start = i * CHUNK_SIZE
        end = start + CHUNK_SIZE
        chunk = img_data[start:end]
        
        ser.write(chunk)
        print(f"Sent packet {i+1}/{total_packets}")

        # FLOW CONTROL: Wait for ESP32 to say it finished transmitting
        # If we don't wait, we will overflow the ESP32 serial buffer
        while True:
            line = ser.readline().strip()
            if line == b"ACK":
                break
        
        # Small delay to let airwaves clear
        time.sleep(0.1) 

    print("Transmission Complete")
    ser.close()

if __name__ == "__main__":
    send_image(IMAGE_PATH)