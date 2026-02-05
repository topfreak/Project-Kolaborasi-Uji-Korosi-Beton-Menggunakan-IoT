# SPRCP IoT Monitoring System (Smart Patch Repair Cathodic Protection)

![Version](https://img.shields.io/badge/Version-v24.4_Stable-blue)
![Platform](https://img.shields.io/badge/Platform-ESP32_WROOM-green)
![Dashboard](https://img.shields.io/badge/Web_Interface-v37.3-orange)
![License](https://img.shields.io/badge/License-Academic_Research-lightgrey)

## 📖 Executive Summary

**SPRCP Monitoring System** adalah platform IoT terintegrasi yang dirancang untuk evaluasi korosi pada struktur beton bertulang. Sistem ini dikembangkan untuk mendukung penelitian metode perbaikan *Smart Patch Repair* dengan dua mekanisme proteksi: *Sacrificial Anode Cathodic Protection* (SACP) dan *Hybrid Cathodic Protection* (HCP).

Sistem ini mengadopsi arsitektur **High-Fidelity Data Acquisition**, di mana integritas data diutamakan mulai dari pembacaan sensor di level *hardware* hingga visualisasi di *dashboard*.

---

## 🏗️ System Architecture

Sistem terdiri dari dua entitas utama yang terhubung melalui Google Firebase Realtime Database:

1.  **Firmware Node (v24.4):** Mengelola akuisisi data fisik, switching relay, dan sinkronisasi waktu.
2.  **Master Hub Dashboard (v37.3):** Antarmuka pusat untuk monitoring, kalibrasi instrumen, dan pelaporan data.

### 1. Firmware Specification (v24.4)
Firmware ini berjalan pada mikrokontroler ESP32 dengan fokus pada stabilitas pengukuran sinyal analog presisi.

* **Precision ADC Engine:**
    Menggunakan modul ADS1115 (16-bit) dengan konfigurasi *Differential Input*. Algoritma sampling menggunakan metode `Oversampling` (128 samples/window) untuk mereduksi *noise* elektronik dan mendapatkan nilai *DC Potential* yang stabil.
* **32-Channel Multiplexing Matrix:**
    Sistem mampu memonitor 8 Benda Uji (M1-MK17) dengan masing-masing 2 Tulangan (T1, T2) secara sekuensial menggunakan *logic switching* CD74HC4067 ganda.
* **Nano-Current Sensing:**
    Monitoring arus anoda menggunakan topologi *High-Side Sensing* via INA219, memungkinkan deteksi arus korosi dalam orde mikro-ampere (µA) hingga mili-ampere (mA).
* **Measurement Protocols:**
    * **Phase 1 (Instant-Off):** Algoritma pemutusan arus cepat (<150ms latency) untuk mengukur *True Polarized Potential* sesuai standar NACE.
    * **Phase 2 (Rest):** Monitoring fase depolarisasi beton.
    * **HCP Natural:** Mode pemantauan pasif untuk sistem Hybrid.
* **Fail-Safe Storage:**
    Menggunakan partisi NVS (*Non-Volatile Storage*) untuk menyimpan *state* terakhir, memungkinkan sistem melakukan *Auto-Resume* setelah gangguan daya.

### 2. Dashboard Analytics (v37.3)
Aplikasi berbasis web (Single-Page Application) yang berfungsi sebagai pusat kendali riset.

* **Real-time Visualization:** Grafik interaktif berbasis *ApexCharts* yang menampilkan tren Potensial (CSE) dan Arus (mA) secara *live*.
* **Instrumentation Calibration:** Fitur untuk memasukkan koefisien kalibrasi linear ($y = ax^2 + bx + c$) untuk setiap kanal sensor, memastikan data yang tampil sesuai dengan standar alat ukur referensi.
* **Automated Reporting:** Modul ekspor data otomatis ke format Excel (.xlsx) dengan struktur laporan yang telah disesuaikan per Minggu dan per Fase (HCP/SACP).

---

## 🛠️ Hardware Pinout Configuration

Sistem menggunakan **ESP32 DOIT DEVKIT V1** sebagai unit pemroses utama. Berikut adalah pemetaan I/O (*Input/Output*) yang digunakan dalam Firmware v24.4:

### I2C Bus & Sensors
| Component | Pin (SDA) | Pin (SCL) | Address |
| :--- | :--- | :--- | :--- |
| **Main Bus** | GPIO 21 | GPIO 22 | - |
| **ADS1115 (ADC)** | - | - | `0x48` |
| **INA219 (Current)** | - | - | `0x40` |
| **SSD1306 (OLED)** | - | - | `0x3C` |

### Control & Actuators
| Function | GPIO Pins | Description |
| :--- | :--- | :--- |
| **Relay Actuators** | 13, 12, 14, 27, 26, 25, 33, 32 | Active Low Control for M1-MK17 |
| **Multiplexer S0-S3** | 23, 19, 18, 5 | Channel Selection Lines |
| **Mux Enable (EN)** | 17, 16 | Enable Line for Mux 1 & Mux 2 |
| **Temp Sensor** | 15 | DS18B20 OneWire Bus |

### User Interface
| Button | GPIO Pin | Type |
| :--- | :--- | :--- |
| **OK / Enter** | 35 | Input Pull-up |
| **UP** | 34 | Input Pull-up |
| **DOWN** | 39 | Input Pull-up |
| **BACK** | 36 | Input Pull-up |

---

## 💻 Installation & Deployment Guide

Bagian ini menjelaskan langkah teknis untuk melakukan *deployment* kode ke perangkat keras dan menjalankan dashboard.

### A. Firmware Deployment (ESP32)

**Prerequisites:**
1.  **Arduino IDE** versi 2.0 ke atas.
2.  Driver USB-to-TTL (CP210x atau CH340) terinstall.

**Required Libraries (Install via Library Manager):**
* `Firebase_ESP_Client` by Mobizt
* `Adafruit ADS1X15`
* `Adafruit INA219`
* `Adafruit SSD1306` & `Adafruit GFX`
* `DallasTemperature` & `OneWire`

**Steps:**
1.  Buka file `firmware/v24.4_stable.ino`.
2.  Konfigurasi kredensial jaringan dan database pada bagian *macros*:
    ```cpp
    #define WIFI_SSID "YOUR_WIFI_SSID"
    #define WIFI_PASSWORD "YOUR_WIFI_PASS"
    #define API_KEY "YOUR_FIREBASE_API_KEY"
    #define DATABASE_URL "YOUR_RTDB_URL"
    ```
3.  Pilih Board: **DOIT ESP32 DEVKIT V1**.
4.  Atur Upload Speed ke **921600** untuk mempercepat proses flash.
5.  Klik **Upload**.

### B. Dashboard Deployment (Web Interface)

Dashboard v37.3 dibangun menggunakan teknologi *Client-Side Rendering*, sehingga tidak memerlukan *backend server* khusus (Node.js/PHP).

**Local Deployment:**
1.  Pastikan folder `dashboard` berisi file `v37.3.html`.
2.  Cukup buka file tersebut menggunakan browser modern (Google Chrome, Edge, atau Firefox).
3.  *Note:* Pastikan komputer terhubung ke internet untuk memuat library CDN (Tailwind, ApexCharts, Firebase SDK).

**Cloud Deployment (Optional):**
* File `v37.3.html` dapat di-hosting menggunakan layanan statis seperti **GitHub Pages**, **Netlify**, atau **Firebase Hosting** untuk akses publik.

---

## 📂 Repository Structure

```text
SPRCP-Project/
├── firmware/
│   ├── v24.4_stable.ino      # Main Firmware Source Code
│   └── libraries/            # (Optional) Library dependencies list
├── dashboard/
│   ├── v37.3.html            # Master Hub Dashboard File
│   └── assets/               # Icons/Images assets
├── docs/
│   ├── pinout_diagram.png    # Hardware wiring diagram
│   └── calibration_guide.pdf # Sensor calibration manual
└── README.md                 # Project Documentation

---

## 👨‍💻 Project Maintainer

**Taufiq Hidayatullah**
* *Role:* Lead Research Developer
* *Project:* Smart Patch Repair Cathodic Protection (SPRCP)
* *Institution:* Universitas Amikom Yogyakarta

> Copyright © 2026 SPRCP Research Group. This software is provided for academic research purposes.
