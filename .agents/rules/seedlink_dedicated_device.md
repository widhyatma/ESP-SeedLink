# Aturan III: Dedicated SeedLink TCP/IP Server (No Extra Bloat)

## Deskripsi & Tujuan
Menjaga kemurnian arsitektur mikrokontroler seismometer sebagai instrumen akuisisi dan server protokol SeedLink (Port 18000) terdedikasi sesuai standar FDSN tanpa beban komputasi/jaringan tambahan.

## Prinsip Perancangan

1. **Fokus Eksklusif pada SeedLink TCP Server:**
   - ESP32 berfungsi murni sebagai server TCP SeedLink v3.1 (default port 18000) yang melayani akuisisi data seismik stasioner.
   - Semua soket LwIP dialokasikan untuk klien seismologi (SWARM, ObsPy, SeisComP, Ringserver) dengan batas maksimum hingga 16 koneksi simultan.

2. **Larangan Fitur Tambahan (No Bloatware):**
   - **DILARANG** menambahkan Web Server lokal (HTTP/HTML UI), MQTT, Bluetooth/BLE, WebSocket non-seismik, REST API eksternal, atau integrasi cloud sekunder ke dalam mikrokontroler.
   - Larangan ini krusial demi:
     - Mencegah kehabisan RAM dan fragmentasi heap memori mikrokontroler.
     - Menjaga integritas buffer soket TCP LwIP.
     - Mencegah gangguan latensi terhadap timer presisi sampling sensor seismik ($10\text{ ms} / 20\text{ ms}$).

3. **Penyaluran Paket MiniSEED yang Stabil:**
   - Penulisan paket 520-byte antar saluran menggunakan *micro-delay staggering* (2 ms) untuk mencegah kejenuhan send buffer antarmuka Wi-Fi.
   - Penanganan deteksi pemutusan soket (*dead/half-open socket*) secara aktif dengan pembersihan slot klien seketika.

