import os
import time
import math
import zlib
import serial
from PIL import Image

# ============================
# CONFIG
# ============================
SERIAL_PORT = '/dev/serial0'
BAUDRATE = 115200
INPUT_DIR = "./raw_photos"        # foto asli dari camera trap (anda bisa ubah)
THUMB_DIR = "./thumbs"            # akan dibuat otomatis
NODE_ID = "N1"
CHUNK_SIZE = 50
# ============================

ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)

# Buat folder thumbnail jika belum ada
os.makedirs(THUMB_DIR, exist_ok=True)


def make_thumbnail(src_path):
    """
    Resize foto asli → thumbnail 160×120 → simpan jpeg kualitas 30.
    """
    img = Image.open(src_path)

    # Resize
    img.thumbnail((160, 120))

    # Nama thumbnail
    base = os.path.basename(src_path)
    thumb_path = os.path.join(THUMB_DIR, base)

    # Save JPEG (kualitas rendah agar 2–6 KB)
    img.save(thumb_path, format="JPEG", quality=30)

    return thumb_path, base


def send_thumbnail(thumb_path, filename, file_id):
    """
    Kirim thumbnail dalam bentuk chunk via UART → ESP32 Node → LoRa.
    """
    with open(thumb_path, "rb") as f:
        data = f.read()

    total_chunks = math.ceil(len(data) / CHUNK_SIZE)

    print(f"\nSending thumbnail: {filename}")
    print(f"Size: {len(data)} bytes → {total_chunks} chunks")

    for idx in range(total_chunks):
        chunk = data[idx*CHUNK_SIZE:(idx+1)*CHUNK_SIZE]

        crc = zlib.crc32(chunk) & 0xFFFFFFFF

        # HEADER BARU:
        # NODE | FILENAME | FILEID | INDEX | TOTAL | CRC | <binary>
        header = f"{NODE_ID}|{filename}|{file_id}|{idx}|{total_chunks}|{crc}|"
        
        packet = header.encode('latin1') + chunk

        ser.write(packet + b"\n")
        #ser.write("HELLO")
        time.sleep(0.12)   # 8–10 paket per detik → aman SF10

        print(f"Chunk {idx+1}/{total_chunks} sent")

    print("DONE")


def process_new_photo(photo_path, file_id):
    """
    Full pipeline untuk 1 foto:
    - convert thumbnail
    - chunk & send
    """
    thumb_path, filename = make_thumbnail(photo_path)
    send_thumbnail(thumb_path, filename, file_id)


# ============================
# CONTOH PEMAKAIAN
# ============================

if __name__ == "__main__":
    while True:
    # Contoh: kirim semua foto dalam folder INPUT_DIR
        file_id = int(time.time())   # ID unik file berdasarkan epoch

        for fname in os.listdir(INPUT_DIR):
            if fname.lower().endswith((".jpg", ".jpeg", ".png")):
                src = os.path.join(INPUT_DIR, fname)

                print(f"\nProcessing {fname} ...")
                process_new_photo(src, file_id)

                file_id += 1   # setiap file baru → fileid baru

        print("\nAll photos processed.\n")
    sleep(2500)

