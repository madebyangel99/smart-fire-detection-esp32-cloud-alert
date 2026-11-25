# ESP32 Praktikum Minggu 3 — BMP280 Custom Chip

Proyek ini mempertahankan `src/main.cpp`, `platformio.ini`, OLED SSD1306, tombol, LED, dan alur Deep Sleep dari proyek praktikum. Komponen BMP280 bawaan diganti dengan custom chip GY-63 yang berasal dari contoh proyek Wokwi yang diunduh.

## Penting: kompilasi custom chip satu kali

Folder `chips/` berisi:
- `gy-63.chip.json`: definisi pin dan kontrol sensor.
- `gy-63.chip.c`: kode model sensor BMP280.

Wokwi untuk VS Code memerlukan file hasil kompilasi `gy-63.chip.wasm`. File WASM tidak disertakan di ZIP ini, jadi buat dengan Wokwi CLI terlebih dahulu:

1. Buka folder proyek ini di VS Code.
2. Buka Terminal > New Terminal.
3. Pastikan terminal berada di folder yang sama dengan `platformio.ini`.
4. Jalankan:

   `wokwi-cli chip compile chips/gy-63.chip.c -o chips/gy-63.chip.wasm`

Wokwi CLI akan mengambil toolchain dan header Chips API yang diperlukan, lalu membuat file WASM. Dokumentasi: https://docs.wokwi.com/guides/custom-chips-to-wasm

Setelah perintah berhasil, jalankan Build PlatformIO, lalu Start Simulation Wokwi.

## Pengaturan alamat I2C

Pada rangkaian ini pin `CSB` dihubungkan ke GND sehingga custom chip memilih alamat I2C `0x77`. Program `main.cpp` sudah mencoba alamat `0x76` dan `0x77`.

SDA BMP280 dan OLED terhubung ke GPIO21, sedangkan SCL keduanya ke GPIO22.

## Catatan
- Jangan menghapus `[[chip]]` pada `wokwi.toml`.
- Jangan mengganti `chip-gy-63` di `diagram.json` menjadi `wokwi-bmp280`.
- Jika perintah `wokwi-cli` tidak dikenali, instal Wokwi CLI sesuai petunjuk resmi: https://docs.wokwi.com/wokwi-cli/getting-started
- Ini adalah model simulasi, bukan pengganti pengujian dengan perangkat keras BMP280 fisik.
