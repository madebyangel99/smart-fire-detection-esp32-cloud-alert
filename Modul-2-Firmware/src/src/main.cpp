#include <Arduino.h>
#include <DHT.h>
#include <ArduinoJson.h>

// PIN (sesuai checklist: 34, 4, 15, 18, 26)
#define SOIL_PIN    34
#define DHT_PIN     4
#define BUTTON_PIN  15
#define FAN_PIN     18   // LED biru = kipas (PWM), berkedip saat fault
#define RELAY_PIN   26   // pompa

#define PWM_FREQUENCY  5000
#define PWM_RESOLUTION 8
#define FAN_CHANNEL    0   // dipakai hanya di core 2.x

// Noise ADC buatan agar chattering terlihat di simulator (set 0 untuk mematikan)
#define SIMULATE_NOISE 1
#define NOISE_RAW      150

// Parameter
const int   MA_SIZE             = 10;
const float THRESHOLD           = 40.0;   // mode A
const float HYST_ON             = 30.0;   // mode B
const float HYST_OFF            = 45.0;   // mode B
const float TEMP_FAN_MIN        = 25.0;
const float TEMP_FAN_MAX        = 40.0;
const unsigned long PUMP_MAX_RUN_MS = 15000;  // C6: 15 detik
const float PUMP_MIN_RISE       = 5.0;        // C6: kenaikan soil_pct minimum (%)
const unsigned long BLINK_MS    = 250;
const unsigned long SAMPLE_MS   = 100;
const unsigned long PRINT_MS    = 1000;
const unsigned long DHT_MS      = 2500;
const unsigned long LONG_PRESS  = 1500;

enum Mode { CHATTERING = 0, HYSTERESIS = 1 };

DHT dht(DHT_PIN, DHT22);

// Moving average
int  maBuf[MA_SIZE];
int  maIdx = 0, maCount = 0;
long maSum = 0;

// Status
Mode mode = CHATTERING;
bool relayState = false;
bool faultLatched = false;      // C6: true = "DRY_RUN_PROTECTION"
unsigned long relayToggles = 0;
unsigned long pumpStart = 0;    // awal jendela pengecekan 15 detik
unsigned long pumpOnSince = 0;  // awal pompa menyala (untuk pump_run_s)
float pumpStartMoist = 0;

int   soilRaw = 0;
float soilAvgRaw = 0;
float soilPct = 0;       // dari data mentah
float soilFiltPct = 0;   // dari moving average (soil_pct)
float temperature = NAN, humidity = NAN;
int   fanPwm = 0;

// Tombol
int btnLast = HIGH, btnStable = HIGH;
unsigned long btnChange = 0, pressStart = 0;

unsigned long tSample = 0, tPrint = 0, tDht = 0, tBlink = 0;
bool blinkOn = false;

// ---------- PWM ----------
void setupPwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(FAN_PIN, PWM_FREQUENCY, PWM_RESOLUTION);
#else
    ledcSetup(FAN_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(FAN_PIN, FAN_CHANNEL);
#endif
}

void writeFan(int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(FAN_PIN, duty);
#else
    ledcWrite(FAN_CHANNEL, duty);
#endif
}

// ---------- Sensor tanah ----------
float movingAverage(int v) {
    if (maCount < MA_SIZE) {
        maBuf[maIdx] = v;
        maSum += v;
        maCount++;
    } else {
        maSum -= maBuf[maIdx];
        maBuf[maIdx] = v;
        maSum += v;
    }
    maIdx = (maIdx + 1) % MA_SIZE;
    return (float)maSum / maCount;
}

// Pemetaan terbalik: raw 0 -> 100%, raw 4095 -> 0%
float rawToPercent(float raw) {
    return constrain((4095.0f - raw) * 100.0f / 4095.0f, 0.0f, 100.0f);
}

int readSoilRaw() {
    int v = analogRead(SOIL_PIN);
#if SIMULATE_NOISE
    v += random(-NOISE_RAW, NOISE_RAW + 1);
#endif
    return constrain(v, 0, 4095);
}

// ---------- Relay ----------
void setRelay(bool on) {
    if (on == relayState) return;
    relayState = on;
    relayToggles++;
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    if (on) {
        pumpStart = millis();
        pumpOnSince = pumpStart;
        pumpStartMoist = soilFiltPct;
    }
    Serial.println(on ? "Relay ON" : "Relay OFF");
}

void updateRelay() {
    if (faultLatched) {          // terkunci: pompa tidak boleh menyala
        setRelay(false);
        return;
    }
    bool want = relayState;
    if (mode == CHATTERING) {
        want = (soilPct < THRESHOLD);            // satu ambang, data mentah
    } else {
        if (!relayState && soilFiltPct < HYST_ON) want = true;    // histeresis
        else if (relayState && soilFiltPct > HYST_OFF) want = false;
    }
    setRelay(want);
}

// C6: Pump Run-Time Safety Cut-Off
void checkPumpRunTime() {
    if (!relayState || faultLatched) return;
    if (millis() - pumpStart >= PUMP_MAX_RUN_MS) {
        if (soilFiltPct - pumpStartMoist < PUMP_MIN_RISE) {
            faultLatched = true;
            setRelay(false);
            Serial.println("!!! FAULT: DRY_RUN_PROTECTION - pompa >15 dtk tanpa kenaikan soil_pct (tandon habis / selang bocor / sensor lepas).");
            Serial.println("    Reset: tahan tombol >1.5 dtk atau ketik 'c'.");
        } else {
            pumpStart = millis();            // soil_pct naik: mulai jendela baru
            pumpStartMoist = soilFiltPct;
        }
    }
}

// Kedipan peringatan pada LED D18 saat fault
void handleWarningBlink() {
    if (!faultLatched) return;
    if (millis() - tBlink >= BLINK_MS) {
        tBlink = millis();
        blinkOn = !blinkOn;
        writeFan(blinkOn ? 255 : 0);
    }
}

// ---------- Kontrol ----------
void toggleMode() {
    mode = (mode == CHATTERING) ? HYSTERESIS : CHATTERING;
    relayToggles = 0;
    Serial.println(mode == CHATTERING
        ? "MODE A: CHATTERING (satu ambang 40%), counter di-reset"
        : "MODE B: HISTERESIS (ON<30%, OFF>45%), counter di-reset");
}

void resetFault() {
    if (faultLatched) {
        faultLatched = false;
        writeFan(fanPwm);        // kembalikan fungsi kipas
        Serial.println("Fault DRY_RUN_PROTECTION di-reset");
    }
}

void handleButton() {
    int r = digitalRead(BUTTON_PIN);
    if (r != btnLast) {
        btnChange = millis();
        btnLast = r;
    }
    if (millis() - btnChange > 30 && r != btnStable) {
        btnStable = r;
        if (r == LOW) {
            pressStart = millis();
        } else {
            if (millis() - pressStart >= LONG_PRESS) resetFault();
            else toggleMode();
        }
    }
}

void handleSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 'm' || c == 'M') toggleMode();
        if (c == 'c' || c == 'C') resetFault();
    }
}

void printJson() {
    JsonDocument doc;
    if (isnan(temperature) || isnan(humidity)) {
        doc["temperature"] = nullptr;
        doc["humidity"] = nullptr;
    } else {
        doc["temperature"] = temperature;
        doc["humidity"] = humidity;
    }
    doc["soil_raw"] = soilRaw;
    doc["soil_raw_avg"] = (int)soilAvgRaw;
    doc["soil_pct"] = (int)soilFiltPct;
    doc["mode"] = (mode == CHATTERING) ? "chattering" : "histeresis";
    doc["relay"] = relayState;
    doc["pump_run_s"] = relayState ? (int)((millis() - pumpOnSince) / 1000) : 0;
    doc["relay_toggles"] = relayToggles;
    doc["fan_pwm"] = fanPwm;
    doc["fault"] = faultLatched ? "DRY_RUN_PROTECTION" : "NONE";
    serializeJson(doc, Serial);
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nESP32 IoT LAB 1");
    Serial.println("Tombol: tekan singkat = ganti mode, tahan >1.5 dtk = reset fault");
    Serial.println("Serial: 'm' = ganti mode, 'c' = reset fault");

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    setupPwm();
    writeFan(0);
    analogSetPinAttenuation(SOIL_PIN, ADC_11db);
    randomSeed(esp_random());

    dht.begin();
    delay(2000);

    for (int i = 0; i < MA_SIZE; i++) {      // isi buffer awal
        soilRaw = readSoilRaw();
        soilAvgRaw = movingAverage(soilRaw);
    }
    soilFiltPct = rawToPercent(soilAvgRaw);
    btnLast = btnStable = digitalRead(BUTTON_PIN);
    Serial.println("MODE A: CHATTERING (satu ambang 40%)");
}

void loop() {
    handleButton();
    handleSerial();
    handleWarningBlink();

    unsigned long now = millis();

    if (now - tSample >= SAMPLE_MS) {
        tSample = now;
        soilRaw = readSoilRaw();
        soilAvgRaw = movingAverage(soilRaw);
        soilPct = rawToPercent(soilRaw);
        soilFiltPct = rawToPercent(soilAvgRaw);
        updateRelay();
        checkPumpRunTime();
    }

    if (now - tDht >= DHT_MS) {
        tDht = now;
        float h = dht.readHumidity();
        float t = dht.readTemperature();
        if (!isnan(h) && !isnan(t)) {
            humidity = h;
            temperature = t;
            float f = constrain((t - TEMP_FAN_MIN) / (TEMP_FAN_MAX - TEMP_FAN_MIN), 0.0f, 1.0f);
            fanPwm = (int)(f * 255);
            if (!faultLatched) writeFan(fanPwm);   // saat fault, LED dipakai untuk kedipan
        } else {
            temperature = NAN;
            humidity = NAN;
            Serial.println("DHT22 gagal dibaca");
        }
    }

    if (now - tPrint >= PRINT_MS) {
        tPrint = now;
        printJson();
    }
}