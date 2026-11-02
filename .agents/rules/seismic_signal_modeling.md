# Aturan II: Sintesis Sinyal Dummy Seismometer Realistis

## Deskripsi & Tujuan
Panduan pemodelan sinyal getaran dummy/sintetis untuk sensor seismik agar menyerupai karakteristik fisik seismogram riil (geofisika batuan bumi) dan tidak menimbulkan artefak atau *clipping* pada perangkat lunak analisis seperti USGS SWARM.

## Karakteristik Fisika Sinyal

1. **Derau Berwarna (Colored Gaussian Noise):**
   - Derau dasar dihasilkan menggunakan transformasi Box-Muller atau generator Gaussian yang independen antar saluran.
   - Sinyal dilewatkan ke filter *low-pass* 2-pole IIR rekursif untuk merefleksikan atenuasi mekanik batuan bumi dan respon frekuensi instrumen seismometer.

2. **Osilasi Mikroseisme Samudra (Ocean Microseisms):**
   - Menyertakan osilasi frekuensi sangat rendah alami bumi (sekitar $0,15\text{ Hz}$ dan $0,23\text{ Hz}$) beramplitudo rendah ($\approx 20\dots30\text{ counts}$).
   - Memodelkan beda fase antara sumbu vertikal ($Z$) dan horizontal ($N$, $E$).

3. **Zero DC Offset (DC Blocker):**
   - Setiap saluran wajib dilengkapi *high-pass filter / DC Blocker* rekursif ($R \approx 0,995$, $f_c \approx 0,05\text{ Hz}$):
     $$y[n] = x[n] - x[n-1] + R \cdot y[n-1]$$
   - Memastikan rata-rata nilai diam (*baseline*) berada tepat di 0 counts tanpa offset statis.

4. **Rentang Amplitudo Latar (Ambient Resting Noise):**
   - Standar deviasi derau ambient dijaga pada kisaran $\pm 100 \dots \pm 250\text{ counts}$.
   - Mencegah tampilan garis helicorder di SWARM bertumpuk (*over-scale*) atau terpotong (*clipping*).

5. **Simulasi Gempa Bumi Triaksial Realistis:**
   - Simulasi gempa otomatis dinonaktifkan secara default (`ENABLE_AUTO_QUAKE false`).
   - Jika dipicu manual:
     - **Fase P-wave ($0 \dots 3,5\text{ s}$):** Gelombang primer berfrekuensi lebih tinggi ($\sim 5,5\text{ Hz}$), dominan pada sumbu vertikal $Z$.
     - **Fase S-wave ($3,5 \dots 9,0\text{ s}$):** Gelombang geser berfrekuensi lebih rendah ($\sim 2\text{ Hz}$), dominan pada sumbu horizontal $N$ dan $E$ dengan amplitudo puncak besar.
     - **Fase Coda & Surface ($9 \dots 25\text{ s}$):** Peluruhan eksponensial halus kembali ke tingkat ambient.

