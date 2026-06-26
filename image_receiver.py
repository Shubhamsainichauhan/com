import sys
import os
import time
import serial
import serial.tools.list_ports

# Will save in the same folder you run the script from
OUTPUT_FILEPATH = "received_image.ccsds"

def list_serial_ports():
    ports = serial.tools.list_ports.comports()
    print("Available serial ports:")
    for i, port in enumerate(ports):
        print(f"  [{i}] {port.device} - {port.description}")
    return ports

def draw_progress_bar(received, total, file_id):
    bar_length = 40
    progress = received / total
    block = int(round(bar_length * progress))
    bar = "#" * block + "-" * (bar_length - block)
    percent = progress * 100
    sys.stdout.write(f"\rProgress: [{bar}] {percent:.1f}% ({received}/{total} Chunks)")
    sys.stdout.flush()

def main():
    print("==================================================")
    print("      LoRa CCSDS Image Reassembly Ground Station  ")
    print("==================================================")
    
    ports = list_serial_ports()
    if not ports:
        print("[ERROR] No serial ports found. Plug in your receiver board.")
        sys.exit(1)
        
    selected_port = None
    if len(sys.argv) > 1:
        selected_port = sys.argv[1]
    else:
        for port in ports:
            if port.device.upper() == "COM11":
                selected_port = port.device
                break
        if not selected_port:
            selected_port = ports[0].device
            
    baud_rate = 9600
    
    print(f"\nOpening {selected_port} at {baud_rate} baud...")
    ser = serial.Serial(selected_port, baud_rate, timeout=1.0)
    ser.dtr = True
    ser.rts = True
        
    time.sleep(2)
    print("Listening for LoRa chunks... (Transmitter sends in a loop)\n")
    
    active_file_id = None
    total_chunks = None
    received_chunks = {}
    
    try:
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue
                
            if line.startswith("[DATA]"):
                payload_part = line[6:]
                meta_str, hex_str = payload_part.split(':')
                file_id, chunk_idx, tot_chunks, chunk_size = map(int, meta_str.split(','))
                
                # Filter out corrupted packets
                if tot_chunks != 429 or chunk_idx >= tot_chunks or chunk_size > 200:
                    continue
                    
                payload = bytearray.fromhex(hex_str)

                if active_file_id is None or file_id != active_file_id:
                    print(f"\n\n[INIT] Synced with Transmitter! Waiting for chunks...")
                    active_file_id = file_id
                    total_chunks = tot_chunks
                    received_chunks = {}

                received_chunks[chunk_idx] = payload
                draw_progress_bar(len(received_chunks), total_chunks, active_file_id)

                # Check if all chunks received
                if len(received_chunks) == total_chunks:
                    print(f"\n\n🎉 All {total_chunks} chunks successfully received!")

                    reassembled_data = bytearray()
                    for idx in range(total_chunks):
                        reassembled_data.extend(received_chunks[idx])

                    with open(OUTPUT_FILEPATH, 'wb') as out_f:
                        out_f.write(reassembled_data)
                    print(f"[SAVED] Real image successfully saved to: {OUTPUT_FILEPATH}")

                    active_file_id = None # Reset for next broadcast

            elif line.startswith("[DEBUG]"):
                if active_file_id is not None:
                    sys.stdout.write("\r\033[K")
                    print(line)
                    draw_progress_bar(len(received_chunks), total_chunks, active_file_id)
                else:
                    print(line)
            else:
                print(line)

    except KeyboardInterrupt:
        print("\nExiting.")
    finally:
        ser.close()

if __name__ == '__main__':
    main()
