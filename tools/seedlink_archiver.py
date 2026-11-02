#!/usr/bin/env python3
"""
SeedLink Continuous MiniSEED Archiver
Menyimpan paket MiniSEED real-time dari ESP32 SeedLink ke dalam berkas .mseed lokal
mengikuti struktur direktori SDS (SeisComP Data Structure) atau berkas harian.
"""

import socket
import argparse
import os
import sys
import datetime
import io
import obspy

def main():
    parser = argparse.ArgumentParser(description="Perekam file MiniSEED dari ESP32 SeedLink")
    parser.add_argument("--host", default="192.168.4.1", help="IP ESP32 (default: 192.168.4.1)")
    parser.add_argument("--port", type=int, default=18000, help="Port SeedLink (default: 18000)")
    parser.add_argument("--station", default="ESP01", help="Station code (default: ESP01)")
    parser.add_argument("--network", default="XX", help="Network code (default: XX)")
    parser.add_argument("--channel", default="EHZ", help="Channel code (default: EHZ)")
    parser.add_argument("--outdir", default="archive", help="Direktori penyimpanan file (default: archive/)")
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    print("=================================================================")
    print("             SEEDLINK CONTINUOUS MINISEED ARCHIVER               ")
    print("=================================================================")
    print(f" Menghubungkan ke : {args.host}:{args.port}")
    print(f" Target Stream    : {args.network}.{args.station}..{args.channel}")
    print(f" Direktori Arsip  : {os.path.abspath(args.outdir)}")
    print(" Tekan Ctrl+C untuk berhenti menyimpan.")
    print("=================================================================\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10.0)

    try:
        sock.connect((args.host, args.port))
        sock.sendall(b"HELLO\r\n")
        sock.recv(1024)
        sock.sendall(f"STATION {args.station} {args.network}\r\n".encode('ascii'))
        sock.recv(256)
        sock.sendall(f"SELECT {args.channel}\r\n".encode('ascii'))
        sock.recv(256)
        sock.sendall(b"DATA\r\n")
        sock.recv(256)
        sock.sendall(b"END\r\n")
        print("[ARCHIVER] Handshake sukses! Mulai menulis data MiniSEED...\n")

        buf = b""
        total_records = 0

        while True:
            while len(buf) < 520:
                chunk = sock.recv(520 - len(buf))
                if not chunk:
                    print("[ARCHIVER] Koneksi terputus.")
                    return
                buf += chunk

            packet = buf[:520]
            buf = buf[520:]

            mseed_record = packet[8:520]
            total_records += 1

            # Tentukan nama file berdasarkan tanggal UTC: NET.STA.LOC.CHAN.D.YYYY.DOY.mseed
            now_utc = datetime.datetime.now(datetime.timezone.utc)
            year = now_utc.year
            doy = now_utc.timetuple().tm_yday
            filename = f"{args.network}.{args.station}.00.{args.channel}.D.{year}.{doy:03d}.mseed"
            filepath = os.path.join(args.outdir, filename)

            # Append 512 bytes MiniSEED ke file
            with open(filepath, "ab") as f:
                f.write(mseed_record)

            if total_records % 5 == 0 or total_records <= 3:
                filesize_kb = os.path.getsize(filepath) / 1024.0
                print(f"[SAVED #{total_records:<4}] {filename} ({filesize_kb:.1f} KB)")

    except KeyboardInterrupt:
        print("\n[ARCHIVER] Perekaman dihentikan oleh pengguna.")
    finally:
        sock.close()

if __name__ == '__main__':
    main()

