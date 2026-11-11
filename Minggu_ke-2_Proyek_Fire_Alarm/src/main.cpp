#include <Arduino.h>
#include <DHT.h>
#include <ArduinoJson.h>

// [C1] Alokasi Pin Aman ESP32
#define PIN_DHT 4
#define TIPE_DHT DHT22
#define PIN_ASAP_ADC1 34      // ADC1 Input Only untuk Sensor Asap
#define PIN_TOMBOL_MODE 15
#define PIN_RELAY_SPRINKLER 26 // Aktuator penyemprot air
#define PIN_PWM_KIPAS 18       // Aktuator kipas exhaust PWM

#define GUNAKAN_HISTERESIS 1

// [C2] Kalibrasi ADC dalam milivolt
const int MV_ASAP_BERSIH = 500;   // Tegangan saat udara bersih (0% asap)
const int MV_ASAP_TEBAL = 2650;   // Tegangan saat asap pekat (100% asap)

// [C5] Ambang Histeresis Relai Sprinkler
const float AMBANG_SPRINKLER_ON = 60.0f;  // Nyala jika asap > 60%
const float AMBANG_SPRINKLER_OFF = 30.0f; // Mati jika asap < 30%
const float AMBANG_TUNGGAL = 45.0f; 

// [C3] Konfigurasi PWM (Versi ESP32 Core v3.x)
const int PWM_FREQ_HZ = 5000;
const int PWM_RES_BIT = 8;

// [C4] Buffer Moving Average
const int UKURAN_FILTER = 5;
uint32_t bufferMv[UKURAN_FILTER] = {0};
int indeksFilter = 0;
bool filterPenuh = false;

DHT dht(PIN_DHT, TIPE_DHT);
unsigned long waktuTelemetriTerakhir = 0;
unsigned long waktuDebounceTombol = 0;
const unsigned long INTERVAL_BACA_MS = 1000;

bool statusSprinkler = false;
bool modeOverridePaksa = false;
bool tombolSebelumnya = HIGH;
unsigned long seqPaket = 0;
unsigned long hitungTransisiRelai = 0;

// [C6] Variabel Pengaman (Safety Cut-Off)
unsigned long waktuSprinklerMulai = 0;
bool statusSystemFault = false;

// Fungsi PWM Core v3.x
void setupPwmKipas() {
  ledcAttach(PIN_PWM_KIPAS, PWM_FREQ_HZ, PWM_RES_BIT);
}

void tulisPwmKipas(uint8_t duty) {
  ledcWrite(PIN_PWM_KIPAS, duty);
}

// [C4] Logika Pemfilteran Sinyal Asap (Moving Average)
uint32_t bacaAsapMilliVoltsTerfilter() {
  uint32_t mvMentah = analogReadMilliVolts(PIN_ASAP_ADC1);
  indeksFilter = (indeksFilter + 1) % UKURAN_FILTER;
  if (indeksFilter == 0) filterPenuh = true;
  bufferMv[indeksFilter] = mvMentah;
  
  int jumlahSampel = filterPenuh ? UKURAN_FILTER : indeksFilter;
  uint64_t total = 0;
  for (int i = 0; i < jumlahSampel; i++) total += bufferMv[i];
  return (uint32_t)(total / jumlahSampel);
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_ASAP_ADC1, ADC_11db);
  
  pinMode(PIN_ASAP_ADC1, INPUT); // [C1] Wajib INPUT murni
  pinMode(PIN_TOMBOL_MODE, INPUT_PULLUP);
  pinMode(PIN_RELAY_SPRINKLER, OUTPUT);
  
  setupPwmKipas();
  dht.begin();
  Serial.println("{\"event\":\"boot\",\"lab\":\"Lab-01 Fire Alarm C1-C6\"}");
}

void loop() {
  unsigned long sekarang = millis();

  // 1. Baca Tombol Override (Debounce)
  bool bacaTombol = digitalRead(PIN_TOMBOL_MODE);
  if (bacaTombol == LOW && tombolSebelumnya == HIGH && (sekarang - waktuDebounceTombol > 60)) {
    waktuDebounceTombol = sekarang;
    if (statusSystemFault) {
      statusSystemFault = false; // Reset sistem dari kegagalan
    } else {
      modeOverridePaksa = !modeOverridePaksa; // Nyala/matikan paksa sprinkler
    }
  }
  tombolSebelumnya = bacaTombol;

  // 2. Evaluasi Sensor Berkala (1 Detik)
  if (sekarang - waktuTelemetriTerakhir >= INTERVAL_BACA_MS) {
    waktuTelemetriTerakhir = sekarang;
    
    int adcRaw = analogRead(PIN_ASAP_ADC1);
    uint32_t asapMv = bacaAsapMilliVoltsTerfilter();
    
    float asapPct = (float)(asapMv - MV_ASAP_BERSIH) * 100.0f / (float)(MV_ASAP_TEBAL - MV_ASAP_BERSIH);
    asapPct = constrain(asapPct, 0.0f, 100.0f);
    
    float suhuUdara = dht.readTemperature();
    
    bool sprinklerSebelum = statusSprinkler;

    // [C6] Logika Safety Cut-Off (Deteksi Gagal Padam)
    if (statusSprinkler && !statusSystemFault && !modeOverridePaksa) {
      if (millis() - waktuSprinklerMulai > 15000) {
        if (asapPct > 50.0f) {
          statusSystemFault = true;
          statusSprinkler = false; // Matikan paksa penyemprot air (percuma)
          Serial.println("{\"event\":\"FAULT\", \"msg\":\"FIRE_SUPPRESSION_FAILED\"}");
        }
      }
    } else if (!statusSprinkler) {
      waktuSprinklerMulai = millis(); // Reset timer saat mati
    }

    // [C5] Logika Kendali Histeresis
    if (!statusSystemFault) {
      if (modeOverridePaksa) {
        statusSprinkler = true;
      } else {
        #if GUNAKAN_HISTERESIS == 1
          if (asapPct > AMBANG_SPRINKLER_ON) statusSprinkler = true;
          else if (asapPct < AMBANG_SPRINKLER_OFF) statusSprinkler = false;
        #else
          statusSprinkler = (asapPct > AMBANG_TUNGGAL);
        #endif
      }
    }

    if (statusSprinkler != sprinklerSebelum) hitungTransisiRelai++;
    
    // Jika fault, kedipkan relai/LED sebagai tanda evakuasi total
    if (statusSystemFault) digitalWrite(PIN_RELAY_SPRINKLER, (millis() / 500) % 2);
    else digitalWrite(PIN_RELAY_SPRINKLER, statusSprinkler ? HIGH : LOW);

    // [C3] Kendali PWM Kipas Exhaust
    int nilaiDuty = map((long)(suhuUdara * 10.0f), 300, 450, 0, 255);
    nilaiDuty = constrain(nilaiDuty, 0, 255);
    if (statusSystemFault) nilaiDuty = 255; // Kipas dipaksa 100% jika gagal padam
    tulisPwmKipas((uint8_t)nilaiDuty);
    float fanPct = (nilaiDuty * 100.0f) / 255.0f;

    // Output Telemetri JSON
    JsonDocument doc;
    doc["device_id"] = "esp32-firealarm-01";
    doc["seq"] = ++seqPaket;
    doc["adc_raw"] = adcRaw;
    doc["smoke_mv_avg"] = asapMv;
    doc["smoke_pct"] = serialized(String(asapPct, 1));
    doc["temp_c"] = serialized(String(suhuUdara, 1));
    doc["sprinkler_on"] = statusSprinkler;
    doc["relay_switches"] = hitungTransisiRelai;
    doc["fan_pwm_pct"] = serialized(String(fanPct, 1));
    doc["ctrl_mode"] = statusSystemFault ? "FAULT_CUTOFF" : 
                      (modeOverridePaksa ? "MANUAL" : 
                      (GUNAKAN_HISTERESIS ? "HYSTERESIS" : "SINGLE_THR"));
    
    serializeJson(doc, Serial);
    Serial.println();
  }
}