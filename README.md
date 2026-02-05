# SPRCP IoT Monitoring System (Smart Patch Repair Cathodic Protection)

![Version](https://img.shields.io/badge/Version-v24.4_Stable-blue)
![Platform](https://img.shields.io/badge/Platform-ESP32_SoC-green)
![Architecture](https://img.shields.io/badge/Architecture-High_Fidelity_DAQ-red)
![License](https://img.shields.io/badge/License-Academic_Research-lightgrey)

## 📖 Executive Summary

**SPRCP (Smart Patch Repair Cathodic Protection) Monitoring System** adalah platform instrumentasi IoT terpadu yang dirancang untuk evaluasi korosi pada struktur beton bertulang. Sistem ini dikembangkan untuk mendukung validasi metode perbaikan *Smart Patch* yang mengintegrasikan dua mekanisme proteksi elektrokimia: *Sacrificial Anode Cathodic Protection* (SACP) dan *Hybrid Cathodic Protection* (HCP).

Repository ini memuat implementasi sistem *end-to-end*, mencakup *Embedded Firmware* untuk akuisisi data presisi tinggi dan *Master Dashboard* untuk analisis visual real-time. Sistem dirancang dengan filosofi **"Direct Precision Sampling"**, memastikan setiap data yang direkam memiliki integritas tinggi sesuai standar pengukuran NACE/ASTM.

---

## 📂 Repository Structure

Proyek ini menggunakan struktur *flat-directory* untuk kemudahan deployment dan manajemen aset:

```text
SPRCP-Project/
├── v24.4.ino       # Firmware Utama (ESP32 Source Code) - Data Acquisition Node
├── v37.3.html      # Master Hub Dashboard (Single-File Web Application)
└── README.md       # Dokumentasi Teknis Sistem

```

---

## 🛠️ System Components

### 1. Firmware Node (v24.4)

Firmware v24.4 adalah otak dari sistem akuisisi data yang berjalan pada mikrokontroler ESP32. Versi ini difokuskan pada stabilitas sinyal analog dan akurasi *timing*.

* **Core Architecture:** Menggunakan pendekatan **Direct ADC Acquisition**. Sistem membaca sinyal sensor secara langsung tanpa pemrosesan artifisial, menjamin data yang dikirim ke cloud adalah representasi faktual dari kondisi fisik beton.
* **Precision Sensing Engine:**
* **Voltage:** Implementasi sensor ADS1115 (16-bit) dengan algoritma *Oversampling* (128 samples/window) untuk mereduksi *white noise* dan mendapatkan *DC Potential* yang stabil.
* **Current:** Monitoring arus anoda menggunakan topologi *High-Side Sensing* (INA219) dengan resolusi mikro-ampere ().


* **32-Channel Multiplexing:** Mengontrol matriks pengukuran untuk 8 Benda Uji (M1-MK17) x 2 Tulangan dengan total 32 titik ukur independen.
* **Standardized Calibration:** Mendukung kompensasi toleransi perangkat keras menggunakan fungsi kalibrasi linear () untuk setiap kanal.
* **Measurement Protocols:**
* *Phase 1 (Instant-Off):* Algoritma interupsi arus presisi (<150ms latency).
* *Phase 2 (Rest):* Monitoring fase depolarisasi.
* *HCP Natural:* Mode pemantauan pasif sistem Hybrid.



### 2. Master Hub Dashboard (v37.3)

Antarmuka pusat kendali riset berbasis web (*Client-Side Application*).

* **Real-time Analytics:** Visualisasi data Potensial (CSE) dan Arus (mA) menggunakan grafik dinamis *ApexCharts*.
* **Remote Configuration:** Pengaturan parameter riset (Interval, Luas Permukaan, Tanggal Mulai) dilakukan secara nirkabel (OTA Config).
* **Automated Reporting:** Modul ekspor data otomatis ke format Excel (.xlsx) dengan struktur laporan mingguan yang terstandarisasi untuk analisis lanjutan.

---

## ⚙️ Hardware Configuration

Sistem dibangun di atas platform **ESP32 DOIT DEVKIT V1** dengan konfigurasi periferal sebagai berikut:

### Pinout Map (v24.4)

| Interface | GPIO Pin | Function |
| --- | --- | --- |
| **I2C Bus** | 21 (SDA), 22 (SCL) | Komunikasi Sensor (ADS1115, INA219) & OLED |
| **Relay Control** | 13, 12, 14, 27, 26, 25, 33, 32 | Aktuator Anoda (M1 s.d. MK17) |
| **Multiplexer Select** | 23 (S0), 19 (S1), 18 (S2), 5 (S3) | Selektor Kanal Analog |
| **Multiplexer Enable** | 17, 16 | Enable Chip Mux 1 & 2 |
| **User Input** | 35, 34, 39, 36 | Tombol Navigasi (OK, UP, DOWN, BACK) |
| **Temp Sensor** | 15 | DS18B20 OneWire Bus |

### Sensor Addressing

* **ADS1115 (ADC):** `0x48`
* **INA219 (Current):** `0x40`
* **SSD1306 (OLED):** `0x3C`

---

## 🔒 Security & Configuration Guide

Demi keamanan data riset, kredensial koneksi database tidak disertakan dalam repository ini (Sanitized). Pengguna wajib melakukan konfigurasi mandiri agar sistem dapat terhubung ke Google Firebase.

### A. Firmware Configuration (ESP32)

1. Buka file `v24.4.ino` di Arduino IDE.
2. Cari blok definisi **Network Credentials** di bagian atas kode.
3. Isi dengan parameter berikut:
```cpp
#define WIFI_SSID       "NAMA_WIFI_ANDA"
#define WIFI_PASSWORD   "PASSWORD_WIFI_ANDA"
#define API_KEY         "FIREBASE_API_KEY_ANDA"
#define DATABASE_URL    "[https://PROJECT-ID.firebaseio.com/](https://PROJECT-ID.firebaseio.com/)"

```



### B. Dashboard Configuration (Web Interface)

File `v37.3.html` memerlukan objek konfigurasi Firebase SDK agar dapat merender data.

1. Buka file `v37.3.html` menggunakan Text Editor (VS Code / Notepad).
2. Temukan bagian script konfigurasi di dalam tag `<head>` atau di awal `<body>`.
3. Isi objek `firebaseConfig` dengan format standar berikut (sesuai yang didapat dari Firebase Console):
```javascript
const firebaseConfig = {
    apiKey: "API_KEY_DARI_FIREBASE_CONSOLE",
    authDomain: "PROJECT_ID.firebaseapp.com",
    databaseURL: "https://PROJECT_ID.firebaseio.com",
    projectId: "PROJECT_ID",
    storageBucket: "PROJECT_ID.appspot.com",
    messagingSenderId: "SENDER_ID",
    appId: "APP_ID"
};

```


4. Simpan file, lalu buka kembali menggunakan browser.

---

## 💻 Installation & Usage Guide

### Firmware Deployment

1. **Environment:** Pastikan **Arduino IDE** (v2.0+) terinstall.
2. **Dependencies:** Install library berikut via Library Manager:
* `Firebase_ESP_Client` (by Mobizt)
* `Adafruit ADS1X15`
* `Adafruit INA219`
* `Adafruit SSD1306` & `Adafruit GFX`
* `DallasTemperature` & `OneWire`


3. **Flashing:** Pilih Board **DOIT ESP32 DEVKIT V1**, atur Upload Speed ke **921600**, dan klik Upload.

### Dashboard Deployment

Dashboard v37.3 adalah aplikasi *Serverless* yang berjalan di sisi klien (Client-Side).

1. Pastikan file `v37.3.html` berada di folder proyek.
2. Double-click file untuk menjalankannya di browser modern (Chrome/Edge/Firefox).
3. **Requirement:** Koneksi internet diperlukan untuk memuat library CDN (ApexCharts, Tailwind) dan koneksi WebSocket ke database.

---

## 📊 Scientific Workflow

Sistem ini mengikuti alur kerja akuisisi data standar instrumentasi:

1. **System Handshake:** Sinkronisasi waktu (NTP) dan pengambilan profil kalibrasi dari Cloud.
2. **Signal Acquisition:**
* Multiplexer mengarahkan jalur pengukuran ke elektroda target.
* ADC mengambil sampel data mentah (*Raw Differential Voltage*).
* Filter digital menerapkan *averaging* untuk stabilitas sinyal.


3. **Signal Conditioning:** Data mentah dikonversi menjadi nilai teknik menggunakan koefisien kalibrasi hardware () untuk mengeliminasi toleransi komponen pasif.
4. **Telemetry:** Data valid dikirim ke Firebase Realtime Database dengan *timestamp* presisi untuk visualisasi di Dashboard.

---

## 👨‍💻 Project Maintainer

**Taufiq Hidayatullah**

* **Role:** Lead Research Developer
* **Focus:** IoT Instrumentation & Structural Health Monitoring
* **Institution:** Universitas Amikom Yogyakarta

---

*Copyright © 2026 SPRCP Research Group. This software is provided for academic research purposes.*

```
