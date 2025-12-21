import serial
import zlib
import os
import time

PORT = '/dev/serial0'
BAUD = 115200
OUTPUT_DIR = "./thumbnails"

ser = serial.Serial(PORT, BAUD, timeout=1)

buffers = {}   # (node, fileid) → array chunk
meta = {}      # (node, fileid) → { total, filename, received }

def ensure_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)

ensure_dir(OUTPUT_DIR)

def extract_data(json_str):
    try:
        if "\"data\":\"" not in json_str:
            return None
        part = json_str.split("\"data\":\"", 1)[1]
        return part.split("\"", 1)[0]
    except:
        return None


def handle_chunk(line):
    """
    Format baru:
      NODE | FILENAME | FILEID | IDX | TOTAL | CRC | <binary>
    """
    try:
        parts = line.split("|", 6)
        node     = parts[0]
        filename = parts[1]
        fileid   = parts[2]
        idx      = int(parts[3])
        total    = int(parts[4])
        crc_in   = int(parts[5])
        payload  = parts[6]

        chunk_bytes = payload.encode('latin1')
        crc_calc = zlib.crc32(chunk_bytes) & 0xFFFFFFFF

        if crc_calc != crc_in:
            print(f"[{node}] CRC error idx={idx}")
            return

        key = (node, fileid)

        if key not in buffers:
            buffers[key] = [None] * total
            meta[key] = {
                "received": 0,
                "total": total,
                "filename": filename,
            }

        if buffers[key][idx] is None:
            buffers[key][idx] = chunk_bytes
            meta[key]["received"] += 1

        print(f"[{node}] {filename} chunk {idx}/{total} ({meta[key]['received']}/{total})")

        if meta[key]["received"] == total:
            print(f"[{node}] COMPLETE {filename}, assembling...")

            save_path = os.path.join(OUTPUT_DIR, filename)

            with open(save_path, "wb") as f:
                for c in buffers[key]:
                    f.write(c)

            print(f"[{node}] Saved → {save_path}")

            del buffers[key]
            del meta[key]

    except Exception as e:
        print("Chunk parse error:", e)


print("Gateway reassembler running...")

while True:
    try:
        line = ser.readline().decode(errors='ignore').strip()
        if not line:
            continue

        if line.startswith("{") and "\"data\"" in line:
            chunk_line = extract_data(line)
            if chunk_line:
                handle_chunk(chunk_line)
        else:
            print("RAW:", line)

    except KeyboardInterrupt:
        break
    except Exception as e:
        print("Loop error:", e)
        time.sleep(0.1)
