import cv2
import serial
import time
import sys
import os

# --- Configuration ---
# /dev/serial0 is the default alias for GPIO UART on Pi 5
SERIAL_PORT = '/dev/ttyAMA0' 
BAUD_RATE = 115200
# Paper specifies splitting data into packets of 220 bytes [cite: 120]
PACKET_SIZE = 220  
IMAGE_PATH = 'file.jpg'

def process_static_image(filepath):
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found.")
        return None

    # Load original image
    img = cv2.imread(filepath)
    if img is None: return None

    # 1. Convert to Grayscale 
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    
    # 2. Resize to 160x120 
    resized = cv2.resize(gray, (160, 120))
    
    # 3. Compress to JPEG
    encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), 30] 
    result, encimg = cv2.imencode('.jpg', resized, encode_param)
    
    return encimg.tobytes()

def send_via_lora(ser, data):
    total_len = len(data)
    print(f"\n[TX] Sending Image: {total_len} bytes")
    
    # Send Header
    ser.write(b'IMG_START') 
    time.sleep(0.5)
    
    # Fragmentation Loop
    for i in range(0, total_len, PACKET_SIZE):
        chunk = data[i:i + PACKET_SIZE]
        ser.write(chunk)
        
        # Progress Feedback
        sys.stdout.write(f"\rPacket {i//PACKET_SIZE + 1} sent ({len(chunk)} bytes)")
        sys.stdout.flush()
        
        # CRITICAL DELAY: LoRa is slow. 
        # We must pause to let the ESP32 transmit the air packet.
        time.sleep(0.25) 
        
    # Send Footer
    time.sleep(0.5)
    ser.write(b'IMG_END')
    print("\n[TX] Transmission Complete.")

def main():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"Connected to LilyGO on {SERIAL_PORT}")
        
        jpg_data = process_static_image(IMAGE_PATH)
        
        if jpg_data:
            print(f"Processed Size: {len(jpg_data)} bytes (160x120 Grayscale)")
            while True:
                cmd = input("Press ENTER to send, 'q' to quit: ")
                if cmd == 'q': break
                send_via_lora(ser, jpg_data)
                
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()