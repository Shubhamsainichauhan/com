import sys
import os
import time
import serial
import serial.tools.list_ports

# Output filepath for reassembled binary file (e.g. image)
OUTPUT_FILEPATH = "received_image.jpg"

def list_serial_ports():
    ports = serial.tools.list_ports.comports()
    print("Available serial ports:")
    for i, port in enumerate(ports):
        print(f"  [{i}] {port.device} - {port.description}")
    return ports

def main():
    print("==================================================")
    print("      LoRa CCSDS Ground Station Receiver Script   ")
    print("==================================================")
    
    ports = list_serial_ports()
    if not ports:
        print("[ERROR] No serial ports found. Plug in your receiver board.")
        sys.exit(1)
        
    selected_port = None
    if len(sys.argv) > 1:
        selected_port = sys.argv[1]
    else:
        # Default to COM22 if present, otherwise ports[0]
        for port in ports:
            if port.device.upper() == "COM22":
                selected_port = port.device
                break
        if not selected_port:
            selected_port = ports[0].device
            
    baud_rate = 9600
    
    print(f"\nOpening {selected_port} at {baud_rate} baud...")
    ser = serial.Serial(selected_port, baud_rate, timeout=0.1) # low timeout for responsive reading
    ser.dtr = True
    ser.rts = True
    
    time.sleep(2)
    print("Listening for LoRa chunks... (Transmitter sends in a loop)\n")
    
    reassembled_data = bytearray()
    in_progress = False
    
    # We will read byte-by-byte and buffer text or parse binary CHUNK: markers
    text_buffer = bytearray()
    
    try:
        while True:
            # Read whatever bytes are available
            data = ser.read(1)
            if not data:
                continue
                
            text_buffer.extend(data)
            
            # 1. Check if we hit a "CHUNK:" marker in the stream
            if len(text_buffer) >= 6 and text_buffer[-6:] == b"CHUNK:":
                # If we had printed text, flush the line
                text_buffer = text_buffer[:-6]
                if text_buffer:
                    try:
                        print(text_buffer.decode('utf-8', errors='ignore').strip())
                    except Exception:
                        pass
                    text_buffer = bytearray()
                
                # We have matched "CHUNK:". Now read metadata (3 bytes: 2 length, 1 flags)
                meta = ser.read(3)
                if len(meta) < 3:
                    print("[ERROR] Truncated CHUNK: metadata.")
                    continue
                    
                payload_len = meta[0] | (meta[1] << 8)
                seq_flags = meta[2]
                
                # Read payload data
                payload = ser.read(payload_len)
                if len(payload) < payload_len:
                    print(f"[ERROR] Truncated payload. Expected {payload_len} bytes, got {len(payload)}.")
                    continue
                
                print(f"[RAW RX] Chunk matched. Size: {payload_len} bytes | Flags: {seq_flags:#04x}")
                
                # Reassemble based on CCSDS Sequence Flags
                # Flags: 01 = First, 00 = Continuation, 10 (0x02) = Last, 11 (0x03) = Standalone
                if seq_flags == 0x01: # First segment
                    reassembled_data = bytearray(payload)
                    in_progress = True
                    print(f"  -> First Segment started. Buffered {len(payload)} bytes.")
                elif seq_flags == 0x03: # Standalone
                    reassembled_data = bytearray(payload)
                    in_progress = False
                    print(f"  -> Standalone packet received. Saving directly...")
                    with open(OUTPUT_FILEPATH, 'wb') as f:
                        f.write(reassembled_data)
                    print(f"  [SAVED] File successfully saved to: {OUTPUT_FILEPATH}")
                elif seq_flags == 0x00: # Continuation
                    if in_progress:
                        reassembled_data.extend(payload)
                        print(f"  -> Continuation Segment. Total buffered: {len(reassembled_data)} bytes.")
                    else:
                        print("  -> [WARNING] Ignored continuation segment (no transfer in progress).")
                elif seq_flags == 0x02: # Last segment
                    if in_progress:
                        reassembled_data.extend(payload)
                        in_progress = False
                        print(f"  -> Last Segment received. Total file size: {len(reassembled_data)} bytes. Saving...")
                        with open(OUTPUT_FILEPATH, 'wb') as f:
                            f.write(reassembled_data)
                        print(f"  [SAVED] File successfully saved to: {OUTPUT_FILEPATH}")
                    else:
                        print("  -> [WARNING] Ignored last segment (no transfer in progress).")
                continue
            
            # 2. Check for newline to print text lines (like debug or status logs)
            if data == b'\n':
                try:
                    line_str = text_buffer.decode('utf-8', errors='ignore').strip()
                    if line_str:
                        # Output normal prints
                        print(line_str)
                except Exception:
                    pass
                text_buffer = bytearray()
                
            # Limit buffer size to prevent infinite growth
            if len(text_buffer) > 1000:
                text_buffer = text_buffer[-100:]
                
    except KeyboardInterrupt:
        print("\nExiting.")
    finally:
        ser.close()

if __name__ == '__main__':
    main()
