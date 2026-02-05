# SPRCP Research System (Smart Patch Repair Cathodic Protection)

![Project Status](https://img.shields.io/badge/Status-Research_Final-success)
![Firmware](https://img.shields.io/badge/Firmware-v24.4_Raw-blue)
![Dashboard](https://img.shields.io/badge/Dashboard-v37.3_Master-green)
![Backend](https://img.shields.io/badge/Database-Firebase_RTDB-orange)

## 📖 Deskripsi Proyek

**SPRCP Research System** adalah solusi *end-to-end* berbasis IoT untuk memantau efektivitas metode perbaikan beton *Smart Patch Repair* menggunakan proteksi katodik. Sistem ini terdiri dari dua komponen utama yang saling terintegrasi:

1.  **Firmware (ESP32):** Bertugas melakukan akuisisi data sensor presisi tinggi dan kontrol perangkat keras.
2.  **Master Hub Dashboard:** Antarmuka web komprehensif untuk visualisasi data, analisis tren korosi, dan manajemen konfigurasi riset.

---

## 🛠️ Komponen 1: Firmware v24.4 (Raw Data Acquisition)

Firmware v24.4 didesain sebagai fondasi pengambilan data yang **jujur dan presisi**. Versi ini fokus pada integritas data mentah (*Raw Data Integrity*) tanpa manipulasi algoritma di sisi *edge device*.

### Fitur Utama Firmware:
* **32-Channel Multiplexing:** Mengontrol matriks pengukuran untuk 8 Benda Uji (M1-MK17) dengan masing-masing 2 Tulangan (T1, T2) secara bergantian.
* **Direct ADC Sampling:** Menggunakan sensor **ADS1115 (16-bit)** untuk membaca potensial korosi dengan resolusi tinggi, meminimalkan *noise* melalui teknik *oversampling*.
* **Multi-Mode Sensing:**
    * *Phase 1 (Instant Off):* Pengukuran potensial *ON* dan *OFF* dengan timing relay presisi (<150ms).
    * *Phase 2 (Rest):* Monitoring fase depolarisasi/istirahat beton.
    * *HCP Natural:* Pengukuran potensial alami pada sistem *Hybrid*.
* **Fail-Safe Mechanism:** Dilengkapi dengan fitur **Anti-Reset** berbasis NVS (*Non-Volatile Storage*) untuk menyimpan status terakhir jika terjadi gangguan daya, serta sinkronisasi waktu otomatis via NTP.
* **Real-Time Sync:** Pengiriman data latensi rendah ke Google Firebase RTDB.

---

## 💻 Komponen 2: Master Hub Dashboard v37.3

Dashboard v37.3 adalah pusat kendali riset yang dibangun sebagai aplikasi *Single-Page* (SPA) responsif. Berdasarkan analisis kode sumber, dashboard ini memiliki kapabilitas analitik tingkat lanjut.

### Fitur Utama Dashboard:

#### 1. 📊 Visualisasi Data Real-Time & Historis
* **Multi-Tab Interface:** Navigasi cepat antara *Monitoring*, *Data Logs*, *Analysis*, dan *Configuration*.
* **Interactive Charts:** Menggunakan **ApexCharts** untuk menampilkan grafik tren potensial (CSE) yang dinamis.
    * *Trend Analysis:* Membandingkan data antar-tulangan dan antar-metode (HCP vs SACP).
    * *Current Density:* Visualisasi rapat arus (mA/m²) untuk analisis efisiensi anoda.
* **Live Status Monitoring:** Menampilkan status koneksi alat, mode operasi yang sedang aktif, dan waktu sinkronisasi terakhir secara *real-time*.

#### 2. 🎛️ Manajemen Konfigurasi Riset (Remote Config)
* **Parameter Tuning:** Pengguna dapat mengubah interval pengukuran arus, luas permukaan tulangan, dan tanggal mulai riset langsung dari web tanpa memprogram ulang alat.
* **Calibration Control:** Antarmuka khusus untuk memasukkan nilai kalibrasi (Koefisien A, B, C) untuk ke-32 kanal sensor guna menjamin akurasi pembacaan.

#### 3. 📂 Manajemen Data & Pelaporan
* **Smart Export Engine:** Fitur ekspor data otomatis ke format **Excel (.xlsx)** menggunakan library *SheetJS*. Laporan terstruktur rapi per-benda uji dan per-minggu.
* **Completion Mapping:** Tabel visual yang menunjukkan kelengkapan data harian (HCP/PH1) untuk memastikan tidak ada data yang terlewat selama periode riset.
* **Log Table:** Tabel data mentah yang dapat di-filter dan diurutkan untuk audit data manual.

---

## ⚙️ Spesifikasi Teknis Sistem

| Komponen | Spesifikasi |
| :--- | :--- |
| **Microcontroller** | ESP32 DOIT DEVKIT V1 |
| **Sensor ADC** | ADS1115 (16-Bit Precision) |
| **Sensor Arus** | INA219 (High-Side DC Current) |
| **Frontend** | HTML5, Tailwind CSS, JavaScript (ES6) |
| **Charting Lib** | ApexCharts.js |
| **Backend** | Google Firebase Realtime Database |

---

## 🚀 Cara Instalasi & Penggunaan

### A. Firmware (ESP32)
1.  Buka file `v24.4.ino` dengan Arduino IDE.
2.  Install library yang dibutuhkan: `Firebase_ESP_Client`, `Adafruit_ADS1X15`, `Adafruit_SSD1306`.
3.  Sesuaikan kredensial WiFi dan API Key pada bagian `#define`.
4.  Upload ke board ESP32.

### B. Dashboard (Web)
1.  Buka file `v37.3.html` dapat langsung dibuka menggunakan browser modern (Chrome/Edge/Firefox).
2.  Pastikan koneksi internet aktif agar dashboard dapat mengambil data dari server Firebase.

---

## 👨‍💻 Tim Pengembang

**Taufiq Hidayatullah**
* *Role:* Lead Research Developer
* *Project:* Smart Patch Repair Cathodic Protection (SPRCP)
* *Institution:* Universitas Amikom Yogyakarta

---

> *Sistem ini dikembangkan untuk tujuan penelitian akademis guna mendukung analisis korosi beton yang akurat, transparan, dan efisien.*