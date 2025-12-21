import serial
import time

# CONFIGURATION
SERIAL_PORT = '/dev/ttyUSB0'
BAUD_RATE = 115200
OUTPUT_FILE = 'received_image.jpg'

def receive_image():
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=None)
    print("Listening for LoRa data...")

    file_data = b""
    expected_size = 0
    receiving = False

    try:
        while True:
            # We read chunk by chunk. 
            # Note: Serial.read() might return fewer bytes than sent if not careful,
            # but since we are piping byte-for-byte from ESP32, we just append.
            
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                
                # Check for Start Flag
                if not receiving and b"START:" in data:
                    try:
                        decoded = data.decode('utf-8', errors='ignore')
                        size_str = decoded.split("START:")[1]
                        expected_size = int(size_str)
                        print(f"Incoming image size: {expected_size} bytes")
                        receiving = True
                        file_data = b"" # Reset buffer
                    except:
                        pass
                
                elif receiving:
                    file_data += data
                    print(f"Received: {len(file_data)} / {expected_size} bytes")

                    if len(file_data) >= expected_size:
                        print("Image received completely.")
                        with open(OUTPUT_FILE, "wb") as f:
                            f.write(file_data)
                        print(f"Saved to {OUTPUT_FILE}")
                        receiving = False
                        expected_size = 0
                        
    except KeyboardInterrupt:
        ser.close()
        print("Stopped.")

if __name__ == "__main__":
    receive_image()