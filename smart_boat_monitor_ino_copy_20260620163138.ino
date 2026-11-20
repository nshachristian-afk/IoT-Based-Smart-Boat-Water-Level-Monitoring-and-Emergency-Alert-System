// ============================================================
// SMART BOAT WATER LEVEL MONITORING SYSTEM
// ESP32-WROOM-32 (DevKit V1)
// PUMP ACTIVATES AT WARNING LEVEL (30%)
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>

// ============================================================
// CONFIGURATION - UPDATE THESE VALUES!
// ============================================================

// ----- WiFi Configuration -----
const char* ssid = "iPhone 12 mini";        // Your WiFi name
const char* password = "Gustave1856";                // Your WiFi password

// ----- Server Configuration -----
const char* serverIP = "172.20.10.2";             // Your computer's IP address
const int serverPort = 80;                          // Default HTTP port
const char* apiEndpoint = "/smart_boat_pro/api/receive_data.php";
// ============================================================
// PIN DEFINITIONS FOR ESP32-WROOM-32 (DevKit V1)
// ============================================================

// ----- Analog Sensors -----
const int WATER_SENSOR_PIN = 34;      // ADC1_CH6 (GPIO34) - Input only

// ----- Outputs -----
const int PUMP_RELAY_PIN = 26;        // GPIO26 - Relay control
const int BUZZER_PIN = 27;            // GPIO27 - Buzzer
const int LED_GREEN = 25;             // GPIO25 - Green LED (Safe)
const int LED_YELLOW = 33;            // GPIO33 - Yellow LED (Warning)
const int LED_RED = 32;               // GPIO32 - Red LED (Critical)

// ----- GPS (SoftwareSerial) -----
const int GPS_RX_PIN = 4;             // GPIO4 - RX (connect to GPS TX)
const int GPS_TX_PIN = 5;             // GPIO5 - TX (connect to GPS RX)

// ----- Built-in LED -----
const int LED_BUILTIN = 2;            // Built-in LED on ESP32

// ============================================================
// THRESHOLDS - PUMP ACTIVATES AT WARNING (30%)
// ============================================================
const float WARNING_THRESHOLD = 30.0;   // Warning level (%) - PUMP TURNS ON HERE
const float CRITICAL_THRESHOLD = 40.0;  // Critical level (%)
const float PUMP_OFF_THRESHOLD = 25.0;  // Pump turns OFF below this (hysteresis)

// ----- Timing -----
const unsigned long SEND_INTERVAL = 5000;     // Send data every 5 seconds
const unsigned long MONITOR_INTERVAL = 1000;  // Monitor every 1 second
const unsigned long SERIAL_BAUD = 115200;     // Serial baud rate
const unsigned long GPS_BAUD = 9600;          // GPS baud rate
const unsigned long BLINK_INTERVAL = 1000;    // Built-in LED blink rate

// ============================================================
// GLOBAL VARIABLES
// ============================================================

// GPS
TinyGPSPlus gps;
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);

// Sensor readings
float waterLevel = 0.0;
float batteryLevel = 100.0;
float temperature = 25.0;
int signalStrength = 100;

// GPS data
double latitude = -1.7024;     // Default to Lake Kivu, Rwanda
double longitude = 29.2568;
double altitude = 0.0;
float speed = 0.0;

// Status
String boatStatus = "SAFE";
String pumpStatus = "OFF";
bool pumpActive = false;

// Timing
unsigned long lastSendTime = 0;
unsigned long lastMonitorTime = 0;
unsigned long lastBlinkTime = 0;
bool ledState = false;

// WiFi
WiFiClient client;
HTTPClient http;

// ============================================================
// SETUP FUNCTION (Required)
// ============================================================
void setup() {
    // Initialize Serial
    Serial.begin(SERIAL_BAUD);
    
    // Initialize GPS Serial
    gpsSerial.begin(GPS_BAUD);
    
    Serial.println("\n===========================================");
    Serial.println("🚤 SMART BOAT MONITORING SYSTEM");
    Serial.println("   ESP32-WROOM-32 (DevKit V1)");
    Serial.println("===========================================");
    Serial.println("📍 GPS: GPIO4 (RX) and GPIO5 (TX)");
    Serial.println("💧 Water Sensor: GPIO34");
    Serial.println("🔄 Relay (Pump): GPIO26");
    Serial.println("🔊 Buzzer: GPIO27");
    Serial.println("🟢 Green LED: GPIO25");
    Serial.println("🟡 Yellow LED: GPIO33");
    Serial.println("🔴 Red LED: GPIO32");
    Serial.println("🔵 Built-in LED: GPIO2");
    Serial.println("===========================================");
    Serial.println("⚙️ PUMP ACTIVATES AT: " + String(WARNING_THRESHOLD) + "% (WARNING)");
    Serial.println("⚙️ PUMP TURNS OFF BELOW: " + String(PUMP_OFF_THRESHOLD) + "%");
    Serial.println("===========================================");
    
    // Initialize pins
    pinMode(WATER_SENSOR_PIN, INPUT);
    pinMode(PUMP_RELAY_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_YELLOW, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    
    // Initial state
    digitalWrite(PUMP_RELAY_PIN, HIGH);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_BUILTIN, LOW);
    
    // Connect to WiFi
    connectToWiFi();
    
    Serial.println("✅ System initialized!");
    Serial.println("📊 Waiting for sensor data...");
    Serial.println("===========================================");
}

// ============================================================
// LOOP FUNCTION (Required)
// ============================================================
void loop() {
    unsigned long currentMillis = millis();
    
    // 1. Blink built-in LED (shows system is alive)
    if (currentMillis - lastBlinkTime >= BLINK_INTERVAL) {
        lastBlinkTime = currentMillis;
        ledState = !ledState;
        digitalWrite(LED_BUILTIN, ledState);
    }
    
    // 2. Read sensors
    if (currentMillis - lastMonitorTime >= MONITOR_INTERVAL) {
        lastMonitorTime = currentMillis;
        readSensors();
        checkThresholds();
        updateStatusLights();
        handlePump();  // PUMP NOW ACTIVATES AT WARNING LEVEL
        printStatus();
    }
    
    // 3. Read GPS
    readGPS();
    
    // 4. Send data to server
    if (currentMillis - lastSendTime >= SEND_INTERVAL) {
        lastSendTime = currentMillis;
        sendDataToServer();
    }
    
    // 5. Handle WiFi reconnection
    if (WiFi.status() != WL_CONNECTED) {
        connectToWiFi();
    }
    
    delay(100);
}

// ============================================================
// FUNCTION: Connect to WiFi
// ============================================================
void connectToWiFi() {
    Serial.print("📡 Connecting to WiFi: ");
    Serial.println(ssid);
    
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi connected!");
        Serial.print("📡 IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("📡 Signal Strength: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
    } else {
        Serial.println("\n❌ WiFi connection failed!");
        Serial.println("📡 Please check WiFi credentials and try again.");
    }
}

// ============================================================
// FUNCTION: Read Sensors
// ============================================================
void readSensors() {
    // Read water level (0-4095 for ESP32)
    int sensorValue = analogRead(WATER_SENSOR_PIN);
    
    // Convert to percentage (calibrate as needed)
    waterLevel = map(sensorValue, 0, 4095, 0, 100);
    waterLevel = constrain(waterLevel, 0, 100);
    
    // Simulate temperature (in real project, use DS18B20 or DHT11)
    temperature = 25.0 + (random(0, 100) / 100.0) * 5.0;
    
    // Simulate battery level (use voltage divider in real)
    static float simulatedBattery = 100.0;
    simulatedBattery -= 0.01;
    if (simulatedBattery < 10.0) simulatedBattery = 100.0;
    batteryLevel = simulatedBattery;
    
    // Signal strength (WiFi RSSI)
    signalStrength = WiFi.RSSI();
    signalStrength = constrain(map(signalStrength, -100, -30, 0, 100), 0, 100);
}

// ============================================================
// FUNCTION: Read GPS
// ============================================================
void readGPS() {
    while (gpsSerial.available() > 0) {
        char c = gpsSerial.read();
        if (gps.encode(c)) {
            if (gps.location.isValid()) {
                latitude = gps.location.lat();
                longitude = gps.location.lng();
            }
            if (gps.altitude.isValid()) {
                altitude = gps.altitude.meters();
            }
            if (gps.speed.isValid()) {
                speed = gps.speed.kmph();
            }
        }
    }
    
    // If GPS not fixed, use default coordinates (Lake Kivu, Rwanda)
    if (!gps.location.isValid()) {
        latitude = -1.7024;
        longitude = 29.2568;
    }
}

// ============================================================
// FUNCTION: Check Thresholds - MODIFIED
// ============================================================
void checkThresholds() {
    // ============================================================
    // MODIFIED: Pump activates at WARNING level (30%)
    // ============================================================
    
    // Determine boat status
    if (waterLevel > CRITICAL_THRESHOLD) {
        boatStatus = "CRITICAL";
    } else if (waterLevel > WARNING_THRESHOLD) {
        boatStatus = "WARNING";
    } else {
        boatStatus = "SAFE";
    }
    
    // ============================================================
    // PUMP CONTROL WITH HYSTERESIS
    // ============================================================
    // Pump turns ON when water level > WARNING_THRESHOLD (30%)
    // Pump turns OFF when water level drops below PUMP_OFF_THRESHOLD (25%)
    // This prevents rapid on/off cycling
    
    if (waterLevel > WARNING_THRESHOLD) {
        pumpActive = true;   // Turn pump ON
    } else if (waterLevel <= PUMP_OFF_THRESHOLD) {
        pumpActive = false;  // Turn pump OFF
    }
    // If waterLevel is between 25% and 30%, pump stays in its current state
}

// ============================================================
// FUNCTION: Update Status Lights
// ============================================================
void updateStatusLights() {
    if (boatStatus == "CRITICAL") {
        digitalWrite(LED_RED, HIGH);
        digitalWrite(LED_YELLOW, LOW);
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(BUZZER_PIN, HIGH);
    } else if (boatStatus == "WARNING") {
        digitalWrite(LED_RED, LOW);
        digitalWrite(LED_YELLOW, HIGH);
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(BUZZER_PIN, LOW);
        // Blink yellow for warning
        if ((millis() / 500) % 2 == 0) {
            digitalWrite(LED_YELLOW, HIGH);
        } else {
            digitalWrite(LED_YELLOW, LOW);
        }
    } else {
        digitalWrite(LED_RED, LOW);
        digitalWrite(LED_YELLOW, LOW);
        digitalWrite(LED_GREEN, HIGH);
        digitalWrite(BUZZER_PIN, LOW);
    }
}

// ============================================================
// FUNCTION: Handle Pump - MODIFIED
// ============================================================
void handlePump() {
    if (pumpActive) {
        digitalWrite(PUMP_RELAY_PIN, LOW);
        pumpStatus = "ON";
    } else {
        digitalWrite(PUMP_RELAY_PIN, HIGH);
        pumpStatus = "OFF";
    }
}

// ============================================================
// FUNCTION: Send Data to Server
// ============================================================
void sendDataToServer() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi disconnected, reconnecting...");
        connectToWiFi();
        return;
    }
    
    // Build URL
    String url = "http://";
    url += serverIP;
    url += ":";
    url += serverPort;
    url += apiEndpoint;
    
    // Build POST data
    String postData = "water_level=" + String(waterLevel);
    postData += "&status=" + boatStatus;
    postData += "&pump_status=" + pumpStatus;
    postData += "&latitude=" + String(latitude, 6);
    postData += "&longitude=" + String(longitude, 6);
    postData += "&battery_level=" + String(batteryLevel);
    postData += "&signal_strength=" + String(signalStrength);
    postData += "&temperature=" + String(temperature);
    
    Serial.println("\n📤 Sending data to server...");
    Serial.println("📊 Water Level: " + String(waterLevel) + "%");
    Serial.println("📊 Status: " + boatStatus);
    Serial.println("📊 Pump: " + pumpStatus + " ⚙️ " + (pumpActive ? "RUNNING" : "IDLE"));
    Serial.println("📊 GPS: " + String(latitude, 6) + ", " + String(longitude, 6));
    Serial.println("📊 Battery: " + String(batteryLevel) + "%");
    Serial.println("📊 Signal: " + String(signalStrength) + "%");
    Serial.println("📊 Temperature: " + String(temperature) + "°C");
    
    // Send HTTP POST
    http.begin(client, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    int httpCode = http.POST(postData);
    String response = http.getString();
    
    if (httpCode > 0) {
        Serial.println("✅ HTTP Response Code: " + String(httpCode));
        if (httpCode == 200) {
            Serial.println("✅ Data sent successfully!");
        }
        Serial.println("📥 Response: " + response);
    } else {
        Serial.println("❌ HTTP Error: " + String(http.errorToString(httpCode)));
        Serial.println("❌ Please check server connection");
    }
    
    http.end();
}

// ============================================================
// FUNCTION: Print Status
// ============================================================
void printStatus() {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint < 5000) return;
    lastPrint = millis();
    
    Serial.println("\n===========================================");
    Serial.println("📊 SYSTEM STATUS");
    Serial.println("===========================================");
    Serial.println("💧 Water Level: " + String(waterLevel) + "%");
    Serial.println("🚩 Status: " + boatStatus);
    Serial.println("🔄 Pump: " + pumpStatus + " " + (pumpActive ? "🟢 RUNNING" : "🔴 OFF"));
    Serial.println("📍 GPS: " + String(latitude, 6) + ", " + String(longitude, 6));
    Serial.println("🔋 Battery: " + String(batteryLevel) + "%");
    Serial.println("📡 Signal: " + String(signalStrength) + "%");
    Serial.println("🌡️ Temperature: " + String(temperature) + "°C");
    Serial.println("===========================================");
}

// ============================================================
// DEBUG FUNCTIONS
// ============================================================

// Print GPS data
void printGPSData() {
    Serial.println("📍 GPS Data:");
    Serial.println("  Latitude: " + String(latitude, 6));
    Serial.println("  Longitude: " + String(longitude, 6));
    Serial.println("  Altitude: " + String(altitude, 1) + " m");
    Serial.println("  Speed: " + String(speed, 1) + " km/h");
    Serial.println("  Satellites: " + String(gps.satellites.value()));
    Serial.println("  HDOP: " + String(gps.hdop.value()));
}

// Print WiFi status
void printWiFiStatus() {
    Serial.println("📡 WiFi Status:");
    Serial.println("  SSID: " + String(ssid));
    Serial.println("  IP: " + WiFi.localIP().toString());
    Serial.println("  RSSI: " + String(WiFi.RSSI()) + " dBm");
    Serial.println("  Status: " + String(WiFi.status()));
}

// ============================================================
// CONFIGURATION FUNCTIONS
// ============================================================

// Set thresholds (can be called from server)
void setThresholds(float warning, float critical) {
    // WARNING_THRESHOLD = warning;
    // CRITICAL_THRESHOLD = critical;
    Serial.println("⚙️ Thresholds updated!");
}

// Set pump control (manual override)
void setPump(bool state) {
    pumpActive = state;
    pumpStatus = state ? "ON" : "OFF";
    digitalWrite(PUMP_RELAY_PIN, state ? HIGH : LOW);
    Serial.println("🔄 Pump manually " + pumpStatus);
}

// ============================================================
// END OF CODE
// ============================================================