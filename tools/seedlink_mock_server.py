#!/usr/bin/env python3
"""
SeedLink Mock Server (Python Standalone)
Menjalankan server SeedLink lokal pada port 18000 untuk pengujian software
seismologi (ObsPy, SWARM, Jamaseis, SeisComP) di PC tanpa memerlukan hardware ESP32 fisik.
"""

import socket
import struct
import time
import math
import random
import datetime

HOST = "0.0.0.0"
PORT = 18000
STATION = "JERIS"
NETWORK = "XX"
LOCATION = "00"
CHANNEL = "EHZ"
SAMPLE_RATE = 50
SAMPLES_PER_RECORD = 100

CHANNELS = ["EHZ", "EHN", "EHE", "HHZ", "HHN", "HHE", "BHZ", "BHN", "BHE", "SHZ", "SHN", "SHE"]

# Filter states global untuk mock server
_lp1 = 0.0
_lp2 = 0.0
_dc_x1 = 0.0
_dc_y1 = 0.0
_ocean_phase1 = 0.0
_ocean_phase2 = 0.0

def generate_samples(count, t_start):
    """Menghasilkan sampel seismik sintetis yang alami (colored Gaussian noise + microseism)."""
    global _lp1, _lp2, _dc_x1, _dc_y1, _ocean_phase1, _ocean_phase2
    samples = []
    dt = 1.0 / SAMPLE_RATE
    for _ in range(count):
        # 1. Gaussian noise
        white = random.gauss(0.0, 1.0) * 120.0
        
        # 2. 2-Pole IIR lowpass filter (~8 Hz cutoff)
        alpha = 0.45
        _lp1 += alpha * (white - _lp1)
        _lp2 += alpha * (_lp1 - _lp2)

        # 3. Ocean microseisms
        _ocean_phase1 += 2.0 * math.pi * 0.15 * dt
        _ocean_phase2 += 2.0 * math.pi * 0.23 * dt
        ocean = 25.0 * math.sin(_ocean_phase1) + 18.0 * math.sin(_ocean_phase2)

        raw = _lp2 + ocean

        # 4. DC Blocker
        dc_out = raw - _dc_x1 + 0.995 * _dc_y1
        _dc_x1 = raw
        _dc_y1 = dc_out

        samples.append(int(dc_out))
    return samples

def build_seedlink_packet(seq_num, samples, start_dt):
    """Membangun paket 520 byte SeedLink (8-byte header + 512-byte MiniSEED)."""
    # 1. 8-byte ASCII header: "SL" + 6-digit hex
    sl_header = f"SL{seq_num:06X}".encode('ascii')

    # 2. 48-byte FSDH
    fmt_fsdh = '>6sc1s5s2s3s2sHHBBBBHHhhBBBBiHH'
    seq_str = f"{seq_num % 1000000:06d}".encode('ascii')
    
    year = start_dt.year
    doy = start_dt.timetuple().tm_yday
    hour = start_dt.hour
    minute = start_dt.minute
    sec = start_dt.second
    frac_sec = int(start_dt.microsecond / 100)  # 0.0001s unit

    fsdh = struct.pack(fmt_fsdh,
        seq_str, b'D', b' ',
        STATION.ljust(5).encode('ascii')[:5],
        LOCATION.ljust(2).encode('ascii')[:2],
        CHANNEL.ljust(3).encode('ascii')[:3],
        NETWORK.ljust(2).encode('ascii')[:2],
        year, doy, hour, minute, sec, 0, frac_sec,
        len(samples), SAMPLE_RATE, 1,
        0, 0, 0, 1,
        0, 64, 48
    )

    # 3. Blockette 1000 (8 bytes)
    b1000 = struct.pack('>HHBBBB', 1000, 0, 3, 1, 9, 0)

    # 4. Padding offset 56 to 64
    padding = b'\x00' * (64 - 56)

    # 5. Data samples (INT32 Big-Endian)
    data = struct.pack(f'>{len(samples)}i', *samples)

    # MiniSEED 512-byte record
    mseed = (fsdh + b1000 + padding + data).ljust(512, b'\x00')
    return sl_header + mseed

def main():
    print("==========================================================")
    print("      SEEDLINK PC MOCK SERVER (Seismometer Emulator)      ")
    print("==========================================================")
    print(f" Mendengarkan di : {HOST}:{PORT}")
    print(f" Stasiun Target  : {NETWORK}.{STATION}.{LOCATION}.{CHANNEL} @ {SAMPLE_RATE}Hz")
    print("==========================================================\n")

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(1)

    while True:
        print("[MOCK] Menunggu koneksi client...")
        conn, addr = server.accept()
        print(f"[MOCK] Client terhubung dari {addr[0]}:{addr[1]}")

        buf = b""
        streaming = False

        while not streaming:
            try:
                chunk = conn.recv(1024)
            except (ConnectionResetError, ConnectionAbortedError, OSError):
                break
            if not chunk:
                break
            buf += chunk
            while b'\r' in buf or b'\n' in buf:
                idx_r = buf.find(b'\r')
                idx_n = buf.find(b'\n')
                idx = min(idx_r if idx_r != -1 else 9999, idx_n if idx_n != -1 else 9999)
                line = buf[:idx].strip().decode('ascii', errors='ignore')
                buf = buf[idx+1:]
                if not line:
                    continue
                
                parts = line.split()
                cmd = parts[0].upper()
                print(f"[MOCK] RX: {line}")

                if cmd == "HELLO":
                    resp = f"SeedLink v3.1 (Python-Mock-Seismometer) :: SLPROTO:3.1\r\nMock Seismic Station {STATION}\r\n".encode('ascii')
                    conn.sendall(resp)
                elif cmd == "INFO":
                    xml_streams = f'<?xml version="1.0" encoding="UTF-8"?><seedlink><station name="{STATION}" network="{NETWORK}" net="{NETWORK}" description="Jerukagung Seismologi">'
                    for ch in CHANNELS:
                        xml_streams += f'<stream location="{LOCATION}" seedname="{ch}" schan="{ch}" slocation="{LOCATION}" type="D" rate="{SAMPLE_RATE}"/>'
                    xml_streams += '</station></seedlink>'
                    
                    xml_bytes = xml_streams.encode('utf-8')
                    # Kirim dalam chunk max 448 bytes
                    offset = 0
                    while offset < len(xml_bytes):
                        chunk = xml_bytes[offset:offset+448]
                        offset += len(chunk)
                        is_last = (offset >= len(xml_bytes))
                        hdr = b"SLINFO  " if is_last else b"SLINFO *"
                        fsdh = struct.pack('>6sc1s5s2s3s2sHHBBBBHHhhBBBBiHH',
                            b'000000', b'D', b' ',
                            STATION.ljust(5).encode('ascii')[:5],
                            LOCATION.ljust(2).encode('ascii')[:2],
                            b'LOG',
                            NETWORK.ljust(2).encode('ascii')[:2],
                            2026, 1, 0, 0, 0, 0, 0,
                            len(chunk), 0, 0,
                            0, 0, 0, 1,
                            0, 64, 48
                        )
                        b1000 = struct.pack('>HHBBBB', 1000, 0, 0, 1, 9, 0)
                        padding = b'\x00' * (64 - 56)
                        mseed = (fsdh + b1000 + padding + chunk).ljust(512, b'\x00')
                        conn.sendall(hdr + mseed)
                elif cmd in ("STATION", "SELECT", "DATA", "FETCH", "USERAGENT", "TIME", "SLPROTO"):
                    conn.sendall(b"OK\r\n")
                elif cmd == "END":
                    print("[MOCK] Masuk ke mode streaming real-time!")
                    streaming = True
                    break
                elif cmd == "BYE":
                    print("[MOCK] Client meminta BYE.")
                    conn.close()
                    break

        if streaming:
            seq = 1
            t_base = time.time()
            record_duration = SAMPLES_PER_RECORD / SAMPLE_RATE  # 2.0 detik
            try:
                while True:
                    start_dt = datetime.datetime.now(datetime.timezone.utc)
                    samples = generate_samples(SAMPLES_PER_RECORD, time.time() - t_base)
                    packet = build_seedlink_packet(seq, samples, start_dt)
                    
                    conn.sendall(packet)
                    print(f"[MOCK] TX Packet #{seq} (SL{seq:06X}) terkirim.")
                    seq += 1
                    time.sleep(record_duration)
            except (ConnectionResetError, BrokenPipeError):
                print("[MOCK] Client terputus.")
            finally:
                conn.close()

if __name__ == '__main__':
    main()

