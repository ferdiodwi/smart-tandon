# 💧 Smart Tandon - Sistem Kontrol & Monitoring Tandon Air

![GitHub stars](https://img.shields.io/github/stars/YOUR_USERNAME/YOUR_REPOSITORY?style=social) ![GitHub forks](https://img.shields.io/github/forks/YOUR_USERNAME/YOUR_REPOSITORY?style=social)

**Smart Tandon** adalah proyek berbasis Arduino dan antarmuka web untuk memonitor level air dan mengontrol pompa secara otomatis. Lupakan kekhawatiran tandon kosong atau air meluap, sistem ini akan menanganinya untuk Anda!

---

## ✨ Fitur Utama

* 📊 **Monitoring Real-time**: Pantau ketinggian air, persentase, dan status pompa langsung dari dashboard web.
* 🤖 **Kontrol Pompa Otomatis**: Pompa akan menyala saat air mencapai level kritis dan mati saat penuh secara otomatis.
* 🕹️ **Mode Manual**: Ambil alih kendali kapan saja! Nyalakan atau matikan pompa secara manual melalui dashboard.
* 📈 **Riwayat Data**: Lihat riwayat ketinggian air dan status sistem untuk analisis.
* 💡 **Indikator Visual**: Tampilan tandon air yang dinamis berubah warna sesuai status (Normal, Kritis, Penuh).
* 🔌 **Status Koneksi**: Indikator untuk memantau koneksi ke Arduino dan komponen sistem lainnya.

---

## 🛠️ Teknologi yang Digunakan

Proyek ini dibangun menggunakan kombinasi hardware dan software modern.

### Hardware
* 🧠 **Arduino Uno/Nano**: Otak dari sistem kontrol.
* 🔊 **Sensor Ultrasonik HC-SR04**: Untuk mengukur jarak ke permukaan air tanpa sentuhan.
* ⚡ **Modul Relay**: Sebagai saklar elektronik untuk menghidupkan/mematikan pompa air.
* 🖥️ **OLED Display SSD1306**: Untuk menampilkan status langsung di perangkat.

### Software & Antarmuka
* 🌐 **HTML, CSS, JavaScript**: Untuk membangun dashboard yang interaktif dan responsif.
* ⚡ **Node.js / Python (Backend)**: Diperlukan server backend sederhana untuk menjembatani komunikasi antara Arduino (Serial Port) dan dashboard web (HTTP/WebSocket). *Catatan: Kode backend tidak termasuk dalam repo ini.*
