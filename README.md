# Brankas Otomatis Berbasis IoT (brankas_otomatis_iot)

Sistem keamanan brankas pintar berbasis Internet of Things (IoT) yang mengintegrasikan kontrol perangkat keras dengan notifikasi real-time via Telegram dan monitoring berbasis web.

## 📂 Struktur Folder

- **WebMonitoringBrankas**: Source code dashboard pemantauan (PHP/MySQL).
- **arduino_sidang1**: Kode program mikrokontroler untuk kontrol sensor/aktuator fisik.
- **esp32_sidang1**: Kode program ESP32 untuk konektivitas Wi-Fi dan komunikasi Bot Telegram.

## 🚀 Fitur Utama

* **Keamanan Ganda**: Kontrol akses fisik yang terintegrasi.
* **Monitoring Web**: Pantau status brankas secara real-time dari browser.
* **Notifikasi Bot Telegram**: Sistem akan mengirimkan pesan instan ke perangkat pengguna jika:
    * Brankas berhasil dibuka/dikunci.
    * Terdeteksi percobaan akses ilegal atau paksa.
* **Log Sistem**: Riwayat aktivitas tercatat secara otomatis di database.

## 🛠️ Komponen yang Digunakan

* **Hardware**: Arduino (Uno/Mega), ESP32 (Wi-Fi & Telegram Module), Solenoid Door Lock, Sensor Keypad/RFID.
* **Software**: Arduino IDE, Web Server (XAMPP/Hosting), MySQL Database.
* **Platform**: Telegram Bot API.

## 🔧 Konfigurasi Bot Telegram

Untuk mengaktifkan fitur notifikasi, pastikan kamu telah mengatur hal berikut di dalam kode `esp32_sidang1`:

1.  Dapatkan **Bot Token** dari [@BotFather](https://t.me/botfather).
2.  Dapatkan **Chat ID** akun Telegram kamu (bisa melalui @IDBot).
3.  Masukkan Token dan Chat ID tersebut ke dalam variabel yang tersedia di file `.ino`:
    ```cpp
    #define BOTtoken "XXXXXXXXX:XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
    #define CHAT_ID "XXXXXXXXX"
    ```

## 💻 Cara Instalasi

1.  **Hardware**: Rakit komponen sesuai skema yang ditentukan di folder kode.
2.  **Database**: Import file SQL ke phpMyAdmin dan sesuaikan konfigurasi di `WebMonitoringBrankas`.
3.  **Firmware**:
    * Buka file di folder `arduino_sidang1` dan `esp32_sidang1` via Arduino IDE.
    * Masukkan **SSID** dan **Password** Wi-Fi pada kode ESP32.
    * Upload kode ke masing-masing mikrokontroler.

## 👤 Author
- **Widya Apriliani Ridwan**
- **Kamil Mahsyar Akbar**

---
*Proyek ini dikembangkan untuk keperluan Tugas Akhir / Sidang.*
