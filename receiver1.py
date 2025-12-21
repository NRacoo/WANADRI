import serial
import re

SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE = 115200
OUTPUT_FILE = "received_image.jpg"

def receive_image():
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print("[INFO] Listening...")

    buffer = b""
    expected_size = None
    received = 0

    open(OUTPUT_FILE, "wb").close()

    while True:
        data = ser.read(ser.in_waiting or 1)
        if not data:
            continue

        buffer += data

        # ===== PARSE START HEADER =====
        if expected_size is None:
            if b"START:" in buffer:
                idx = buffer.index(b"START:") + len(b"START:")
                size_bytes = b""

                # ambil digit saja
                while idx < len(buffer) and buffer[idx:idx+1].isdigit():
                    size_bytes += buffer[idx:idx+1]
                    idx += 1

                if size_bytes:
                    expected_size = int(size_bytes)
                    print(f"[INFO] Expecting {expected_size} bytes")

                    # buang header, sisanya data
                    buffer = buffer[idx:]
                    received = 0
                else:
                    continue
            else:
                continue

        # ===== WRITE DATA =====
        if buffer:
            with open(OUTPUT_FILE, "ab") as f:
                f.write(buffer)
            received += len(buffer)
            buffer = b""

            print(f"[RX] {received}/{expected_size} bytes", end="\r")

        if received >= expected_size:
            print("\n[SUCCESS] Image received completely")
            break

if __name__ == "__main__":
    receive_image()
