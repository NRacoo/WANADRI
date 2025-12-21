import serial

SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE = 115200
OUTPUT_FILE = "received_image.jpg"

def receive_image():
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print("[INFO] Listening...")

    buffer = b""
    expected_size = None
    received = 0

    # pastikan file kosong
    open(OUTPUT_FILE, "wb").close()

    while True:
        data = ser.read(ser.in_waiting or 1)
        if not data:
            continue

        buffer += data

        # ===== PARSE HEADER =====
        if expected_size is None:
            if b"START:" in buffer and b"\n" in buffer:
                header, buffer = buffer.split(b"\n", 1)
                header_str = header.decode(errors="ignore")

                if header_str.startswith("START:"):
                    expected_size = int(header_str.replace("START:", ""))
                    received = 0
                    print(f"[INFO] Expecting {expected_size} bytes")
                else:
                    buffer = b""
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
