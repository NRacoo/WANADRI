#!/usr/bin/env python3
# pi_sender.py - FINAL ROBUST VERSION
# Requires: pip install pillow pyserial

import serial
import time
import base64
import math
import zlib
import os
import re
from PIL import Image

# ====================== CONFIG ======================
SERIAL_PORT = "/dev/serial0"     # UART port
BAUD = 115200

NODE_ID = "N1"

CHUNK_SIZE = 50

ACK_TIMEOUT = 8.0                # total wait for ACK
MAX_RETRIES = 5                  # Pi-level retries
INTER_CHUNK_DELAY = 0.2

TX_OK_WAIT = 1.5                 # optional small wait to observe TX_OK

ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.1)

# Robust regex
ACK_RE = re.compile(r"ACK\| *(\d+) *\| *(\d+)")
TXOK_RE = re.compile(r"TX_OK\| *(\d+) *\| *(\d+)")
TXFAIL_RE = re.compile(r"TX_FAIL\| *(\d+) *\| *(\d+)")

# ====================== HELPERS ======================

def drain_serial(short_secs=0.03):
    t0 = time.time()
    while time.time() - t0 < short_secs:
        ser.read(4096)

def make_thumbnail(input_path, thumb_path="/tmp/thumb_send.jpg", max_side=256):
    img = Image.open(input_path)
    img.thumbnail((max_side, max_side))
    img.save(thumb_path, "JPEG", quality=70)
    return thumb_path

# ====================== CORE CHUNK SENDER ======================

def send_chunk_wait_ack(filename, fileid, idx, total, raw_chunk):
    """
    Send one chunk → Wait for TX_OK → Wait for ACK
    Uses robust regex + repr debugging so hidden chars don't break ACK.
    """
    crc = zlib.crc32(raw_chunk) & 0xFFFFFFFF
    payload_b64 = base64.b64encode(raw_chunk).decode("ascii")
    header = f"{NODE_ID}|{filename}|{fileid}|{idx}|{total}|{crc}|"
    line_out = header + payload_b64

    for attempt in range(1, MAX_RETRIES + 1):
        # Clear buffer of noise
        drain_serial(0.03)

        # Send
        ser.write((line_out + "\n").encode('ascii'))
        ser.flush()
        print(f"[SEND] chunk {idx} attempt {attempt}/{MAX_RETRIES}")

        # Main wait loop
        t0 = time.time()
        buffer = ""
        seen_txok = False

        while time.time() - t0 < ACK_TIMEOUT:
            blk = ser.read(512)
            if blk:
                try:
                    text = blk.decode("utf-8", errors="ignore")
                except:
                    text = blk.decode("latin-1", errors="ignore")
                buffer += text

                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    line = line.strip()

                    if not line:
                        continue

                    # Show real content (repr)
                    print("[RCV]", line)
                    print("      repr:", repr(line))

                    # ---- TX_OK detection ----
                    m_ok = TXOK_RE.search(line)
                    if m_ok:
                        fid = int(m_ok.group(1))
                        sidx = int(m_ok.group(2))
                        if fid == fileid and sidx == idx:
                            seen_txok = True
                            print(f"[INFO] TX_OK matched {fid}/{sidx}")

                    # ---- ACK detection ----
                    m_ack = ACK_RE.search(line)
                    if m_ack:
                        fid = int(m_ack.group(1))
                        sidx = int(m_ack.group(2))
                        if fid == fileid and sidx == idx:
                            print(f"[OK] ACK matched {fid}/{sidx}")
                            time.sleep(0.01)
                            return True
                        else:
                            print(f"[INFO] ACK for other chunk {fid}/{sidx}")

                    # ---- TX_FAIL forwarded by Node ----
                    m_fail = TXFAIL_RE.search(line)
                    if m_fail:
                        fid = int(m_fail.group(1))
                        sidx = int(m_fail.group(2))
                        if fid == fileid and sidx == idx:
                            print(f"[WARN] Node local TX_FAIL for {fid}/{sidx}, Pi will retry...")
                            break  # break inner line loop → Pi retry

            time.sleep(0.02)

        print(f"[WARN] Timeout on attempt {attempt}, seen_txok={seen_txok}")
        backoff = attempt * 0.5
        print(f"[RETRY] sleeping {backoff:.2f}s before retry")
        time.sleep(backoff)

    print(f"[FAIL] chunk {idx} failed after {MAX_RETRIES} attempts")
    return False

# ====================== FULL FILE SENDER ======================

def send_thumbnail_file(input_path):
    print("[*] Creating thumbnail...")
    thumb = make_thumbnail(input_path)
    filename = os.path.basename(thumb)
    fileid = int(time.time())  # unique-ish ID

    data = open(thumb, "rb").read()

    total = math.ceil(len(data) / CHUNK_SIZE)
    print(f"[*] Sending {filename}: size={len(data)} bytes, chunks={total}")

    for idx in range(total):
        raw_chunk = data[idx*CHUNK_SIZE : (idx+1)*CHUNK_SIZE]
        ok = send_chunk_wait_ack(filename, fileid, idx, total, raw_chunk)
        if not ok:
            print("[FATAL] Transfer aborted at chunk", idx)
            return False

        print(f"[DONE] chunk {idx}/{total-1}")
        time.sleep(INTER_CHUNK_DELAY)

    print("[SUCCESS] File transfer complete.")
    return True

# ====================== MAIN ======================

if __name__ == "__main__":
    # Ganti ke path foto saat mau kirim
    SRC = "sample.jpg"
    send_thumbnail_file(SRC)
