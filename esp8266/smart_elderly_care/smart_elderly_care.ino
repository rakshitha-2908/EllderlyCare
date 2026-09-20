/*
  SmartElderlyCare - ESP8266 + MPU6050 -> FastAPI

  BEFORE FLASHING:
  1. iPhone: Settings -> Personal Hotspot -> turn ON "Maximize Compatibility".
     The ESP8266 has no 5 GHz radio, so without this it cannot see the hotspot.
  2. Connect your LAPTOP to the same iPhone hotspot.
  3. Laptop and ESP must be on the SAME Wi-Fi.
  4. Run the server so the board can reach it (port 8000 is taken by Colima):
        uvicorn main:app --host 0.0.0.0 --port 8001 --app-dir backend
  5. Set SERVER_IPS[] below to this laptop's IPv4 on that Wi-Fi.
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <math.h>

// =============================
// Wi-Fi
// =============================

const char* WIFI_SSID     = "iPhone (2)";
const char* WIFI_PASSWORD = "viksa726";

const unsigned long WIFI_CONNECT_TIMEOUT = 20000;  // give up after 20 s
const unsigned long WIFI_RETRY_INTERVAL  = 5000;   // retry every 5 s in loop()

// =============================
// LAPTOP / FASTAPI SERVER
// =============================

// Laptop IPv4 addresses the ESP will try (same Wi-Fi as this board).
// Current campus Wi-Fi GHS-303 laptop IP is 192.168.0.101.
// iPhone hotspot laptop IP is usually 172.20.10.2.
const char* SERVER_IPS[] = {
  "192.168.0.101",
  "172.20.10.2",
};
const int SERVER_IP_COUNT = 2;
int currentServer = 0;

const int SERVER_PORT = 8001;

const int HTTP_TIMEOUT_MS = 3000;   // don't let a dead server stall the loop

// =============================
// MPU6050
// =============================

#define MPU_ADDR 0x68
#define SDA_PIN  D2
#define SCL_PIN  D1

// =============================
// FALL DETECTION
// =============================

// A real fall has two parts:
//   1. free fall  -> total acceleration DIPS below ~0.5 g
//   2. impact     -> total acceleration SPIKES above ~2.5 g shortly after
// At rest the total reads 1.0 g (gravity), so a single ">2.2 g" test
// fires on ordinary arm movement.

const float FREEFALL_THRESHOLD = 0.80;   // dip below this counts as free-fall
const float IMPACT_THRESHOLD   = 1.60;   // spike after the dip counts as impact

const unsigned long FREEFALL_WINDOW = 1000;   // impact must follow within 1 s
const unsigned long FALL_COOLDOWN   = 10000;  // ignore new falls for 10 s

bool          inFreefall   = false;
unsigned long freefallTime = 0;
unsigned long lastFallTime = 0;

// Latched flag: set when a fall is detected, cleared only after it is sent.
bool pendingFall = false;

// =============================
// TIMING
// =============================

unsigned long lastSendTime  = 0;
const unsigned long SEND_INTERVAL = 1000;   // send sensor data every 1 s

unsigned long lastWifiCheck = 0;

bool mpuOK = false;

// =============================
// SENSOR DATA
// =============================

struct SensorData
{
  float accelX, accelY, accelZ;
  float gyroX,  gyroY,  gyroZ;
  float total;
};


// =====================================================
// MPU6050 FUNCTIONS
// =====================================================

void writeMPU(byte reg, byte value)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}


bool mpuPresent()
{
  Wire.beginTransmission(MPU_ADDR);
  return (Wire.endTransmission() == 0);
}


bool readMPU(SensorData &d)
{
  // Point the MPU at register 0x3B (first accelerometer byte).
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);

  if (Wire.endTransmission(false) != 0)
  {
    return false;
  }

  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14)
  {
    return false;
  }

  // Read all 14 bytes into a buffer FIRST.
  // Writing "Wire.read() << 8 | Wire.read()" is a real bug: C++ does not
  // define which of the two read() calls runs first, so the compiler may
  // swap the high and low bytes and produce garbage.
  uint8_t buf[14];

  for (int i = 0; i < 14; i++)
  {
    buf[i] = Wire.read();
  }

  int16_t accelXRaw = (int16_t)((buf[0]  << 8) | buf[1]);
  int16_t accelYRaw = (int16_t)((buf[2]  << 8) | buf[3]);
  int16_t accelZRaw = (int16_t)((buf[4]  << 8) | buf[5]);
  // buf[6], buf[7] = temperature (unused)
  int16_t gyroXRaw  = (int16_t)((buf[8]  << 8) | buf[9]);
  int16_t gyroYRaw  = (int16_t)((buf[10] << 8) | buf[11]);
  int16_t gyroZRaw  = (int16_t)((buf[12] << 8) | buf[13]);

  // 16384 LSB/g at the default +/-2 g range
  d.accelX = accelXRaw / 16384.0;
  d.accelY = accelYRaw / 16384.0;
  d.accelZ = accelZRaw / 16384.0;

  // 131 LSB/(deg/s) at the default +/-250 dps range
  d.gyroX = gyroXRaw / 131.0;
  d.gyroY = gyroYRaw / 131.0;
  d.gyroZ = gyroZRaw / 131.0;

  d.total = sqrt(d.accelX * d.accelX +
                 d.accelY * d.accelY +
                 d.accelZ * d.accelZ);

  return true;
}


// =====================================================
// WI-FI
// =====================================================

bool connectWiFi(unsigned long timeoutMs)
{
  WiFi.mode(WIFI_STA);                  // client only (default is AP+STA)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);   // stop modem-sleep dropouts
  WiFi.setAutoReconnect(true);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - start > timeoutMs)
    {
      return false;
    }

    delay(250);
    yield();                            // feed the watchdog
    Serial.print(".");
  }

  Serial.println();
  return true;
}


void reportWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Wi-Fi connected!");

    Serial.print("  ESP8266 IP : ");
    Serial.println(WiFi.localIP());

    Serial.print("  Gateway    : ");
    Serial.println(WiFi.gatewayIP());

    Serial.print("  Signal     : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.print("  Server     : ");
    Serial.print(SERVER_IPS[currentServer]);
    Serial.print(":");
    Serial.println(SERVER_PORT);
  }
  else
  {
    Serial.print("Wi-Fi FAILED. status = ");
    Serial.println(WiFi.status());
    Serial.println("  1 = network name not found (5 GHz? hotspot off? typo?)");
    Serial.println("  4 = wrong password");
    Serial.println("Sensors will still run; Wi-Fi retries in the background.");
  }
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" SmartElderlyCare ESP8266");
  Serial.println("==============================");

  // -----------------------------
  // I2C
  // -----------------------------

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  Serial.println("I2C initialized.");

  // -----------------------------
  // MPU6050
  // -----------------------------

  for (int attempt = 1; attempt <= 5 && !mpuOK; attempt++)
  {
    mpuOK = mpuPresent();

    if (!mpuOK)
    {
      Serial.print("MPU6050 not found, attempt ");
      Serial.print(attempt);
      Serial.println("/5 ...");
      delay(500);
    }
  }

  if (mpuOK)
  {
    writeMPU(0x6B, 0x00);      // clear sleep bit, wake the sensor
    delay(100);
    Serial.println("MPU6050 detected and awakened.");
  }
  else
  {
    Serial.println("ERROR: MPU6050 not responding. Check SDA=D2, SCL=D1, 3V3, GND.");
  }

  // -----------------------------
  // Wi-Fi
  // -----------------------------

  Serial.println();
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  connectWiFi(WIFI_CONNECT_TIMEOUT);
  reportWiFi();

  Serial.println();
  Serial.println("Starting sensor monitoring...");
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
  // ---------------------------------------------------
  // Keep Wi-Fi alive (non-blocking)
  // ---------------------------------------------------

  if (millis() - lastWifiCheck >= WIFI_RETRY_INTERVAL)
  {
    lastWifiCheck = millis();

    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Wi-Fi down, reconnecting...");
      WiFi.reconnect();
    }
  }

  // ---------------------------------------------------
  // Read MPU6050
  // ---------------------------------------------------

  SensorData data;

  if (!readMPU(data))
  {
    Serial.println("Failed to read MPU6050.");
    delay(1000);
    return;
  }

  // ---------------------------------------------------
  // Two-stage fall detection
  // ---------------------------------------------------

  unsigned long now = millis();

  // Stage 1: free fall - the body is falling, so measured acceleration
  // drops towards zero.
  if (!inFreefall && data.total < FREEFALL_THRESHOLD)
  {
    inFreefall   = true;
    freefallTime = now;
  }

  // The free-fall flag expires if no impact follows in time.
  if (inFreefall && (now - freefallTime > FREEFALL_WINDOW))
  {
    inFreefall = false;
  }

  // Stage 2: impact - a hard spike shortly after the free fall.
  if (inFreefall &&
      data.total > IMPACT_THRESHOLD &&
      (lastFallTime == 0 || now - lastFallTime > FALL_COOLDOWN))
  {
    inFreefall   = false;
    lastFallTime = now;

    pendingFall = true;      // latched until it is actually sent

    Serial.println();
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("      FALL DETECTED");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
  }

  // ---------------------------------------------------
  // Print values
  // ---------------------------------------------------

  Serial.println("--------------------------------");

  Serial.print("Acceleration: ");
  Serial.print(data.accelX, 2); Serial.print(", ");
  Serial.print(data.accelY, 2); Serial.print(", ");
  Serial.print(data.accelZ, 2);
  Serial.println(" g");

  Serial.print("Total Acceleration: ");
  Serial.print(data.total, 2);
  Serial.println(" g");

  Serial.print("Gyroscope: ");
  Serial.print(data.gyroX, 2); Serial.print(", ");
  Serial.print(data.gyroY, 2); Serial.print(", ");
  Serial.print(data.gyroZ, 2);
  Serial.println(" dps");

  // ---------------------------------------------------
  // Send data to FastAPI every second
  // ---------------------------------------------------

  // Send on the 1 s cadence, or immediately when a fall is latched.
  if (pendingFall || millis() - lastSendTime >= SEND_INTERVAL)
  {
    lastSendTime = millis();

    if (sendSensorData(data, pendingFall))
    {
      pendingFall = false;   // only clear once the server has it
    }
  }

  delay(200);
}


// =====================================================
// SEND DATA TO FASTAPI
// =====================================================

bool sendSensorData(const SensorData &d, bool fallDetected)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Wi-Fi disconnected, skipping send.");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String json = "{";
  json += "\"device_id\":\"elderly-device-01\",";
  json += "\"accel_x\":";            json += String(d.accelX, 3); json += ",";
  json += "\"accel_y\":";            json += String(d.accelY, 3); json += ",";
  json += "\"accel_z\":";            json += String(d.accelZ, 3); json += ",";
  json += "\"gyro_x\":";             json += String(d.gyroX, 3);  json += ",";
  json += "\"gyro_y\":";             json += String(d.gyroY, 3);  json += ",";
  json += "\"gyro_z\":";             json += String(d.gyroZ, 3);  json += ",";
  json += "\"total_acceleration\":"; json += String(d.total, 3);  json += ",";
  json += "\"fall_detected\":";      json += fallDetected ? "true" : "false";
  json += "}";

  Serial.println("JSON:");
  Serial.println(json);

  for (int attempt = 0; attempt < SERVER_IP_COUNT; attempt++)
  {
    int idx = (currentServer + attempt) % SERVER_IP_COUNT;
    String url = String("http://") + SERVER_IPS[idx] + ":" +
                 String(SERVER_PORT) + "/sensor";

    Serial.print("Sending data to: ");
    Serial.println(url);

    if (!http.begin(client, url))
    {
      Serial.println("HTTP connection failed.");
      http.end();
      continue;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(json);

    Serial.print("HTTP Response: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode >= 200 && httpResponseCode < 300)
    {
      currentServer = idx;
      String response = http.getString();
      Serial.println("Server response:");
      Serial.println(response);
      http.end();
      return true;
    }

    if (httpResponseCode > 0)
    {
      Serial.println(http.getString());
    }
    else
    {
      Serial.print("HTTP error: ");
      Serial.println(http.errorToString(httpResponseCode));
      Serial.println("  -> uvicorn --host 0.0.0.0 --port 8001 --app-dir backend");
      Serial.println("  -> Laptop and ESP must be on the same Wi-Fi");
    }

    http.end();
  }

  return false;
}
