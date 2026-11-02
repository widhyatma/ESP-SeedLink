#!/usr/bin/env python3
"""
SeedLink to DataLink Forwarder (Bridge)
Menghubungkan ESP32 Seismometer (SeedLink port 18000) ke RingServer pusat (DataLink port 16000).
Cocok digunakan jika server pusat Anda menjalankan IRIS/EarthScope RingServer.
"""

import socket
import argparse
import sys
import time
import io
import obspy

def main():
    parser = argparse.ArgumentParser(description="Forwarder dari ESP32 SeedLink ke Central RingServer (DataLink)")
    parser.add_argument("--sl-host", default="192.168.18.34", help="IP ESP32 Seismometer (default: 192.168.18.34)")
    parser.add_argument("--sl-port", type=int, default=18000, help="Port SeedLink ESP32 (default: 18000)")
    parser.add_argument("--station", default="JERIS", help="Kode stasiun (default: JERIS)")
    parser.add_argument("--network", default="AM", help="Kode network (default: AM)")
    parser.add_argument("--channel", default="EHZ", help="Kode channel (default: EHZ)")
    parser.add_argument("--dl-host", default="127.0.0.1", help="IP Central RingServer DataLink (default: 127.0.0.1)")
    parser.add_argument("--dl-port", type=int, default=16000, help="Port DataLink RingServer (default: 16000)")
    args = parser.parse_args()

    print("=================================================================")
    print("        ESP32 SEEDLINK -> CENTRAL RINGSERVER FORWARDER           ")
    print("=================================================================")
    print(f" Source (ESP32 SeedLink)  : {args.sl_host}:{args.sl_port}")
    print(f" Target (Central RingSrv) : {args.dl_host}:{args.dl_port}")
    print(f" Stream                   : {args.network}.{args.station}..{args.channel}")
    print("=================================================================\n")

    # 1. Hubungkan ke ESP32 SeedLink
    sl_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sl_sock.settimeout(10.0)
    try:
        sl_sock.connect((args.sl_host, args.sl_port))
        print("[BRIDGE] Terhubung ke ESP32 SeedLink Server.")
    except Exception as e:
        print(f"[ERROR] Gagal terhubung ke ESP32: {e}")
        sys.exit(1)

    # 2. Hubungkan ke Central RingServer (DataLink)
    dl_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    dl_sock.settimeout(10.0)
    try:
        dl_sock.connect((args.dl_host, args.dl_port))
        print("[BRIDGE] Terhubung ke Central RingServer (DataLink port 16000).")
    except Exception as e:
        print(f"[WARN] Central RingServer di {args.dl_host}:{args.dl_port} belum aktif atau tidak terjangkau ({e}).")
        print("[INFO] Bridge akan tetap memonitor dan parsing paket ESP32.")
        dl_sock = None

    # Lakukan handshake SeedLink dengan ESP32
    try:
        sl_sock.sendall(b"HELLO\r\n")
        sl_sock.recv(1024)
        sl_sock.sendall(f"STATION {args.station} {args.network}\r\n".encode('ascii'))
        sl_sock.recv(256)
        sl_sock.sendall(f"SELECT {args.channel}\r\n".encode('ascii'))
        sl_sock.recv(256)
        sl_sock.sendall(b"DATA\r\n")
        sl_sock.recv(256)
        sl_sock.sendall(b"END\r\n")
        print("[BRIDGE] Handshake selesai! Mulai meneruskan paket MiniSEED...\n")

        buf = b""
        packet_count = 0

        while True:
            while len(buf) < 520:
                chunk = sl_sock.recv(520 - len(buf))
                if not chunk:
                    print("[BRIDGE] ESP32 menutup koneksi.")
                    return
                buf += chunk

            packet = buf[:520]
            buf = buf[520:]

            mseed_record = packet[8:520]
            packet_count += 1

            # Parse metadata untuk streamid DataLink
            try:
                st = obspy.read(io.BytesIO(mseed_record), format='MSEED')
                tr = st[0]
                # Format streamid IRIS: NET_STA_LOC_CHAN/MSEED
                loc = tr.stats.location if tr.stats.location else ""
                stream_id = f"{tr.stats.network}_{tr.stats.station}_{loc}_{tr.stats.channel}/MSEED"
                
                # Hitung waktu epoch microsecond untuk DataLink header
                hp_start = int(tr.stats.starttime.timestamp * 1_000_000)
                hp_end = int(tr.stats.endtime.timestamp * 1_000_000)

                print(f"[TX #{packet_count:<4}] {stream_id:<20} | {tr.stats.starttime.strftime('%Y-%m-%d %H:%M:%S')} | {len(mseed_record)} bytes")

                # Forward ke RingServer jika koneksi DataLink aktif
                if dl_sock:
                    dl_header = f"WRITE {stream_id} {hp_start} {hp_end} N {len(mseed_record)}\r\n".encode('ascii')
                    dl_sock.sendall(dl_header + mseed_record)

            except Exception as parse_err:
                print(f"[WARN] Gagal parse paket #{packet_count}: {parse_err}")

    except KeyboardInterrupt:
        print("\n[BRIDGE] Berhenti oleh pengguna.")
    finally:
        sl_sock.close()
        if dl_sock:
            dl_sock.close()

if __name__ == '__main__':
    main()

