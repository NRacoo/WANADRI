#!/usr/bin/env python3
"""
reassembler_full.py

Robust serial reassembler for LoRa Gateway JSON output.

Receives JSON lines from gateway on serial and reconstructs files from
base64-encoded chunk payloads.

Usage:
  python3 reassembler_full.py --port /dev/serial0 --baud 115200

"""

import serial
import argparse
import json
import base64
import os
import time
from collections import defaultdict

# ---------- CONFIG ----------
DEFAULT_PORT = "/dev/serial0"
DEFAULT_BAUD = 115200
OUT_DIR = "received"         # final assembled files
TMP_DIR = "tmp_chunks"       # temporary per-file chunk storage
VERBOSE = True
# ----------------------------

def log(*args, **kwargs):
    if VERBOSE:
        print(*args, **kwargs)

def ensure_dirs():
    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(TMP_DIR, exist_ok=True)

class FileReassembler:
    """
    Maintain state for one file identified by fileid.
    """
    def __init__(self, fileid, filename=None):
        self.fileid = fileid
        self.filename = filename or f"{fileid}"
        self.chunks = {}        # idx -> bytes
        self.received_indices = set()
        self.total = None       # known when gateway sends total
        self.first_received_at = time.time()
        self.last_activity = time.time()
        # directory for storing chunk files (optional persistence)
        self.dir = os.path.join(TMP_DIR, f"{fileid}")
        os.makedirs(self.dir, exist_ok=True)

def add_chunk(self, idx, data_bytes, total=None):
    """
    Add a chunk in an idempotent, radio-safe way.
    Duplicate idx is HARD-DROPPED (never overwritten).
    """
    self.last_activity = time.time()

    # --- HARD DROP duplicate idx ---
    if idx in self.chunks:
        log(f"[DROP] duplicate chunk idx={idx} fileid={self.fileid}")
        return False

    # --- OPTIONAL: protect JPEG SOI duplication ---
    if idx != 0:
        # sanity: non-zero chunk MUST NOT start with JPEG SOI
        if data_bytes.startswith(b'\xFF\xD8'):
            log(f"[DROP] invalid SOI in non-zero chunk idx={idx}")
            return False

    # --- store ONCE ---
    self.chunks[idx] = data_bytes
    self.received_indices.add(idx)

    if total is not None:
        self.total = int(total)

    # persist for debugging (safe)
    try:
        with open(os.path.join(self.dir, f"chunk_{idx:06d}.bin"), "wb") as f:
            f.write(data_bytes)
    except Exception as e:
        log(f"[WARN] could not write chunk file: {e}")

    return True

    def got_all(self):
        if self.total is None:
            return False
        return len(self.received_indices) >= int(self.total)

    def assemble(self):
        """Return assembled bytes in idx order. Raise if missing indices."""
        if self.total is None:
            raise RuntimeError("Total unknown; cannot assemble")
        parts = []
        for i in range(int(self.total)):
            if i not in self.chunks:
                raise RuntimeError(f"Missing chunk {i}")
            parts.append(self.chunks[i])
        return b"".join(parts)

    def write_assembled(self):
        """Write assembled file to OUT_DIR and return path."""
        outname = f"{self.fileid}_{self.filename}"
        # sanitize filename somewhat
        outname = outname.replace("/", "_").replace("\\", "_")
        outpath = os.path.join(OUT_DIR, outname)
        assembled = self.assemble()
        with open(outpath, "wb") as f:
            f.write(assembled)
        return outpath

# Global state: fileid -> FileReassembler
files = {}

def handle_chunk_json(obj):
    filename = obj.get("filename")
    fileid = obj.get("fileid")
    idx = obj.get("idx")
    total = obj.get("total")
    data_b64 = obj.get("data")

    if fileid is None or idx is None or data_b64 is None:
        print("[WARN] chunk JSON missing fields")
        return

    fileid = str(fileid)
    idx = int(idx)

    # Decode base64 → still contains HEADER + RAW JPEG BYTES
    try:
        packet = base64.b64decode(data_b64)
    except Exception as e:
        print("[WARN] base64 decode failed:", e)
        return

    # ===================== STRIP HEADER =====================
    # Header format:
    # N1|filename|fileid|idx|total|crc|
    pipe_count = 0
    header_end = -1
    for i, b in enumerate(packet):
        if b == ord('|'):
            pipe_count += 1
            if pipe_count == 6:
                header_end = i + 1
                break

    if header_end < 0:
        print("[WARN] Header delimiter not found, dropping chunk")
        return

    raw_payload = packet[header_end:]
    # ========================================================

    if fileid not in files:
        files[fileid] = FileReassembler(
            fileid=fileid,
            filename=filename or f"{fileid}.jpg"
        )
        print(f"[INFO] New file {filename}, id={fileid}")

    fr = files[fileid]
    fr.add_chunk(idx, raw_payload, total=total)

    print(f"[INFO] Got chunk {idx}/{fr.total} for {filename}")

    if fr.got_all():
        print(f"[DONE] All chunks received for {filename}")
        outpath = fr.write_assembled()
        print(f"[SAVED] {outpath}")
        del files[fileid]


def handle_chunk_json(obj):
    filename = obj.get("filename")
    fileid = obj.get("fileid")
    idx = obj.get("idx")
    total = obj.get("total")
    data_b64 = obj.get("data")

    if fileid is None or idx is None or data_b64 is None:
        print("[WARN] chunk JSON missing fields")
        return

    fileid = str(fileid)
    idx = int(idx)

    # Decode base64 → still contains HEADER + RAW JPEG BYTES
    try:
        packet = base64.b64decode(data_b64)
    except Exception as e:
        print("[WARN] base64 decode failed:", e)
        return

    # ===================== STRIP HEADER =====================
    # Header format:
    # N1|filename|fileid|idx|total|crc|
    pipe_count = 0
    header_end = -1
    for i, b in enumerate(packet):
        if b == ord('|'):
            pipe_count += 1
            if pipe_count == 6:
                header_end = i + 1
                break

    if header_end < 0:
        print("[WARN] Header delimiter not found, dropping chunk")
        return

    raw_payload = packet[header_end:]
    # ========================================================

    if fileid not in files:
        files[fileid] = FileReassembler(
            fileid=fileid,
            filename=filename or f"{fileid}.jpg"
        )
        print(f"[INFO] New file {filename}, id={fileid}")

    fr = files[fileid]
    fr.add_chunk(idx, raw_payload, total=total)

    print(f"[INFO] Got chunk {idx}/{fr.total} for {filename}")

    if fr.got_all():
        print(f"[DONE] All chunks received for {filename}")
        outpath = fr.write_assembled()
        print(f"[SAVED] {outpath}")
        del files[fileid]

def handle_ack_json(obj):
    fileid = obj.get("fileid")
    idx = obj.get("idx")
    log(f"[ACK JSON] fileid={fileid}, idx={idx}")

def process_serial_line(raw_bytes):
    # decode to string and strip
    try:
        line = raw_bytes.decode('utf-8', errors='ignore').strip()
    except Exception:
        return

    if not line:
        return

    # only attempt JSON parsing when line looks like JSON object
    if not line.startswith('{'):
        # ignore non-json lines silently (or log if you want)
        # print("[IGNORED]", line)
        return

    try:
        obj = json.loads(line)
    except Exception as e:
        log("[WARN] Invalid JSON:", raw_bytes)
        return

    t = obj.get("type")
    if t == "chunk":
        handle_chunk_json(obj)
    elif t == "ack":
        handle_ack_json(obj)
    else:
        log("[INFO] Unknown JSON type or log:", obj)

def main_loop(port, baud):
    ensure_dirs()
    log(f"[START] Opening serial port {port} @ {baud}")
    ser = serial.Serial(port, baud, timeout=1)
    buffer = b""
    try:
        while True:
            # read available data in chunks
            chunk = ser.read(1024)
            if chunk:
                buffer += chunk
                # split lines on newline
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    process_serial_line(line)
            else:
                # no data; small sleep to avoid busy loop
                time.sleep(0.05)
            # optional housekeeping: drop old incomplete files after long inactivity
            # (not implemented automatically)
    except KeyboardInterrupt:
        log("[STOP] interrupted by user")
    finally:
        ser.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Reassembler for LoRa gateway JSON over serial")
    parser.add_argument("--port", "-p", default=DEFAULT_PORT, help="Serial port (default /dev/serial0)")
    parser.add_argument("--baud", "-b", default=DEFAULT_BAUD, type=int, help="Baud rate")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    args = parser.parse_args()
    VERBOSE = args.verbose or True
    main_loop(args.port, args.baud)
