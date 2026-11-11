#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <ArduinoJson.h>

// --- KONFIGURASI LAYAR OLED (I2C) ---
#define LEBAR_LAYAR 128
#define TINGGI_LAYAR 64
#define ALAMAT_OLED 0x3C

// --- KONFIGURASI PIN PROYEK FIRE ALARM ---
#define DHT_PIN 15
#define SMOKE_PIN 34
#define BUZZER_PIN 23              // Dipindah dari 21 (GPIO 21 kini dipakai SDA I2C)
#define LED_PIN 19
#define BTN_PIN 4                  // EXTO Wakeup Pin: WAJIB RTC GPIO (GPIO 4), bukan GPIO 18
#define PIN_LED_STATUS 2           // Indikator Status Aktif/Deep Sleep

// --- AMBANG BATAS ALARM ---
const float BATAS_SUHU = 45.0f;
const int BATAS_ASAP = 2000;
#define DURASI_TIDUR_DETIK 10      // Durasi Deep Sleep (10 detik)

// --- INISIALISASI PERANTI ---
Adafruit_SSD1306 display(LEBAR_LAYAR, TINGGI_LAYAR, &Wire, -1);
DHTesp dht;

// --- VARIABEL PERSISTEN DI RTC SLOW MEMORY ---
RTC_DATA_ATTR int nomorSiklusBoot = 0;
RTC_DATA_ATTR float riwayatSuhuTerakhir = 0.0f;
RTC_DATA_ATTR int riwayatAsapTerakhir = 0;

// Struktur Data Telemetri Bersama (FreeRTOS)
struct DataFireAlarm {
  float suhu;
  int nilaiAsap;
  bool statusBahaya;
  bool valid;
} dataFire;

SemaphoreHandle_t mutexData;

// --- CEKLIS 3.3 POIN 1: PEMINDAIAN BUS I2C ---
void pindaiBusI2C() {
  Serial.println("\n[I2C SCAN] Memindai peranti pada bus bersama (SDA=21, SCL=22)...");
  int jumlahDitemukan = 0;

  for (byte alamat = 1; alamat < 127; alamat++) {
    Wire.beginTransmission(alamat);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("[I2C SCAN] Peranti terdeteksi di alamat 0x%02X", alamat);
      if (alamat == ALAMAT_OLED) {
        Serial.print("  -> OLED SSD1306");
      }
      Serial.println();
      jumlahDitemukan++;
    } else if (error == 4) {
      Serial.printf("[I2C SCAN] Error tidak diketahui di alamat 0x%02X\n", alamat);
    }
  }

  if (jumlahDitemukan == 0) {
    Serial.println("[I2C SCAN] Tidak ada peranti I2C yang terdeteksi! Periksa kabel SDA/SCL.");
  } else {
    Serial.printf("[I2C SCAN] Selesai. Total %d peranti ditemukan.\n", jumlahDitemukan);
  }
}

// --- DOKUMENTASI DIAGNOSTIK WAKEUP ---
void cetakDiagnostikWakeup() {
  esp_sleep_wakeup_cause_t alasan = esp_sleep_get_wakeup_cause();
  Serial.printf("\n==========================================\n");
  Serial.printf("[RTC MEM] Siklus Bangun Ke-%d\n", nomorSiklusBoot);

  switch (alasan) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("[RTC WAKEUP] Pemicu: Pewaktu Internal (RTC Timer)");
      break;
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("[RTC WAKEUP] Pemicu: Tombol Darurat/Reset (EXT0 GPIO 4)");
      break;
    default:
      Serial.println("[RTC WAKEUP] Pemicu: Power-on Reset / Boot Pertama");
      break;
  }

  Serial.printf("[RTC MEM] Riwayat Lalu -> Suhu: %.1f C | Asap: %d\n", 
                riwayatSuhuTerakhir, riwayatAsapTerakhir);
  Serial.printf("==========================================\n");
}

// --- VISUALISASI OLED ---
void perbaruiTampilanOLED(float suhu, int asap, bool bahaya, int siklus, const char* pemicu) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header Judul
  display.setCursor(0, 0);
  display.print("FIRE ALARM SYSTEM");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Status Lingkungan
  display.setCursor(0, 15);
  display.printf("Suhu : %.1f C", suhu);
  display.setCursor(0, 27);
  display.printf("Asap : %d ADC", asap);

  // Indikator Status Bahaya
  display.setCursor(0, 39);
  if (bahaya) {
    display.print("STATUS: ** BAHAYA **");
  } else {
    display.print("STATUS: AMAN");
  }

  // Footer Daya & RTC
  display.drawLine(0, 50, 127, 50, SSD1306_WHITE);
  display.setCursor(0, 54);
  display.printf("Boot:%d | Src:%s", siklus, pemicu);

  display.display();
}

// --- TASK FREERTOS (Core 0) ---
void TaskTampilanDanSerial(void *pvParameters) {
  DataFireAlarm lokal;

  xSemaphoreTake(mutexData, portMAX_DELAY);
  lokal = dataFire;
  xSemaphoreGive(mutexData);

  esp_sleep_wakeup_cause_t alasan = esp_sleep_get_wakeup_cause();
  const char* labelPemicu = (alasan == ESP_SLEEP_WAKEUP_EXT0) ? "EXT0_BTN" : "TIMER";

  if (lokal.valid) {
    perbaruiTampilanOLED(lokal.suhu, lokal.nilaiAsap, lokal.statusBahaya, nomorSiklusBoot, labelPemicu);

    JsonDocument doc;
    doc["device_id"] = "esp32-fire-alarm-01";
    doc["boot_count"] = nomorSiklusBoot;
    doc["wakeup_cause"] = labelPemicu;
    doc["temp_c"] = serialized(String(lokal.suhu, 1));
    doc["smoke_adc"] = lokal.nilaiAsap;
    doc["is_danger"] = lokal.statusBahaya;
    doc["prev_temp_c"] = serialized(String(riwayatSuhuTerakhir, 1));
    doc["prev_smoke_adc"] = riwayatAsapTerakhir;

    serializeJson(doc, Serial);
    Serial.println();

    riwayatSuhuTerakhir = lokal.suhu;
    riwayatAsapTerakhir = lokal.nilaiAsap;
  }

  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SMOKE_PIN, INPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);

  digitalWrite(PIN_LED_STATUS, HIGH);

  nomorSiklusBoot++;
  cetakDiagnostikWakeup();

  mutexData = xSemaphoreCreateMutex();

  Wire.begin(21, 22);

  pindaiBusI2C();

  if (!display.begin(SSD1306_SWITCHCAPVCC, ALAMAT_OLED)) {
    Serial.println("[ERROR] Gagal menemukan layar OLED SSD1306 (0x3C)!");
  }

  dht.setup(DHT_PIN, DHTesp::DHT22);

  float temp = dht.getTemperature();
  int smoke = analogRead(SMOKE_PIN);

  bool isDanger = (temp >= BATAS_SUHU || smoke >= BATAS_ASAP);

  if (isDanger) {
    digitalWrite(LED_PIN, HIGH);
    tone(BUZZER_PIN, 1000);
    delay(1000);
    noTone(BUZZER_PIN);
  } else {
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }

  xSemaphoreTake(mutexData, portMAX_DELAY);
  dataFire.suhu = temp;
  dataFire.nilaiAsap = smoke;
  dataFire.statusBahaya = isDanger;
  dataFire.valid = true;
  xSemaphoreGive(mutexData);

  xTaskCreatePinnedToCore(TaskTampilanDanSerial, "TaskOLED_Serial", 4096, NULL, 1, NULL, 0);

  delay(1000);

  Serial.printf("[POWER] Menyiapkan Deep Sleep selama %d detik...\n", DURASI_TIDUR_DETIK);

  esp_sleep_enable_timer_wakeup((uint64_t)DURASI_TIDUR_DETIK * 1000000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PIN, 0);

  digitalWrite(PIN_LED_STATUS, LOW);
  Serial.println("[POWER] Zzz... Memasuki Deep Sleep. Arus hemat energi aktif.");
  Serial.flush();

  esp_deep_sleep_start();
}

void loop() {
  // Kosong
}