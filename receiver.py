#!/usr/bin/env python3
import serial
import json
import base64
import os
import time

# --- CONFIG ---
PORT = "/dev/serial0"
BAUD = 115200
OUT_DIR = "received_images"

# Ensure output directory exists
os.makedirs(OUT_DIR, exist_ok=True)

class FileBuffer:
    def __init__(self, filename, total_chunks):
        self.filename = filename
        self.total = total_chunks
        self.chunks = {}
        self.last_update = time.time()

    def add_chunk(self, idx, data):
        self.chunks[idx] = data
        self.last_update = time.time()
        print(f"[PROGRESS] {self.filename}: {len(self.chunks)}/{self.total} chunks")

    def is_complete(self):
        return len(self.chunks) == self.total

    def save(self):
        path = os.path.join(OUT_DIR, self.filename)
        # Sort by index to reassemble in order
        with open(path, "wb") as f:
            for i in range(self.total):
                if i in self.chunks:
                    f.write(self.chunks[i])
        return path

buffers = {} # Stores active file downloads

def process_line(line):
    try:
        # 1. Parse JSON Envelope
        obj = json.loads(line)
        if obj.get("type") != "chunk": return

        # 2. Extract Base64 Payload
        b64_data = obj['data']
        
        # 3. Decode Base64 -> Raw Bytes (Header + Image)
        raw_packet = base64.b64decode(b64_data)

        # 4. Find Header Separator (6th pipe '|')
        # Header Format: NodeID|Filename|FileID|Idx|Total|CRC|...Data...
        pipe_count = 0
        header_end = -1
        for i, byte_val in enumerate(raw_packet):
            if byte_val == 124: # ASCII for '|'
                pipe_count += 1
                if pipe_count == 6:
                    header_end = i + 1
                    break
        
        if header_end == -1:
            print("Error: Malformed header")
            return

        # 5. Parse Header
        header_str = raw_packet[:header_end].decode('utf-8')
        image_data = raw_packet[header_end:]
        
        parts = header_str.split('|')
        file_id  = parts[2]
        idx      = int(parts[3])
        total    = int(parts[4])
        filename = parts[1]

        # 6. Store Chunk
        if file_id not in buffers:
            buffers[file_id] = FileBuffer(filename, total)
        
        buf = buffers[file_id]
        buf.add_chunk(idx, image_data)

        # 7. Check Completion
        if buf.is_complete():
            saved_path = buf.save()
            print(f"[SUCCESS] Saved image to: {saved_path}")
            del buffers[file_id]

    except Exception as e:
        print(f"Error parsing line: {e}")

# --- MAIN LOOP ---
print(f"Listening on {PORT}...")
ser = serial.Serial(PORT, BAUD, timeout=1)

while True:
    try:
        line = ser.readline()
        if line:
            # Decode carefully, ignoring garbage bytes
            decoded_line = line.decode('utf-8', errors='ignore').strip()
            if decoded_line.startswith('{'):
                process_line(decoded_line)
    except KeyboardInterrupt:
        break