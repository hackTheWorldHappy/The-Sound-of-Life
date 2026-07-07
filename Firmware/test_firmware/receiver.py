#!/usr/bin/env python3
"""
Serial Audio Receiver for ESP32 Theremin
Receives 16-bit PCM frames over serial and plays via sounddevice.
"""

import serial
import sounddevice as sd
import numpy as np
import sys
import time
import threading

SAMPLE_RATE = 22050
FRAME_SAMPLES = 64
BAUD = 230400
SYNC_BYTE1 = 0xAA
SYNC_BYTE2 = 0x55

def find_serial_port():
    import serial.tools.list_ports
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if 'USB' in p.description or 'ACM' in p.description or 'CP210' in p.description:
            return p.device
    return ports[0].device if ports else None

def parse_frames(data, buffer):
    """Parse binary frames from serial stream. Returns (frames, remaining_buffer)."""
    frames = []
    i = 0
    while i < len(data):
        # Find sync bytes
        if i + 1 >= len(data):
            break
        if data[i] != SYNC_BYTE1 or data[i+1] != SYNC_BYTE2:
            i += 1
            continue
        
        if i + 3 >= len(data):
            break
        
        frame_len = data[i+2]
        if frame_len != FRAME_SAMPLES:
            i += 3
            continue
        
        payload_start = i + 3
        payload_end = payload_start + frame_len * 2 + 1  # samples*2 + checksum
        
        if payload_end > len(data):
            break
        
        payload = data[payload_start:payload_end-1]
        checksum = data[payload_end-1]
        
        # Verify checksum
        calc_checksum = SYNC_BYTE1 ^ SYNC_BYTE2 ^ frame_len
        for b in payload:
            calc_checksum ^= b
        
        if calc_checksum == checksum:
            frames.append(payload)
        
        i = payload_end
    
    return frames, data[i:]

def audio_callback(outdata, frames, time_info, status):
    """Callback for sounddevice output stream."""
    try:
        chunk = audio_queue.get_nowait()
    except:
        chunk = np.zeros(frames, dtype=np.int16)
    
    if len(chunk) < frames:
        chunk = np.pad(chunk, (0, frames - len(chunk)))
    elif len(chunk) > frames:
        chunk = chunk[:frames]
    
    outdata[:, 0] = chunk.astype(np.float32) / 32768.0

def serial_reader(ser):
    """Background thread: read serial, parse frames, push to queue."""
    global audio_queue
    buf = bytearray()
    while running:
        try:
            n = ser.in_waiting
            if n:
                buf += ser.read(n)
            else:
                time.sleep(0.001)
                continue
        except serial.SerialException as e:
            print(f"Serial disconnected: {e}")
            break
        except OSError as e:
            if e.errno == 5:
                print("Serial I/O error (device disconnected?)")
            else:
                print(f"Serial OS error: {e}")
            break
        except Exception as e:
            print(f"Serial read error: {e}")
            time.sleep(0.1)
            continue
        
        frames, buf = parse_frames(buf, buf)
        for frame in frames:
            samples = np.frombuffer(frame, dtype='<i2')
            try:
                audio_queue.put_nowait(samples)
            except:
                pass  # Queue full, drop frame

if __name__ == '__main__':
    while True:
        port = find_serial_port()
        if not port:
            print("No serial port found, retrying in 2s...")
            time.sleep(2)
            continue
        
        print(f"Opening {port} @ {BAUD} baud...")
        try:
            ser = serial.Serial(port, BAUD, timeout=0.1)
        except serial.SerialException as e:
            print(f"Failed to open port: {e}, retrying in 2s...")
            time.sleep(2)
            continue
        
        time.sleep(0.5)
        ser.reset_input_buffer()
        
        audio_queue = __import__('queue').Queue(maxsize=10)
        running = True
        
        reader_thread = threading.Thread(target=serial_reader, args=(ser,), daemon=True)
        reader_thread.start()
        
        print("Starting audio stream...")
        try:
            with sd.OutputStream(
                samplerate=SAMPLE_RATE,
                channels=1,
                dtype='float32',
                blocksize=FRAME_SAMPLES,
                callback=audio_callback
            ):
                print("Playing... Press Ctrl+C to stop")
                while running:
                    time.sleep(0.1)
        except KeyboardInterrupt:
            print("\nStopping...")
            running = False
            ser.close()
            break
        except Exception as e:
            print(f"Audio error: {e}")
        finally:
            running = False
            try:
                ser.close()
            except:
                pass
        
        print("Disconnected, reconnecting in 2s...")
        time.sleep(2)
