#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "DHT.h"

const char* ssid = "Isi dengan nama Wifi";
const char* password = "Isi dengan Password";
#define BOT_TOKEN "Isi dengan token Bot"
#define CHAT_ID "Isi dengan Chat Id"

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define MQ135_PIN 34
#define LDR_PIN 35

#define LED_STATUS 2
#define BUZZER_PIN 13

unsigned long lastTime = 0;
unsigned long interval = 6000;
unsigned long lastBotCheck = 0;
unsigned long botCheckInterval = 2000;

const float RL = 10.0;
const float R0 = 4.0;
const float ADC_RESOLUTION = 4095.0;
const float VREF = 3.3;

bool isActive = false;

float convertLDRToLux(int analogValue) {
  float maxLux = 1000.0;
  float normalized = 1.0 - (float)analogValue / 4095.0;
  float lux = pow(normalized, 2.5) * maxLux;
  return lux;
}

float getMQ135PPM(int analogValue) {
  float voltage = analogValue * (VREF / ADC_RESOLUTION);
  if (voltage < 0.01) return -1;

  float RS = (VREF - voltage) * RL / voltage;
  if (RS <= 0) return -1;

  float ratio = RS / R0;
  float ppm = pow(10, ((log10(ratio) - 1.92) / -0.42));
  if (isinf(ppm) || isnan(ppm)) return -1;

  return ppm;
}

String getAirQualityStatus(float ppm) {
  if (ppm < 400) return "🟢 *Baik*";
  else if (ppm < 1000) return "🟡 *Sedang*";
  else return "🔴 *Buruk*";
}

void ringBuzzer(int durationMillis) {
  unsigned long startMillis = millis();
  while (millis() - startMillis < durationMillis) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

void handleTelegramCommands() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String text = bot.messages[i].text;
      String chat_id = bot.messages[i].chat_id;
      String from_name = bot.messages[i].from_name;

      if (text == "/start") {
        isActive = true;
        bot.sendMessage(chat_id, "✅ Pengiriman data sensor *DI-AKTIFKAN*", "Markdown");
      } else if (text == "/stop") {
        isActive = false;
        bot.sendMessage(chat_id, "⛔ Pengiriman data sensor *DIHENTIKAN*", "Markdown");
      } else if (text == "/status") {
        String statusMsg = isActive ? "📡 Bot *AKTIF*" : "💤 Bot *NONAKTIF*";
        bot.sendMessage(chat_id, statusMsg, "Markdown");
      } else {
        bot.sendMessage(chat_id, "❓ Perintah tidak dikenal. Gunakan /start, /stop, atau /status", "Markdown");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(LED_STATUS, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_STATUS, HIGH);
  digitalWrite(BUZZER_PIN, LOW);

  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung");
  client.setInsecure();
}

void loop() {
  if (millis() - lastBotCheck > botCheckInterval) {
    lastBotCheck = millis();
    handleTelegramCommands();
  }

  if (isActive && millis() - lastTime > interval) {
    lastTime = millis();

    digitalWrite(LED_STATUS, LOW);
    delay(100);
    digitalWrite(LED_STATUS, HIGH);

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int mq135Raw = analogRead(MQ135_PIN);
    int ldrRaw = analogRead(LDR_PIN);
    float ldrLux = convertLDRToLux(ldrRaw);

    // Debug: tampilkan nilai ADC dan lux di Serial Monitor
    Serial.print("LDR ADC: ");
    Serial.print(ldrRaw);
    Serial.print(" → Lux: ");
    Serial.println(ldrLux, 2);

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Gagal baca DHT11!");
      return;
    }

    float mq135PPM = getMQ135PPM(mq135Raw);
    String mq135Status;

    if (mq135PPM < 0) {
      mq135PPM = 0;
      mq135Status = "❓ *Tidak Valid*";
    } else {
      mq135Status = getAirQualityStatus(mq135PPM);
    }

    String message = "📡 *Data Sensor ESP32*\n";
    message += "🌡 Suhu: " + String(temperature) + " °C\n";
    message += "💧 Kelembaban: " + String(humidity) + " %\n";
    message += "🧪 MQ135: " + String(mq135PPM, 2) + " ppm (" + mq135Status + ")\n";
    message += "💡 Intensitas Cahaya: " + String(ldrLux, 2) + " lux\n";

    String warning = "";
    int warningCount = 0;

    if (temperature > 30) {
      warning += "⚠️ Suhu terlalu panas untuk belajar!\n";
      warningCount++;
    } else if (temperature < 23) {
      warning += "⚠️ Suhu terlalu dingin untuk belajar!\n";
      warningCount++;
    }

    if (humidity < 30) {
      warning += "⚠️ Udara terlalu kering!\n";
      warningCount++;
    } else if (humidity > 70) {
      warning += "⚠️ Udara terlalu lembab!\n";
      warningCount++;
    }

    if (mq135PPM > 1000) {
      warning += "⚠️ Kualitas udara buruk! CO₂ tinggi.\n";
      warningCount++;
    }

    if (ldrLux < 300) {
      warning += "⚠️ Pencahayaan kurang! Tidak ideal untuk belajar.\n";
      warningCount++;
    }

    Serial.println("Mengirim ke Telegram...");
    bot.sendMessage(CHAT_ID, message, "Markdown");

    if (warningCount > 0) {
      bot.sendMessage(CHAT_ID, "🚨 *PERINGATAN KONDISI TIDAK NYAMAN:*\n" + warning, "Markdown");
    }

    if (warningCount >= 3) {
      ringBuzzer(3000);
    }
  }
}
