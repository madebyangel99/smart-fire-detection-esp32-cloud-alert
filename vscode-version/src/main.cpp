#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "DHTesp.h"

const int DHT_PIN = 15;
const int SMOKE_PIN = 34;
const int BUZZER_PIN = 21;
const int LED_PIN = 19;
const int BTN_PIN = 18;

const float BATAS_SUHU = 45.0; 
const int BATAS_ASAP = 2000; 

DHTesp dht;
unsigned long waktuNotifTerakhir = 0;
unsigned long waktuBacaSensor = 0; // Timer baru khusus sensor

bool alarmBisu = false;
float suhuSekarang = 24.0; // Ide Anda: Default mulai dari 24 derajat
int asapSekarang = 0;

void kirimNotifikasiCloud(float suhu, int asap);

void setup() {
  Serial.begin(115200);
  pinMode(SMOKE_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  
  dht.setup(DHT_PIN, DHTesp::DHT22);

  WiFi.begin("Wokwi-GUEST", "", 6);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung!");
}

void loop() {
  // 1. NON-BLOCKING SENSOR READ (Hanya baca setiap 2 detik)
  if (millis() - waktuBacaSensor > 2000) {
    float bacaSuhu = dht.getTemperature();
    
    // Jika sensor tidak error (bukan nan), baru update nilai suhuSekarang
    if (!isnan(bacaSuhu)) {
      suhuSekarang = bacaSuhu;
    }
    
    asapSekarang = analogRead(SMOKE_PIN);
    Serial.printf("Suhu: %.2f C | Level Asap: %d\r\n", suhuSekarang, asapSekarang);
    
    waktuBacaSensor = millis(); // Reset timer
  }

  // 2. LOGIKA ALARM KONDISI BAHAYA
  if (suhuSekarang > BATAS_SUHU || asapSekarang > BATAS_ASAP) {
    digitalWrite(LED_PIN, HIGH); 
    
    if (!alarmBisu) {
      // Nada Naik
      for (int hz = 400; hz <= 1000; hz += 20) {
        tone(BUZZER_PIN, hz);
        delay(10); 
        if (digitalRead(BTN_PIN) == LOW) { alarmBisu = true; break; }
      }
      // Nada Turun
      if (!alarmBisu) {
        for (int hz = 1000; hz >= 400; hz -= 20) {
          tone(BUZZER_PIN, hz);
          delay(10); 
          if (digitalRead(BTN_PIN) == LOW) { alarmBisu = true; break; }
        }
      }
    } else {
      noTone(BUZZER_PIN); 
    }
    
    // Timer Notifikasi (Setiap 10 detik)
    if (millis() - waktuNotifTerakhir > 10000) {
      kirimNotifikasiCloud(suhuSekarang, asapSekarang);
      waktuNotifTerakhir = millis();
    }
  } 
  // 3. KONDISI AMAN
  else {
    alarmBisu = false; 
    noTone(BUZZER_PIN); 
    digitalWrite(LED_PIN, LOW);
  }
}

// Fungsi pengiriman ke Telegram tetap sama...
void kirimNotifikasiCloud(float suhu, int asap) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String botToken = "9167335881:AAEEzXH6xqQkoihWwEqcUySq1HF0_tjLZa4"; 
    String chatId = "5241244905";     
    String pesan = "%F0%9F%9A%A8%20STATUS:%20KRITIS%20%F0%9F%9A%A8%0A%F0%9F%94%A5%20SEGERA%20EVAKUASI%0A%0ASuhu%20Ruangan:%20" + String(suhu) + "%20C%0ALevel%20Asap:%20" + String(asap);
    String url = "https://api.telegram.org/bot" + botToken + "/sendMessage?chat_id=" + chatId + "&text=" + pesan;
    
    http.begin(url);

    http.setTimeout(2000);
    
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      Serial.println("Peringatan Telegram terkirim!");
    }
    http.end();
  }
}