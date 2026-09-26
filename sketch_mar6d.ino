//ปัจจุบัน 7/3/2569 ที่ใช้อัพโหลดบอร์ด Farm 1 ESP32

#include <Arduino.h>
#include <cmath>
#include <WiFi.h>
#include <PubSubClient.h>

// --- [Pin Definition for IOXESP32 Farm Base] ---
#define MAX485_DE_RE 2
#define RX2_PIN 4
#define TX2_PIN 15

// --- [1. WiFi Settings] ---
const char* ssid = "Project_IOT";
const char* password = "1234567890";

// --- [2. NETPIE Settings] ---
const char* mqtt_server    = "mqtt.netpie.io";
const char* mqtt_client_id = "ca45cc09-c92c-4fcc-8761-533e20e38d6b";
const char* mqtt_token     = "cEAsBiux2XsKio46RPJAeRi8Bqn6TpWP";
const char* mqtt_secret    = "c3ctn3Jwe8Cn4v9bvk9iW4d2PDTmwVf2";

WiFiClient espClient;
PubSubClient client(espClient);

// --- [ตัวแปรสำหรับการหาค่าเฉลี่ย 8 ครั้งต่อวัน และนับวัน]---
float sumETo = 0.0;        // เก็บผลรวม ETo
int readingCount = 0;      // นับจำนวนครั้งที่อ่าน (0-8)
int dayCount = 0;          // นับจำนวนวันที่ระบบทำงานครบ 24 ชั่วโมง <--- เพิ่มบรรทัดนี้

unsigned long previousMillis = 0; 
bool firstRun = true;      

// ***  3 ชั่วโมง (3 * 60 * 60 * 1000 = 10800000 มิลลิวินาที) ***
const unsigned long interval = 10800000; 

// --- [Math function for ETo calculation] ---
float calculateETo(float T, float Rn, float G, float u2, float es, float ea, float delta, float gamma) {
  float term1 = 0.408 * delta * (Rn - G);
  float term2 = gamma * (900.0 / (T + 273.0)) * u2 * (es - ea);
  float numerator = term1 + term2;
  float denominator = delta + gamma * (1.0 + 0.34 * u2);
  return numerator / denominator;
}

// --- [RS485 Sensor Reading Functions] ---
uint16_t calculateCRC(byte *buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) { crc >>= 1; crc ^= 0xA001; } 
      else { crc >>= 1; }
    }
  }
  return crc;
}

float readSenseCAP(uint16_t regAddr) {
  while(Serial2.available()) Serial2.read();
  byte req[8] = {0x14, 0x04, (byte)((regAddr >> 8) & 0xFF), (byte)(regAddr & 0xFF), 0x00, 0x02, 0, 0}; 
  uint16_t crc = calculateCRC(req, 6);
  req[6] = crc & 0xFF; req[7] = (crc >> 8) & 0xFF;

  digitalWrite(MAX485_DE_RE, HIGH);
  delay(2); Serial2.write(req, 8); Serial2.flush(); delay(1); 
  digitalWrite(MAX485_DE_RE, LOW);

  unsigned long startTime = millis();
  byte buf[30]; int len = 0;
  while (millis() - startTime < 500) {
    if (Serial2.available()) {
      buf[len++] = Serial2.read();
      if (len >= 30) break;
    }
  }
  for (int i = 0; i <= len - 9; i++) {
    if (buf[i] == 0x14 && buf[i+1] == 0x04 && buf[i+2] == 0x04) {
      int32_t rawData = (int32_t)(((uint32_t)buf[i+3] << 24) | ((uint32_t)buf[i+4] << 16) | ((uint32_t)buf[i+5] << 8) | ((uint32_t)buf[i+6]));
      return rawData / 1000.0; 
    }
  }
  return NAN; 
}

// --- [NETPIE Connection Function] ---
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to NETPIE...");
    if (client.connect(mqtt_client_id, mqtt_token, mqtt_secret)) {
      Serial.println(" Connected!");
    } else {
      Serial.print(" Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 5 seconds...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(MAX485_DE_RE, OUTPUT);
  digitalWrite(MAX485_DE_RE, LOW); 
  Serial2.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);

  Serial.println("\n--- System Started: NETPIE + ETo Daily Average (3 Hours) ---");

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  // Set NETPIE MQTT Server
  client.setServer(mqtt_server, 1883);
}

void loop() {
  // Maintain NETPIE connection
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      reconnectMQTT();
    }
    client.loop();
  }

  unsigned long currentMillis = millis();

  // Read and send data
  if (currentMillis - previousMillis >= interval || firstRun) {
    previousMillis = currentMillis; 
    firstRun = false;

    Serial.println("\n[ Reading Sensor Data... ]");

    float temp     = readSenseCAP(0x0000); delay(100); 
    float hum      = readSenseCAP(0x0002); delay(100);
    float pressure = readSenseCAP(0x0004); delay(100);
    float light    = readSenseCAP(0x0006); delay(100);
    float wind     = readSenseCAP(0x0012); delay(100);
    float rain     = readSenseCAP(0x0014); delay(100);

    if (!isnan(temp) && !isnan(hum)) {
      // Print sensor values
      Serial.printf("Temp:       %.2f °C\n", temp);
      Serial.printf("Hum:        %.2f %%\n", hum);
      Serial.printf("Pressure:   %.2f hPa\n", pressure / 100.0);
      Serial.printf("Light:      %.0f Lux\n", light);
      Serial.printf("Wind Speed: %.2f m/s\n", wind);
      Serial.printf("Rainfall:   %.2f mm\n", rain);

      // --- 3. ETo Calculation (รอบปัจจุบัน) ---
      float P = pressure / 1000.0; 
      float Rs = (light / 116.0) * 0.0864; 
      float Rn = 0.77 * Rs; 
      float es = 0.6108 * exp((17.27 * temp) / (temp + 237.3)); 
      float ea = es * (hum / 100.0); 
      float delta = (4098.0 * es) / pow((temp + 237.3), 2);
      float gamma = 0.000665 * P; 
      float eto_now = calculateETo(temp, Rn, 0.0, wind, es, ea, delta, gamma);
      
      // เก็บค่ารวมและเพิ่มตัวนับ
      sumETo += eto_now;
      readingCount++;
      
      Serial.printf("ETo (Now):  %.2f mm/day\n", eto_now);
      Serial.printf("Reading:    %d/8\n", readingCount);
      Serial.printf("Days:       %d\n", dayCount); // แสดงวันบน Serial

      // --- 4. Create JSON Payload for NETPIE ---
      String payload = "{\"data\": {";
      payload += "\"Temp\":" + String(temp) + ",";
      payload += "\"Hum\":" + String(hum) + ",";
      payload += "\"Pressure\":" + String(pressure / 100.0) + ",";
      payload += "\"Light\":" + String(light) + ",";
      payload += "\"Wind_Speed\":" + String(wind) + ",";
      payload += "\"Rainfall\":" + String(rain) + ",";
      payload += "\"ETo_Now\":" + String(eto_now) + ",";
      
    
      // ถ้าครบ 8 ครั้ง (ครบ 1 วัน) ให้หาค่าเฉลี่ย
      if (readingCount >= 8) {
        float avgETo = sumETo / 8.0; 
        dayCount++; // เพิ่มจำนวนวันไปอีก 1 <--- เพิ่มบรรทัดนี้
        
        Serial.println("******************************************");
        Serial.printf("=> Daily Average ETo: %.2f mm/day\n", avgETo);
        Serial.printf("=> Total Days Logged: %d\n", dayCount);
        Serial.println("******************************************");
        
        // เพิ่มค่าเฉลี่ยเข้าไปใน Payload เพื่อส่งขึ้น NETPIE ด้วย
        payload += ",\"ETo_Avg\":" + String(avgETo);
        // อัปเดตค่า Day ใน payload อีกครั้งเพราะมีการบวกวันเพิ่ม

        payload += ",\"Day\":" + String(dayCount); 
        
        // รีเซ็ตตัวแปรเพื่อเริ่มวันใหม่
        sumETo = 0.0;
        readingCount = 0;
      } else {
         // ถ้ายังไม่ครบ 8 ครั้ง ให้ส่ง ETo_Avg เป็น 0.0 ไปก่อน เพื่อไม่ให้โชว์ค่าเก่าค้าง
         payload += ",\"ETo_Avg\": 0.0";
      }

      payload += "}}";

      // --- 5. Publish to NETPIE ---
      client.publish("@shadow/data/update", payload.c_str());
      Serial.println("=> Successfully published data to NETPIE!");
      Serial.println("-----------------------------------------");

    } else {
      Serial.println("=> Failed to read sensor data!");
    }
  }
}