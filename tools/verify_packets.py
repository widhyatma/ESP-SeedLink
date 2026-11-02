"""
Unit test untuk memvalidasi kepatuhan format MiniSEED v2 & SeedLink 520-byte packet.
"""

import struct
import io
import sys
import obspy

def test_miniseed_packet():
    print("==================================================")
    print("   TEST 1: MiniSEED v2 512-Byte Record Structure   ")
    print("==================================================")

    # 48 bytes FSDH
    fmt_fsdh = '>6sc1s5s2s3s2sHHBBBBHHhhBBBBiHH'
    assert struct.calcsize(fmt_fsdh) == 48, f"FSDH size error: {struct.calcsize(fmt_fsdh)}"

    seq_str = b'000001'
    quality = b'D'
    reserved = b' '
    station = b'ESP01'
    location = b'00'
    channel = b'EHZ'
    network = b'XX'

    year = 2026
    doy = 304
    hour = 12
    minute = 30
    sec = 0
    unused = 0
    frac_sec = 0  # 0.0001s unit

    num_samples = 100
    sr_factor = 50
    sr_multiplier = 1
    act_flags = 0
    io_flags = 0
    dq_flags = 0
    num_blockettes = 1
    time_corr = 0
    data_offset = 64
    blockette_offset = 48

    fsdh = struct.pack(fmt_fsdh,
        seq_str, quality, reserved, station, location, channel, network,
        year, doy, hour, minute, sec, unused, frac_sec,
        num_samples, sr_factor, sr_multiplier,
        act_flags, io_flags, dq_flags, num_blockettes,
        time_corr, data_offset, blockette_offset
    )

    # Blockette 1000 (8 bytes)
    b1000 = struct.pack('>HHBBBB', 1000, 0, 3, 1, 9, 0)
    assert len(b1000) == 8

    # Padding (offset 56 to 64)
    padding = b'\x00' * (data_offset - (48 + 8))

    # Synthetic samples (100 samples int32)
    synthetic_samples = [int(1500 * (i % 8)) for i in range(num_samples)]
    sample_bytes = struct.pack(f'>{num_samples}i', *synthetic_samples)

    # Full 512 byte record
    mseed_record = (fsdh + b1000 + padding + sample_bytes).ljust(512, b'\x00')
    assert len(mseed_record) == 512, f"MiniSEED record size mismatch: {len(mseed_record)}"

    # Parse with ObsPy
    st = obspy.read(io.BytesIO(mseed_record), format='MSEED')
    tr = st[0]

    print("[PASS] MiniSEED successfully parsed by ObsPy!")
    print(f"       Trace ID      : {tr.id}")
    print(f"       Sampling Rate : {tr.stats.sampling_rate} Hz")
    print(f"       Start Time    : {tr.stats.starttime}")
    print(f"       End Time      : {tr.stats.endtime}")
    print(f"       Num Samples   : {tr.stats.npts}")
    print(f"       Encoding      : {tr.stats.mseed.encoding}")
    print(f"       Byte Order    : {tr.stats.mseed.byteorder}")

    assert tr.stats.network == 'XX'
    assert tr.stats.station == 'ESP01'
    assert tr.stats.location == '00'
    assert tr.stats.channel == 'EHZ'
    assert tr.stats.sampling_rate == 50.0
    assert tr.stats.npts == 100
    assert list(tr.data[:8]) == synthetic_samples[:8]

    print("\n==================================================")
    print("   TEST 2: SeedLink 520-Byte Envelope Format      ")
    print("==================================================")

    sl_header = b'SL000001'
    sl_packet = sl_header + mseed_record
    assert len(sl_packet) == 520, f"SeedLink packet size mismatch: {len(sl_packet)}"

    magic = sl_packet[:2]
    seq_hex = sl_packet[2:8].decode('ascii')
    payload = sl_packet[8:]

    print(f"[PASS] Magic Bytes : {magic.decode('ascii')}")
    print(f"[PASS] Sequence Hex: 0x{seq_hex} ({int(seq_hex, 16)})")
    print(f"[PASS] Payload Size: {len(payload)} bytes")

    # Verify payload is valid MiniSEED
    st_payload = obspy.read(io.BytesIO(payload), format='MSEED')
    assert len(st_payload) == 1
    print("[PASS] SeedLink 520-byte packet validated perfectly!")
    print("==================================================")

if __name__ == '__main__':
    test_miniseed_packet()

