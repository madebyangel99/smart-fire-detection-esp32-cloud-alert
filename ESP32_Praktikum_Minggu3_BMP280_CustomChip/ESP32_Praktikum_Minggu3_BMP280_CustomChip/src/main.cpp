#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

#define LEBAR_LAYAR        128
#define TINGGI_LAYAR       64
#define ALAMAT_OLED        0x3C
#define PIN_LED_STATUS     2
#define PIN_WAKEUP_TOMBOL  GPIO_NUM_15
#define DURASI_TIDUR_DETIK 10

Adafruit_SSD1306 display(LEBAR_LAYAR, TINGGI_LAYAR, &Wire, -1);
Adafruit_BMP280 bmp;

// Variabel persisten di RTC slow memory (terjaga selama Deep Sleep)
RTC_DATA_ATTR int nomorSiklusBoot = 0;
RTC_DATA_ATTR float riwayatSuhuTerakhir = 0.0f;
RTC_DATA_ATTR float riwayatTekananTerakhir = 0.0f;

struct DataSensor {
    float suhu;
    float tekanan;
    float elevasi;
    bool valid;
} dataCuaca;

SemaphoreHandle_t mutexData;

const char* labelPemicuWakeup() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return "TIMER";
        case ESP_SLEEP_WAKEUP_EXT0:  return "EXT0 BTN";
        default:                     return "POWER-ON";
    }
}

void cetakDiagnostikWakeup() {
    Serial.printf("\n========================================\n");
    Serial.printf("[RTC] Siklus Bangun Ke-%d\n", nomorSiklusBoot);
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("[RTC WAKEUP] Pemicu: Pewaktu Internal (RTC Timer)");
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("[RTC WAKEUP] Pemicu: Tombol Darurat Luar (EXT0 pada GPIO 15)");
            break;
        default:
            Serial.println("[RTC WAKEUP] Pemicu: Power-on Reset / Boot Pertama");
            break;
    }
    Serial.printf("[RTC MEM] Riwayat Siklus Lalu -> Suhu: %.2f C | Tekanan: %.2f hPa\n",
                  riwayatSuhuTerakhir, riwayatTekananTerakhir);
}

void perbaruiTampilanOled(float t, float p, float alt, int siklus, const char* pemicu) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.printf("UNIB IoT - STASIUN CUACA");
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

// Task pada Core 0: UI OLED dan serialisasi JSON
void TaskTampilanDanSerial(void *pvParameters) {
    xSemaphoreTake(mutexData, portMAX_DELAY);
    DataSensor lokal = dataCuaca;
    xSemaphoreGive(mutexData);

    if (lokal.valid) {
        const char* labelPemicu = labelPemicuWakeup();

        perbaruiTampilanOled(lokal.suhu, lokal.tekanan, lokal.elevasi, nomorSiklusBoot, labelPemicu);

        JsonDocument doc;
        doc["device_id"]    = "esp32-weather-st01";
        doc["boot_count"]   = nomorSiklusBoot;
        doc["wakeup_cause"] = labelPemicu;
        doc["temp_c"]       = serialized(String(lokal.suhu, 2));
        doc["pressure_hpa"] = serialized(String(lokal.tekanan, 2));
        doc["altitude_m"]   = serialized(String(lokal.elevasi, 1));
        doc["prev_temp_c"]  = serialized(String(riwayatSuhuTerakhir, 2));
        serializeJson(doc, Serial);
        Serial.println();

        riwayatSuhuTerakhir = lokal.suhu;
        riwayatTekananTerakhir = lokal.tekanan;
    }
    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_LED_STATUS, OUTPUT);
    pinMode(PIN_WAKEUP_TOMBOL, INPUT_PULLUP);

    digitalWrite(PIN_LED_STATUS, HIGH); // LED menyala: fase aktif
    nomorSiklusBoot++;
    cetakDiagnostikWakeup();

    mutexData = xSemaphoreCreateMutex();
    Wire.begin(21, 22);
    delay(100);

    if (!display.begin(SSD1306_SWITCHCAPVCC, ALAMAT_OLED)) {
        Serial.println("Gagal menemukan layar OLED SSD1306!");
    }

    dataCuaca.valid = false;
    if (!bmp.begin(0x76) && !bmp.begin(0x77)) {
        Serial.println("Gagal mendeteksi sensor BMP/BME280 pada alamat 0x76/0x77!");
    } else {
        bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                        Adafruit_BMP280::SAMPLING_X2,
                        Adafruit_BMP280::SAMPLING_X16,
                        Adafruit_BMP280::FILTER_X16,
                        Adafruit_BMP280::STANDBY_MS_500);
        bmp.takeForcedMeasurement();

        xSemaphoreTake(mutexData, portMAX_DELAY);
        dataCuaca.suhu    = bmp.readTemperature();
        dataCuaca.tekanan = bmp.readPressure() / 100.0F;
        dataCuaca.elevasi = bmp.readAltitude(1013.25);
        dataCuaca.valid   = true;
        xSemaphoreGive(mutexData);
    }

    xTaskCreatePinnedToCore(TaskTampilanDanSerial, "TaskUI", 4096, NULL, 1, NULL, 0);
    delay(500);

    Serial.printf("[POWER] Menyiapkan Deep Sleep selama %d detik...\n", DURASI_TIDUR_DETIK);
    esp_sleep_enable_timer_wakeup((uint64_t)DURASI_TIDUR_DETIK * 1000000ULL);
    esp_sleep_enable_ext0_wakeup(PIN_WAKEUP_TOMBOL, 0);

    digitalWrite(PIN_LED_STATUS, LOW);
    Serial.println("[POWER] Zzz... Memasuki Deep Sleep. Arus turun ke ~10-150 uA.");
    Serial.flush();
    esp_deep_sleep_start();
}

void loop() {}
