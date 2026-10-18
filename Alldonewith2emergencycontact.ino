// ----------------- BLYNK CREDENTIALS -----------------
#define BLYNK_TEMPLATE_ID "TMPL3s584Xc7L"
#define BLYNK_TEMPLATE_NAME "Remote Relay Switch"
#define BLYNK_AUTH_TOKEN "FWsnPqa8_tk6XFFv-NS4W2rft-GHJteK"

// ----------------- LIBRARIES -----------------
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <TinyGPS.h>
#include <HardwareSerial.h>
#include <math.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// ------------------- WIFI -------------------
char ssid[] = "realme";
char pass[] = "12341234";

// ------------------- TELEGRAM -------------------
#define BOT_TOKEN "7881082334:AAEl7xQKIA3_Da6bLYZ05j6lcMiBow9PnAA"
#define CHAT_ID1 "1852146055"   // Your Telegram ID
#define CHAT_ID2 "7562463296"   // Friend's Telegram ID
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ------------------- HARDWARE -------------------
#define RELAY_PIN 23
#define GPS_RX 16
#define GPS_TX 17

// ------------------- SENSOR -------------------
Adafruit_MPU6050 mpu;
HardwareSerial gpsSerial(2);
TinyGPS gps;
BlynkTimer timer;

// ------------------- CONSTANTS -------------------
const float TILT_ANGLE_THRESHOLD = 45.0;
const unsigned long ALERT_TIME_MS = 60000;   // 60s countdown
const unsigned long LED_BLINK_INTERVAL = 500;

// ------------------- VARIABLES -------------------
bool alertActive = false;
bool messageSent = false;
bool ledState = false;
unsigned long alertStartTime = 0;
unsigned long lastLedToggle = 0;
int blynkTimerID;

float latitude = 0.0, longitude = 0.0, tiltAngle = 0.0;
int satelliteCount = 0;

// ------------------ BLYNK RELAY ------------------
BLYNK_WRITE(V1)
{
  int buttonState = param.asInt();
  digitalWrite(RELAY_PIN, buttonState ? LOW : HIGH);
  Serial.println(buttonState ? ">>> Motor ON" : ">>> Motor OFF");
}

// ------------------ READ SENSOR DATA ------------------
void readSensors() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  unsigned long gpsAge;
  gps.f_get_position(&latitude, &longitude, &gpsAge);
  satelliteCount = gps.satellites();

  if (gpsAge == TinyGPS::GPS_INVALID_AGE) {
    latitude = longitude = 0.0;
    satelliteCount = 0;
  }

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  tiltAngle = acos(a.acceleration.z /
              sqrt(pow(a.acceleration.x,2)+pow(a.acceleration.y,2)+pow(a.acceleration.z,2))) * 180.0 / PI;
}

// ------------------ BLYNK DASHBOARD ------------------
void updateDashboard() {
  String latDir = (latitude >= 0) ? "N" : "S";
  String lonDir = (longitude >= 0) ? "E" : "W";

  Blynk.virtualWrite(V0, String(abs(latitude), 6) + "° " + latDir);
  Blynk.virtualWrite(V2, String(abs(longitude), 6) + "° " + lonDir);
  Blynk.virtualWrite(V5, tiltAngle);
  Blynk.virtualWrite(V6, satelliteCount);

  if (alertActive) {
    unsigned long elapsed = millis() - alertStartTime;
    unsigned long remaining = (elapsed < ALERT_TIME_MS) ? (ALERT_TIME_MS - elapsed) / 1000 : 0;
    Blynk.virtualWrite(V3, "⚠️ ALERT! " + String(remaining) + "s left");
  } else {
    Blynk.virtualWrite(V3, "✅ Vehicle is Safe");
    Blynk.virtualWrite(V4, 0);
  }
}

// ------------------ SERIAL OUTPUT ------------------
void printSerialStatus(bool isAlert, bool isEmergency) {
  Serial.println("------------------------------------------------------------");

  if (isEmergency) {
    Serial.println("🚨 FINAL EMERGENCY ALERT!");
    Serial.println("!!! EMERGENCY CONTACT TRIGGERED !!!");
  } else if (isAlert) {
    unsigned long elapsed = millis() - alertStartTime;
    unsigned long remaining = (elapsed < ALERT_TIME_MS) ? (ALERT_TIME_MS - elapsed) / 1000 : 0;
    Serial.println("🚨 TILT ALERT DETECTED!");
    Serial.print("⏳ Pick it up within ");
    Serial.print(remaining);
    Serial.println(" seconds or emergency will be triggered!");
  } else {
    Serial.println("✅ VEHICLE STATUS: SAFE");
  }

  if (satelliteCount > 0) {
    Serial.println("🌍 GPS Data:");
    char latDir = (latitude >= 0) ? 'N' : 'S';
    char lonDir = (longitude >= 0) ? 'E' : 'W';
    Serial.print("  📍 Latitude  : "); Serial.print(abs(latitude), 6); Serial.print("° "); Serial.println(latDir);
    Serial.print("  📍 Longitude : "); Serial.print(abs(longitude), 6); Serial.print("° "); Serial.println(lonDir);
    Serial.print("  🛰️ Satellites: "); Serial.println(satelliteCount);
  } else {
    Serial.println("⚠️ GPS not fixed yet!");
  }
  Serial.println("------------------------------------------------------------\n");
}

// ------------------ TELEGRAM ALERT FUNCTION ------------------
void sendTelegramAlert(String msg) {
  bot.sendMessage(CHAT_ID1, msg, "Markdown");
  bot.sendMessage(CHAT_ID2, msg, "Markdown");
}

// ------------------ SETUP ------------------
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  if (!mpu.begin()) { Serial.println("❌ MPU6050 not found!"); while (1) delay(10); }
  Serial.println("✅ MPU6050 ready");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  client.setInsecure();
  sendTelegramAlert("✅ Vehicle Tracker online & monitoring!");
  Serial.println("✅ System initialized successfully.");

  blynkTimerID = timer.setInterval(30000L, updateDashboard);
}

// ------------------ MAIN LOOP ------------------
void loop() {
  Blynk.run();
  timer.run();
  readSensors();

  // ------------------ TILT DETECTION ------------------
  if (tiltAngle > TILT_ANGLE_THRESHOLD) {
    if (!alertActive) {
      alertActive = true;
      alertStartTime = millis();
      messageSent = false;
      lastLedToggle = millis();
      Serial.println("🚨 Tilt detected — countdown started!\n");

      timer.deleteTimer(blynkTimerID);
      blynkTimerID = timer.setInterval(1000L, updateDashboard);
    } else {
      unsigned long elapsed = millis() - alertStartTime;

      if (millis() - lastLedToggle > LED_BLINK_INTERVAL) {
        ledState = !ledState;
        Blynk.virtualWrite(V4, ledState ? 255 : 0);
        lastLedToggle = millis();
      }

      if (elapsed >= ALERT_TIME_MS && !messageSent) {
        printSerialStatus(false, true);

        String msg = "🚨 *Vehicle Tilt Detected!*\n";
        msg += "Still tilted after 60s — possible accident.\n";
        msg += "🌍 [Open Location in Google Maps](https://maps.google.com/?q=" 
                + String(latitude,6) + "," + String(longitude,6) + ")";
        sendTelegramAlert(msg);

        Serial.println("📨 Telegram alerts sent to both contacts!");
        messageSent = true;
      } else {
        printSerialStatus(true, false);
      }
    }
  } else {
    if (alertActive) {
      Serial.println("✅ Vehicle stabilized — alert cancelled.\n");

      timer.deleteTimer(blynkTimerID);
      blynkTimerID = timer.setInterval(30000L, updateDashboard);
    }
    alertActive = false;
    messageSent = false;
    Blynk.virtualWrite(V4, 0);
    printSerialStatus(false, false);
  }

  delay(1000);
}
