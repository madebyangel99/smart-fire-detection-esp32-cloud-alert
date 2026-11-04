#include <WiFi.h>
#include <HTTPClient.h>
#include "DHTesp.h"

// Deklarasi Pin
const int DHT_PIN = 15;
const int SMOKE_PIN = 34;
const int BUZZER_PIN = 21;
const int LED_PIN = 19;

// Batas Peringatan Kebakaran
const float BATAS_SUHU = 45.0; 
const int BATAS_ASAP = 2000; 

DHTesp dht;
unsigned long waktuNotifTerakhir = 0;

void setup() {
  Serial.begin(115200);
  
  pinMode(SMOKE_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
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
  float suhu = dht.getTemperature();
  int nilaiAsap = analogRead(SMOKE_PIN);
  
  Serial.printf("Suhu: %.2f C | Level Asap: %d\n", suhu, nilaiAsap);

  // Jika kondisi BERBAHAYA
  if (suhu > BATAS_SUHU || nilaiAsap > BATAS_ASAP) {
    
    digitalWrite(LED_PIN, HIGH);
    
    // Aktuasi Sirine
    for (int hz = 400; hz <= 1000; hz += 20) {
      tone(BUZZER_PIN, hz);
      delay(10); 
    }
    for (int hz = 1000; hz >= 400; hz -= 20) {
      tone(BUZZER_PIN, hz);
      delay(10); 
    }
    
    // Kirim Notifikasi Cloud setiap 10 detik
    if (millis() - waktuNotifTerakhir > 10000) {
      kirimNotifikasiCloud(suhu, nilaiAsap);
      waktuNotifTerakhir = millis();
    }
    
  } 
  // Jika kondisi AMAN
  else {
    noTone(BUZZER_PIN); 
    digitalWrite(LED_PIN, LOW);
    delay(1000); 
  }
}

void kirimNotifikasiCloud(float suhu, int asap) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    String botToken = "9167335881:AAEEzXH6xqQkoihWwEqcUySq1HF0_tjLZa4"; 
    String chatId = "5241244905";
    
    // Struktur Pesan Darurat 2 Bahasa (URL Encoded)
    // Tampilan hasil akhir di Telegram:
    // 🚨 STATUS: KRITIS 🚨
    // 🔥 SEGERA EVAKUASI / EVACUATE IMMEDIATELY!
    // 
    // [INDONESIA]
    // Suhu Ruangan: ... C
    // Level Asap: ...
    //
    // [ENGLISH]
    // Room Temp: ... C
    // Smoke Level: ...
    
    String pesan = "%F0%9F%9A%A8%20STATUS:%20KRITIS%20%F0%9F%9A%A8%0A%F0%9F%94%A5%20SEGERA%20EVAKUASI%20/%20EVACUATE%20IMMEDIATELY!%0A%0A%5BINDONESIA%5D%0ASuhu%20Ruangan:%20" + String(suhu) + "%20C%0ALevel%20Asap:%20" + String(asap) + "%0A%0A%5BENGLISH%5D%0ARoom%20Temp:%20" + String(suhu) + "%20C%0ASmoke%20Level:%20" + String(asap);
    
    String url = "https://api.telegram.org/bot" + botToken + "/sendMessage?chat_id=" + chatId + "&text=" + pesan;
    
    Serial.println(">>> MENGIRIM PERINGATAN DARURAT KE TELEGRAM... <<<");
    
    http.begin(url);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      Serial.printf("Pesan Telegram terkirim! Kode: %d\n", httpResponseCode);
    } else {
      Serial.printf("Gagal mengirim Telegram. Error: %d\n", httpResponseCode);
    }
    
    http.end();
  }
}