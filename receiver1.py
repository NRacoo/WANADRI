import serial
import time
import os

# --- Configuration ---
# On Raspberry Pi 5, the primary GPIO serial is /dev/serial0
SERIAL_PORT = '/dev/serial0' 
BAUD_RATE = 115200

def main():
    try:
        # Open the GPIO Serial Port
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        print(f"[System] Receiver active on {SERIAL_PORT} (GPIO UART)")
        print("[System] Waiting for incoming LoRa transmission...")
    except Exception as e:
        print(f"Error opening serial port: {e}")
        print("Tip: Ensure you enabled 'Hardware Serial' in raspi-config")
        return

    buffer = b""
    is_receiving = False
    start_time = 0
    
    # Save received files to a 'downloads' folder
    if not os.path.exists("downloads"):
        os.makedirs("downloads")

    while True:
        try:
            # Check if data is waiting in the UART buffer
            if ser.in_waiting:
                data = ser.read(ser.in_waiting)
                
                # --- State Machine: Start of Image ---
                if b'IMG_START' in data:
                    print("\n[RX] Header detected! Starting download...")
                    is_receiving = True
                    start_time = time.time()
                    buffer = b"" # Clear buffer for new image
                    # Clean the data stream by removing the tag
                    data = data.replace(b'IMG_START', b'')

                # --- State Machine: End of Image ---
                if b'IMG_END' in data:
                    print("\n[RX] Footer detected. Finalizing...")
                    is_receiving = False
                    data = data.replace(b'IMG_END', b'')
                    buffer += data
                    
                    # Calculate stats
                    duration = time.time() - start_time
                    size_kb = len(buffer) / 1024
                    
                    # Save the file
                    timestamp = int(time.time())
                    filename = f"downloads/received_{timestamp}.jpg"
                    
                    with open(filename, "wb") as f:
                        f.write(buffer)
                        
                    print("-" * 40)
                    print(f"SUCCESS: Image saved to {filename}")
                    print(f"Stats: {size_kb:.2f} KB in {duration:.1f} seconds")
                    print("-" * 40)
                    
                    buffer = b"" # Reset
                    continue

                # --- State Machine: Accumulating Data ---
                if is_receiving:
                    buffer += data
                    # Print visual feedback (dots) every ~200 bytes
                    if len(buffer) % 220 == 0:
                        print(".", end="", flush=True)

        except KeyboardInterrupt:
            print("\n[System] Stopping receiver...")
            break
        except Exception as e:
            print(f"\n[Error] {e}")

if __name__ == "__main__":
    main()