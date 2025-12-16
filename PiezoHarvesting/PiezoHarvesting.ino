#include <Wire.h>
#include <Adafruit_INA219.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "time.h" // สำหรับ Timestamp

// ===========================================
// ** 1. การกำหนดค่า Wi-Fi และ Server **
// ===========================================
const char* ssid = "TR"; // <<< แก้ไข: ชื่อ Wi-Fi ของคุณ
const char* password = "0968580208"; // <<< แก้ไข: รหัสผ่าน Wi-Fi ของคุณ

// ===========================================
// ** 2. การกำหนดค่า NTP Time Server **
// ===========================================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600; 	 // GMT+7 (Bangkok/Thailand)
const int daylightOffset_sec = 0;

WebServer server(80); 

// ===========================================
// ** 3. ตัวแปรสำหรับ INA219 และเกณฑ์ **
// ===========================================
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
const int ledPin = 2; 

Adafruit_INA219 ina219;
const float targetVoltage_V = 2.5; // V ที่ใช้ในการกำหนดสถานะ ULTIMATE_READY

// ** NEW CONFIG: ค่าความจุของตัวเก็บประจุ (หน่วยเป็น Farad F) **
// ตัวอย่าง: 470 uF = 0.00047 F 
const float CAPACITOR_C = 0.00047; // <<< สำคัญ: แก้ไขค่านี้ให้ตรงกับตัวเก็บประจุจริง

float busVoltage_V_last = 0.0;
float current_mA_last = 0.0;
float power_mW_last = 0.0;
String status_last = "INIT";

float totalEnergy_Joules = 0.0; // พลังงานสะสมสุทธิ (Total Energy, Pdt)
float potentialEnergy_Joules = 0.0; // พลังงานศักย์ในตัวเก็บประจุ (1/2 * C * V^2)
unsigned long lastMeasureTime = 0; 

// ===========================================
// ** ฟังก์ชัน: จัดการเวลา **
// ===========================================

String getTimestamp() {
	struct tm timeinfo;
	if (!getLocalTime(&timeinfo)) {
		return "Time_Error"; 
	}
	char timeString[20]; 
	strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%S", &timeinfo);
	return String(timeString);
}

// ===========================================
// ** ฟังก์ชัน: Web Server (JSON API) **
// ===========================================

void handleRoot() {
	StaticJsonDocument<400> doc; 
	
	doc["voltage_V"] = busVoltage_V_last; 
	doc["current_mA"] = current_mA_last; 	
	doc["power_mW"] = power_mW_last; 		
	doc["status"] = status_last;
	doc["energy_Joules"] = totalEnergy_Joules; // พลังงานสะสม (Pdt)
	doc["potentialE_Joules"] = potentialEnergy_Joules; // พลังงานศักย์ (1/2CV^2)
    doc["target_V"] = targetVoltage_V; 
	doc["timestamp"] = getTimestamp(); 

	String jsonResponse;
	serializeJson(doc, jsonResponse);

	server.sendHeader("Access-Control-Allow-Origin", "*"); 
	server.send(200, "application/json", jsonResponse);
}

// ===========================================
// ** SETUP **
// ===========================================
void setup() {
	Serial.begin(115200);
	
	pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, LOW); 

	// --- 1. เริ่มต้น I2C และ INA219 ---
	Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
	if (!ina219.begin()) {
		Serial.println("--- ERROR: Failed to find INA219 chip! ---");
		while (1); 
	}
	ina219.setCalibration_32V_2A(); 
	
	// --- 2. เชื่อมต่อ Wi-Fi ---
	WiFi.begin(ssid, password);
	Serial.print("Connecting to WiFi");
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	Serial.println("\nWiFi connected.");
	Serial.print("ESP32 IP Address: ");
	Serial.println(WiFi.localIP());

	// --- 3. ตั้งค่าเวลา NTP ---
	configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
	Serial.print("Current time: ");
	Serial.println(getTimestamp());
	
	// --- 4. ตั้งค่า Web Server ---
	server.on("/", handleRoot); 
	server.begin(); 
	lastMeasureTime = millis(); 
	
	Serial.println("==========================================");
	Serial.println("System Ready. Local API is running.");
}

// ===========================================
// ** LOOP **
// ===========================================
void loop() {
	server.handleClient(); 

	unsigned long currentTime = millis();
    
	// 1. อ่านค่าจาก INA219
	busVoltage_V_last = ina219.getBusVoltage_V(); 
	current_mA_last = ina219.getCurrent_mA();
	power_mW_last = ina219.getPower_mW();
	
	float deltaTime_sec = (currentTime - lastMeasureTime) / 1000.0; 
	
	// 2. การคำนวณพลังงานสะสม (E = Integral P dt)
    float energy_delta = (power_mW_last * deltaTime_sec) / 1000.0;
	totalEnergy_Joules += energy_delta; 
	
    if (totalEnergy_Joules < 0.0) {
        totalEnergy_Joules = 0.0;
    }
    
	// ** 3. การคำนวณพลังงานศักย์ในตัวเก็บประจุ (E = 1/2 * C * V^2) **
    potentialEnergy_Joules = 0.5 * CAPACITOR_C * busVoltage_V_last * busVoltage_V_last;

	lastMeasureTime = currentTime; 

	// 4. ควบคุม LED และกำหนดสถานะ
	if (busVoltage_V_last >= targetVoltage_V) {
		digitalWrite(ledPin, HIGH); 
		status_last = "ULTIMATE_READY";
	} else {
		digitalWrite(ledPin, LOW); 	
		
		if (current_mA_last > 0.5) {
			status_last = "CHARGING";
		} else if (current_mA_last < -0.5) {
			status_last = "DISCHARGING";
		} else {
			status_last = "IDLE";
		}
	}

	// 5. Serial Print สำหรับ Debug (แสดงค่าใหม่ด้วย)
	const unsigned long printInterval_ms = 500; 
	static unsigned long lastPrintTime = 0;
	
	if (currentTime - lastPrintTime >= printInterval_ms) {
		Serial.print(getTimestamp()); Serial.print(" | V:"); 
		Serial.print(busVoltage_V_last, 4); Serial.print(" | mA:");
		Serial.print(current_mA_last, 4); Serial.print(" | P(mW):");
		Serial.print(power_mW_last, 4); Serial.print(" | E(Pdt):"); 
		Serial.print(totalEnergy_Joules, 6); Serial.print(" | E(1/2CV^2):");
        Serial.print(potentialEnergy_Joules, 6); Serial.print(" | STATUS:");
		Serial.println(status_last);
		lastPrintTime = currentTime;
	}

	delay(10); 
}