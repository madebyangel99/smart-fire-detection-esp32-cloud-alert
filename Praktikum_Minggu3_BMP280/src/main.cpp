#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LEBAR_LAYAR        128
#define TINGGI_LAYAR       64
#define ALAMAT_OLED        0x3C
#define PIN_LED_STATUS     2
#define PIN_WAKEUP_TOMBOL  15
#define DURASI_TIDUR_DETIK 10

Adafruit_SSD1306 display(LEBAR_LAYAR, TINGGI_LAYAR, &Wire, -1);
Adafruit_BMP280 bmp;

struct DataSensor {
    float suhu;
    float tekanan;
    float elevasi;
    bool valid;
};

DataSensor dataCuaca = {0, 0, 0, false};

// Variabel simulasi penyimpanan RTC Memory
int nomorSiklusBoot = 0;
float riwayatSuhuTerakhir = 0.0f;
float riwayatTekananTerakhir = 0.0f;

// Status perangkat
bool oledTersedia = false;
bool sensorTersedia = false;

// Penanda tombol dari interrupt
volatile bool tombolDitekan = false;

// Sinkronisasi task FreeRTOS
SemaphoreHandle_t mutexData;
SemaphoreHandle_t taskSelesai;

const char* pemicuSiklus = "POWER-ON";

// ==========================================
// INTERRUPT TOMBOL
// ==========================================

void IRAM_ATTR tombolISR() {
    tombolDitekan = true;
}

// ==========================================
// I2C SCANNER
// ==========================================

void scanI2C() {
    Serial.println("\n[I2C] Memulai pemindaian perangkat...");

    int jumlahPerangkat = 0;

    for (byte alamat = 1; alamat < 127; alamat++) {
        Wire.beginTransmission(alamat);

        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C] Perangkat ditemukan pada alamat 0x%02X\n", alamat);
            jumlahPerangkat++;
        }
    }

    if (jumlahPerangkat == 0) {
        Serial.println("[I2C] Tidak ada perangkat ditemukan.");
    } else {
        Serial.printf("[I2C] Total perangkat ditemukan: %d\n", jumlahPerangkat);
    }
}

// ==========================================
// TAMPILAN OLED
// ==========================================

void perbaruiTampilanOled(
    float t,
    float p,
    float alt,
    int siklus,
    const char* pemicu
) {
    if (!oledTersedia) return;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("UNIB IoT - STASIUN CUACA");
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    display.setCursor(0, 14);
    display.printf("Suhu    : %.1f C", t);

    display.setCursor(0, 26);
    display.printf("Tekanan : %.1f hPa", p);

    display.setCursor(0, 38);
    display.printf("Elevasi : %.1f m", alt);

    display.drawLine(0, 50, 127, 50, SSD1306_WHITE);

    display.setCursor(0, 54);
    display.printf("#%d | %s", siklus, pemicu);

    display.display();
}

// ==========================================
// TASK FREERTOS: OLED DAN JSON
// ==========================================

void TaskTampilanDanSerial(void *pvParameters) {
    DataSensor lokal;

    xSemaphoreTake(mutexData, portMAX_DELAY);
    lokal = dataCuaca;
    xSemaphoreGive(mutexData);

    if (lokal.valid) {
        Serial.println("[FREERTOS] Task tampilan dan JSON berjalan.");

        perbaruiTampilanOled(
            lokal.suhu,
            lokal.tekanan,
            lokal.elevasi,
            nomorSiklusBoot,
            pemicuSiklus
        );

        JsonDocument doc;

        doc["device_id"] = "esp32-weather-st01";
        doc["boot_count"] = nomorSiklusBoot;
        doc["wakeup_cause"] = pemicuSiklus;
        doc["temp_c"] = lokal.suhu;
        doc["pressure_hpa"] = lokal.tekanan;
        doc["altitude_m"] = lokal.elevasi;
        doc["prev_temp_c"] = riwayatSuhuTerakhir;

        serializeJson(doc, Serial);
        Serial.println();

        // Simpan riwayat untuk siklus berikutnya
        riwayatSuhuTerakhir = lokal.suhu;
        riwayatTekananTerakhir = lokal.tekanan;
    } else {
        Serial.println("[ERROR] Data sensor tidak valid.");
    }

    // Beri tahu program utama bahwa task selesai
    xSemaphoreGive(taskSelesai);

    vTaskDelete(NULL);
}

// ==========================================
// PROSES SATU SIKLUS SIMULASI
// ==========================================

void prosesSiklus() {
    nomorSiklusBoot++;

    Serial.println("\n========================================");
    Serial.printf("[RTC] Siklus Simulasi Ke-%d\n", nomorSiklusBoot);
    Serial.printf("[RTC WAKEUP] Pemicu: %s\n", pemicuSiklus);

    Serial.printf(
        "[RTC MEM] Riwayat Siklus Lalu -> Suhu: %.2f C | Tekanan: %.2f hPa\n",
        riwayatSuhuTerakhir,
        riwayatTekananTerakhir
    );

    digitalWrite(PIN_LED_STATUS, HIGH);

    if (sensorTersedia) {
        bmp.takeForcedMeasurement();

        xSemaphoreTake(mutexData, portMAX_DELAY);

        dataCuaca.suhu = bmp.readTemperature();
        dataCuaca.tekanan = bmp.readPressure() / 100.0F;
        dataCuaca.elevasi = bmp.readAltitude(1013.25);
        dataCuaca.valid = true;

        xSemaphoreGive(mutexData);
    } else {
        dataCuaca.valid = false;
    }

    // Jalankan task FreeRTOS pada Core 0
    BaseType_t hasilTask = xTaskCreatePinnedToCore(
        TaskTampilanDanSerial,
        "TaskUI",
        4096,
        NULL,
        1,
        NULL,
        0
    );

    if (hasilTask == pdPASS) {
        // Tunggu sampai task selesai
        xSemaphoreTake(taskSelesai, portMAX_DELAY);
    } else {
        Serial.println("[ERROR] Gagal membuat task FreeRTOS.");
    }

    Serial.printf(
        "[POWER] Menyiapkan simulasi Deep Sleep selama %d detik...\n",
        DURASI_TIDUR_DETIK
    );

    digitalWrite(PIN_LED_STATUS, LOW);

    Serial.println("[POWER] Simulasi jeda dimulai.");

    // Simulasi Deep Sleep menggunakan jeda 10 detik
    delay(DURASI_TIDUR_DETIK * 1000UL);

    Serial.println("[POWER] Simulasi jeda selesai.");
}

// ==========================================
// SETUP
// ==========================================

void setup() {
    Serial.begin(115200);

    pinMode(PIN_LED_STATUS, OUTPUT);
    pinMode(PIN_WAKEUP_TOMBOL, INPUT_PULLUP);

    digitalWrite(PIN_LED_STATUS, LOW);

    Wire.begin(21, 22);
    delay(100);

    // Buat semaphore FreeRTOS
    mutexData = xSemaphoreCreateMutex();
    taskSelesai = xSemaphoreCreateBinary();

    if (mutexData == NULL || taskSelesai == NULL) {
        Serial.println("[ERROR] Gagal membuat semaphore.");
        while (true) {
            delay(1000);
        }
    }

    // Pemindaian perangkat I2C
    scanI2C();

    // Inisialisasi OLED
    if (display.begin(SSD1306_SWITCHCAPVCC, ALAMAT_OLED)) {
        oledTersedia = true;
        Serial.println("[OLED] SSD1306 berhasil dideteksi.");
    } else {
        Serial.println("[OLED] Gagal mendeteksi SSD1306.");
    }

    // Inisialisasi BMP280
    if (bmp.begin(0x76)) {
        sensorTersedia = true;
        Serial.println("[BMP280] Sensor ditemukan pada alamat 0x76.");
    } else if (bmp.begin(0x77)) {
        sensorTersedia = true;
        Serial.println("[BMP280] Sensor ditemukan pada alamat 0x77.");
    } else {
        Serial.println("[BMP280] Sensor tidak ditemukan.");
    }

    if (sensorTersedia) {
        bmp.setSampling(
            Adafruit_BMP280::MODE_FORCED,
            Adafruit_BMP280::SAMPLING_X2,
            Adafruit_BMP280::SAMPLING_X16,
            Adafruit_BMP280::FILTER_X16,
            Adafruit_BMP280::STANDBY_MS_500
        );
    }

    // Aktifkan interrupt tombol
    attachInterrupt(
        digitalPinToInterrupt(PIN_WAKEUP_TOMBOL),
        tombolISR,
        FALLING
    );

    Serial.println("[INTERRUPT] Tombol GPIO15 siap.");
    Serial.println("[SYSTEM] Simulasi stasiun cuaca dimulai.");
}

// ==========================================
// LOOP
// ==========================================

void loop() {
    // Tentukan pemicu untuk siklus berikutnya
    if (nomorSiklusBoot == 0) {
        pemicuSiklus = "POWER-ON";
    } else if (tombolDitekan) {
        tombolDitekan = false;
        pemicuSiklus = "EXT0 SIMULASI";
    } else {
        pemicuSiklus = "TIMER SIMULASI";
    }

    prosesSiklus();
}