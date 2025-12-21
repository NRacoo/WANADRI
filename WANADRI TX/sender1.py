import cv2
import serial
import time
import sys
import os

# --- Configuration ---
SERIAL_PORT = '/dev/serial0' 
BAUD_RATE = 115200
PACKET_SIZE = 220
IMAGE_PATH = 'file.jpg'

def process_static_image(filepath):
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found.")
        return None

    # 1. Load Image (Loads in BGR Color by default)
    img = cv2.imread(filepath)
    if img is None: return None

    print(f"Original Dimensions: {img.shape}")

    # 2. Resize to 160x120 (Paper limit)
    # We keep the small resolution to ensure the LoRa transmission 
    # doesn't take 10+ minutes.
    resized = cv2.resize(img, (160, 120))
    
    # 3. Compress to JPEG (Color)
    # Quality 30 is a balance between color detail and file size.
    # You can lower this to 20 or 15 if transmission is too slow.
    encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), 30] 
    result, encimg = cv2.imencode('.jpg', resized, encode_param)
    
    return encimg.tobytes()

def send_via_lora(ser, data):
    total_len = len(data)
    print(f"\n[TX] Sending Color Image: {total_len} bytes")
    
    # Send Header
    ser.write(b'IMG_START') 
    time.sleep(0.5)
    
    # Fragmentation Loop
    for i in range(0, total_len, PACKET_SIZE):
        chunk = data[i:i + PACKET_SIZE]
        ser.write(chunk)
        
        # Progress Bar
        percent = int((i / total_len) * 100)
        sys.stdout.write(f"\rProgress: [{('=' * (percent // 5)).ljust(20)}] {percent}%")
        sys.stdout.flush()
        
        # DELAY: 0.25s is safe. If you see packet loss on receiver, increase to 0.3s
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
            print(f"Processed Size: {len(jpg_data)} bytes (160x120 Color)")
            while True:
                cmd = input("Press ENTER to send, 'q' to quit: ")
                if cmd == 'q': break
                send_via_lora(ser, jpg_data)
                
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()