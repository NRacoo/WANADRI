#!/usr/bin/env python3
import serial
import time
import os
import json
import base64
import math
import random
import zlib
import sys
from PIL import Image

# --- CONFIG ---
PORT = "/dev/serial0"
BAUD = 115200
NODE_ID = "N1"
CHUNK_SIZE = 150        # Keep small for LoRa
DELAY = 0.5             # Airtime buffer (Seconds)

def compress_image(input_path, output_path="temp.jpg"):
    """Resize image to 320px width (Crucial for LoRa speed)"""
    try:
        img = Image.open(input_path)
        w_percent = (320 / float(img.size[0]))
        h_size = int((float(img.size[1]) * float(w_percent)))
        img = img.resize((320, h_size), Image.Resampling.LANCZOS)
        img.save(output_path, "JPEG", quality=50)
        return True
    except Exception as e:
        print(f"Error resizing: {e}")
        return False

def send_file(serial_conn, file_path):
    if not os.path.exists(file_path):
        print("File not found")
        return

    filename = os.path.basename(file_path)
    file_id = str(random.randint(1000, 9999))
    
    with open(file_path, "rb") as f:
        file_bytes = f.read()

    total_chunks = math.ceil(len(file_bytes) / CHUNK_SIZE)
    print(f"Sending {filename} (ID:{file_id}) - {total_chunks} Chunks")

    for idx in range(total_chunks):
        # 1. Slice Data
        chunk_data = file_bytes[idx*CHUNK_SIZE : (idx+1)*CHUNK_SIZE]

        # 2. Build Header: N1|filename|fileid|idx|total|crc|
        crc = zlib.crc32(chunk_data) & 0xFFFFFFFF
        header = f"{NODE_ID}|{filename}|{file_id}|{idx}|{total_chunks}|{crc}|"
        
        # 3. Combine & Base64 Encode
        payload_bytes = header.encode('utf-8') + chunk_data
        b64_payload = base64.b64encode(payload_bytes).decode('utf-8')

        # 4. JSON Wrap
        msg = {
            "type": "chunk",
            "fileid": file_id, # Redundant but helps receiver quick-check
            "data": b64_payload
        }

        # 5. Send
        json_str = json.dumps(msg) + "\n"
        serial_conn.write(json_str.encode('utf-8'))
        
        print(f"TX Chunk {idx+1}/{total_chunks}")
        time.sleep(DELAY) # Wait for LoRa to transmit

    print("Done.")

# --- MAIN ---
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 sender.py <image_file>")
        sys.exit(1)

    ser = serial.Serial(PORT, BAUD, timeout=1)
    
    # 1. Compress first
    if compress_image(sys.argv[1], "thumb_ready.jpg"):
        # 2. Send the compressed file
        send_file(ser, "thumb_ready.jpg")