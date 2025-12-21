import serial
import json
import base64
import os
import zlib

SERIAL_PORT = "/dev/serial0"
BAUD = 115200
OUT_DIR = "received"
os.makedirs(OUT_DIR, exist_ok=True)

ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)

files = {}

class FileBuf:
    def __init__(self, total):
        self.total = total
        self.chunks = {}

    def done(self):
        return len(self.chunks) == self.total

while True:
    line = ser.readline().decode(errors="ignore").strip()
    if not line:
        continue

    try:
        obj = json.loads(line)
    except:
        continue

    if obj.get("type") != "chunk":
        continue

    fid = str(obj["fileid"])
    idx = obj["idx"]
    total = obj["total"]
    crc = obj["crc"]

    payload = base64.b64decode(obj["data"])
    if zlib.crc32(payload) & 0xFFFFFFFF != crc:
        print("[DROP] CRC mismatch", fid, idx)
        continue

    if fid not in files:
        files[fid] = FileBuf(total)

    fb = files[fid]
    if idx in fb.chunks:
        continue

    fb.chunks[idx] = payload
    print(f"[RX] {fid} {idx}/{total}")

    if fb.done():
        path = os.path.join(OUT_DIR, f"{fid}.jpg")
        with open(path, "wb") as f:
            for i in range(total):
                f.write(fb.chunks[i])
        print("[SAVED]", path)
        del files[fid]
