import serial
import time
import os
import math
from PIL import Image

SERIAL_PORT = "/dev/serial0"
BAUD = 115200
CHUNK_SIZE = 200   # lebih efisien, masih aman LoRa
ACK_TIMEOUT = 3.0

ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.5)

def make_thumbnail(src, out="/tmp/thumb.jpg", max_side=256):
    img = Image.open(src)
    img.thumbnail((max_side, max_side))
    img.save(out, "JPEG", quality=70)
    return out

def wait_ack(fileid, idx):
    t0 = time.time()
    while time.time() - t0 < ACK_TIMEOUT:
        if ser.in_waiting:
            line = ser.readline().decode(errors="ignore").strip()
            if line == f"ACK|{fileid}|{idx}":
                return True
    return False

def send_thumbnail(src):
    thumb = make_thumbnail(src)
    with open(thumb, "rb") as f:
        data = f.read()

    fileid = int(time.time())
    total = math.ceil(len(data) / CHUNK_SIZE)

    print(f"[INFO] size={len(data)} total={total}")

    for idx in range(total):
        chunk = data[idx*CHUNK_SIZE:(idx+1)*CHUNK_SIZE]

        header = f"SEND|{fileid}|{idx}|{total}|{len(chunk)}\n"
        ser.write(header.encode())
        ser.write(chunk)
        ser.flush()

        if not wait_ack(fileid, idx):
            print(f"[FAIL] chunk {idx}")
            return False

        print(f"[OK] chunk {idx}/{total-1}")
        time.sleep(0.1)

    print("[DONE] transfer complete")
    return True

if __name__ == "__main__":
    send_thumbnail("./sample.jpg")
