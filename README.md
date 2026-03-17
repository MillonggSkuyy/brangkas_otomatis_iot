# Brankas Otomatis Berbasis IoT (brankas_otomatis_iot)

Proyek ini adalah sistem keamanan brankas pintar berbasis Internet of Things (IoT). Sistem ini mengintegrasikan perangkat keras (Arduino & ESP32) dengan antarmuka web untuk monitoring secara real-time.

## 📂 Struktur Folder

- **WebMonitoringBrankas**: Berisi file source code untuk dashboard pemantauan berbasis web (PHP/HTML/JS).
- **arduino_sidang1**: Kode program untuk mikrokontroler Arduino (biasanya menangani sensor dan aktuator fisik).
- **esp32_sidang1**: Kode program untuk ESP32 (menangani konektivitas Wi-Fi, pengiriman data ke cloud/web, atau modul kamera).

## 🚀 Fitur Utama

* **Keamanan Ganda**: Menggunakan kombinasi perangkat keras untuk akses fisik.
* **Monitoring Real-time**: Pantau status brankas (terkunci/terbuka) melalui website.
* **Notifikasi IoT**: Integrasi sistem untuk memberikan peringatan jarak jauh.
* **Log Akses**: Mencatat riwayat penggunaan brankas.

## 🛠️ Komponen yang Digunakan

* **Hardware**:
    * Arduino Uno/Mega
    * ESP32 (Konektivitas Wi-Fi)
    * Solenoid Door Lock
    * Sensor (Keypad / Fingerprint / RFID / Magnetic Switch)
* **Software**:
    * Arduino IDE
    * Web Server (XAMPP/Hosting)
    * Database (MySQL)

## 🔧 Cara Instalasi

1.  **Hardware**: Hubungkan komponen sesuai dengan pin yang didefinisikan di folder `arduino_sidang1` dan `esp32_sidang1`.
2.  **Web**:
    * Upload isi folder `WebMonitoringBrankas` ke server lokal atau hosting.
    * Konfigurasi database pada file koneksi di dalam folder tersebut.
3.  **Firmware**:
    * Buka file `.ino` di folder Arduino dan ESP32 menggunakan Arduino IDE.
    * Sesuaikan konfigurasi Wi-Fi (SSID & Password) pada kode ESP32.
    * Upload kode ke masing-masing perangkat.

## 👤 Author
- **Widya Apriliani Ridwan**
- **Kamil Mahsyar Akbar**

---
*Proyek ini dikembangkan untuk keperluan Tugas Akhir / Sidang.*
