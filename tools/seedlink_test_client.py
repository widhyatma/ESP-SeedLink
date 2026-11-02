#!/usr/bin/env python3
"""
SeedLink Test Client
Digunakan untuk menguji dan memantau live streaming dari ESP32 Seismometer SeedLink Server.
"""

import socket
import argparse
import sys
import io
import time
import math
import obspy

def render_sparkline(samples, width=40):
    """Membuat visualisasi waveform ASCII horizontal di terminal."""
    if not len(samples):
        return ""
    # Subsampling jika sampel lebih banyak dari lebar visualisasi
    step = max(1, len(samples) // width)
    downsampled = [samples[i] for i in range(0, len(samples), step)][:width]
    
    max_val = max(abs(x) for x in downsampled) or 1
    chars = " _.-:=+*#%@"
    n_chars = len(chars) - 1
    
    line = []
    for val in downsampled:
        norm = (val / max_val + 1.0) / 2.0  # Range 0.0 s/d 1.0
        idx = min(n_chars, max(0, int(norm * n_chars)))
        line.append(chars[idx])
    return "".join(line)

def main():
    parser = argparse.ArgumentParser(description="SeedLink Test Client untuk ESP32 Seismometer")
    parser.add_argument("--host", default="192.168.18.34", help="IP address ESP32 (default: 192.168.18.34)")
    parser.add_argument("--port", type=int, default=18000, help="Port SeedLink (default: 18000)")
    parser.add_argument("--station", default="JERIS", help="Station code (default: JERIS)")
    parser.add_argument("--network", default="AM", help="Network code (default: AM)")
    parser.add_argument("--channel", default="EHZ", help="Channel code (default: EHZ)")
    parser.add_argument("--max-packets", type=int, default=0, help="Batas jumlah paket sebelum berhenti (0 = tidak terbatas)")
    args = parser.parse_args()

    print("==========================================================")
    print("           SEEDLINK TEST CLIENT (ObsPy Engine)            ")
    print("==========================================================")
    print(f" Menghubungkan ke : {args.host}:{args.port}")
    print(f" Target Stream    : {args.network}.{args.station}..{args.channel}")
    print(" Tekan Ctrl+C untuk berhenti secara aman (BYE).")
    print("==========================================================\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10.0)

    try:
        sock.connect((args.host, args.port))
        print("[CLIENT] Terhubung ke server!")
    except Exception as e:
        print(f"[ERROR] Gagal terhubung ke {args.host}:{args.port} -> {e}")
        print("\nTips:")
        print("  1. Pastikan ESP32 sudah menyala dan terhubung ke jaringan.")
        print("  2. Jika menggunakan mode SoftAP ESP32, hubungkan Wi-Fi laptop ke 'ESP32-SEISMOMETER' (IP: 192.168.4.1).")
        print("  3. Jika menggunakan Wi-Fi router, gunakan IP lokal ESP32 yang tertera di Serial Monitor.")
        sys.exit(1)

    try:
        # 1. HELLO Handshake
        sock.sendall(b"HELLO\r\n")
        resp = sock.recv(1024).decode('ascii', errors='ignore')
        print(f"[CLIENT] HELLO Response:\n{resp.strip()}\n")

        # 2. STATION Command
        cmd_station = f"STATION {args.station} {args.network}\r\n".encode('ascii')
        sock.sendall(cmd_station)
        resp = sock.recv(256).decode('ascii', errors='ignore')
        print(f"[CLIENT] STATION Response: {resp.strip()}")

        # 3. SELECT Command
        cmd_select = f"SELECT {args.channel}\r\n".encode('ascii')
        sock.sendall(cmd_select)
        resp = sock.recv(256).decode('ascii', errors='ignore')
        print(f"[CLIENT] SELECT Response : {resp.strip()}")

        # 4. DATA Command
        sock.sendall(b"DATA\r\n")
        resp = sock.recv(256).decode('ascii', errors='ignore')
        print(f"[CLIENT] DATA Response   : {resp.strip()}")

        # 5. END Command (Mulai streaming)
        sock.sendall(b"END\r\n")
        print("[CLIENT] END dikirim. Memasuki mode streaming paket...\n")
        print(f"{'SEQ':<8} | {'TIMESTAMP (UTC)':<24} | {'SMPL':<5} | {'RATE':<5} | {'PEAK(counts)':<12} | {'WAVEFORM SPARKLINE':<40}")
        print("-" * 110)

        packet_count = 0
        buf = b""

        while True:
            # Baca data sampai mencapai 520 byte (1 paket SeedLink)
            while len(buf) < 520:
                chunk = sock.recv(520 - len(buf))
                if not chunk:
                    print("\n[CLIENT] Koneksi ditutup oleh server.")
                    return
                buf += chunk

            packet = buf[:520]
            buf = buf[520:]

            # Parse SeedLink Header (8 bytes)
            sl_magic = packet[:2].decode('ascii', errors='ignore')
            sl_seq_hex = packet[2:8].decode('ascii', errors='ignore')
            mseed_record = packet[8:520]

            if sl_magic != "SL":
                print(f"[WARN] Magic bytes tidak valid: {sl_magic}, mengabaikan paket.")
                continue

            packet_count += 1

            # Parse MiniSEED payload dengan ObsPy
            try:
                st = obspy.read(io.BytesIO(mseed_record), format='MSEED')
                tr = st[0]
                samples = tr.data
                max_amp = max(abs(int(x)) for x in samples) if len(samples) else 0
                spark = render_sparkline(samples, width=38)
                t_str = tr.stats.starttime.strftime("%Y-%m-%d %H:%M:%S")

                print(f"SL{sl_seq_hex:<6} | {t_str:<24} | {tr.stats.npts:<5} | {tr.stats.sampling_rate:<5.0f} | {max_amp:<12} | [{spark}]")

                # Cek jika ada simulasi gempa (amplitudo tinggi)
                if max_amp > 1000:
                    print(f"         >>> EVENT GEMPA TERDETEKSI! Amplitudo Puncak: {max_amp} counts <<<")

            except Exception as e:
                print(f"[ERROR] Gagal mem-parse MiniSEED: {e}")

            if args.max_packets > 0 and packet_count >= args.max_packets:
                print(f"\n[CLIENT] Mencapai batas paket ({args.max_packets}). Berhenti.")
                break

    except KeyboardInterrupt:
        print("\n[CLIENT] Pengguna menekan Ctrl+C. Mengirim sinyal BYE...")
        try:
            sock.sendall(b"BYE\r\n")
            time.sleep(0.2)
        except Exception:
            pass
    finally:
        sock.close()
        print("[CLIENT] Selesai. Koneksi ditutup.")

if __name__ == '__main__':
    main()

