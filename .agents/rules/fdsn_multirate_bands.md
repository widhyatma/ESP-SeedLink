# Aturan IV: Standar Pemetaan Band FDSN SEED Multi-Rate (E, B, S)

## Deskripsi & Tujuan
Standarisasi konfigurasi saluran, laju cuplik (*sampling rate*), dan pembungkus paket MiniSEED multi-rate untuk stasiun seismometer sesuai konvensi internasional FDSN (*International Federation of Digital Seismograph Networks*).

## Spesifikasi Band & Komponen

1. **Definisi Band Seismik:**
   - **Band E (Extremely Short Period / High Rate):** Wajib **$100\text{ Hz}$** ($10.000\ \mu\text{s}$ interval cuplik). Digunakan untuk deteksi getaran frekuensi tinggi atau mikroseismik lokal.
   - **Band B (Broadband):** Standar **$50\text{ Hz}$** ($20.000\ \mu\text{s}$ interval cuplik). Standar jaringan nasional BMKG untuk perekaman gempa regional dan teleseismik.
   - **Band S (Short Period):** Standar **$50\text{ Hz}$** ($20.000\ \mu\text{s}$ interval cuplik). Karakteristik sensor geophone frekuensi natural $1\dots2\text{ Hz}$.

2. **Komposisi 3-Komponen Triaksial:**
   - Setiap band memiliki 3 komponen orientasi orthogonal:
     - $Z$: Vertikal
     - $N$: Horizontal Utara-Selatan
     - $E$: Horizontal Timur-Barat
   - Saluran terdaftar aktif (9 saluran):
     - Band E: `EHZ`, `EHN`, `EHE`
     - Band B: `BHZ`, `BHN`, `BHE`
     - Band S: `SHZ`, `SHN`, `SHE`

3. **Arsitektur Sampling Multi-Rate Sinkron:**
   - Master loop timer berjalan pada frekuensi tertinggi ($100\text{ Hz} = 10.000\ \mu\text{s}$).
   - Saluran Band E disampel setiap tick loop ($k = 0, 1, 2, \dots$).
   - Saluran Band B dan S disampel pada setiap tick genap ($k \pmod 2 == 0$).

4. **Ukuran Record MiniSEED Seragam 1.0 Detik:**
   - Saluran 100 Hz menggunakan buffer 100 sampel per paket record.
   - Saluran 50 Hz menggunakan buffer 50 sampel per paket record.
   - Semua saluran membungkus tepat durasi 1.0 detik data riil dan disalurkan berurutan dengan jeda mikroskopis 2 ms antar saluran untuk menjaga kestabilan aliran LwIP TCP.

5. **Penjamin Kontinuitas Timestamp (Zero-Gap BTIME):**
   - Setiap saluran mengelola pelacakan waktu awal record (*BTIME*) tersendiri.
   - Timestamp dimajukan secara eksak menggunakan durasi matematis:
     $$\Delta t = \frac{N_{\text{samples}}}{f_s}$$
   - Mencegah timbulnya celah semu (*false data gaps*) pada klien visualisasi seperti USGS SWARM.

