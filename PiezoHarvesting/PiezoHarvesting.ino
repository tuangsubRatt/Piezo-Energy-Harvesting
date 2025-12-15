#include <Wire.h>
#include <Adafruit_INA219.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "time.h" // สำหรับ Timestamp

// ===========================================
// ** 1. การกำหนดค่า Wi-Fi และ Server **
// ===========================================
const char* ssid = "TR"; 
const char* password = "0968580208"; 

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
const float targetVoltage_V = 2.5; 

float busVoltage_V_last = 0.0;
float current_mA_last = 0.0;
float power_mW_last = 0.0;
String status_last = "INIT";

// *** totalEnergy_Joules คือค่าหลักสำหรับหลอดพลังงาน ***
float totalEnergy_Joules = 0.0;
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
	doc["energy_Joules"] = totalEnergy_Joules;
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

	// 1. อ่านค่าจาก INA219
	busVoltage_V_last = ina219.getBusVoltage_V(); 
	current_mA_last = ina219.getCurrent_mA();
	power_mW_last = ina219.getPower_mW();
	
	unsigned long currentTime = millis();
	float deltaTime_sec = (currentTime - lastMeasureTime) / 1000.0; 
	
	// 2. การคำนวณพลังงานสะสม (Integration of Power) - รวมการชาร์จและการคายประจุ
    
    // คำนวณพลังงานที่เพิ่ม/ลด (Joule) ในช่วงเวลาที่ผ่านมา
    // Power (mW) * Delta_Time (s) / 1000 = Energy (J)
    float energy_delta = (power_mW_last * deltaTime_sec) / 1000.0;
    
    // นำไปบวก/ลบจากพลังงานสะสม (J)
	totalEnergy_Joules += energy_delta; 
	
    // ป้องกันไม่ให้ค่าพลังงานสะสมติดลบ (ถ้าเริ่มต้นที่ 0 J)
    if (totalEnergy_Joules < 0.0) {
        totalEnergy_Joules = 0.0;
    }
    
	lastMeasureTime = currentTime; 

	// 3. ควบคุม LED และกำหนดสถานะ (Logic สำหรับ Web App)
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

	// 4. Serial Print สำหรับ Debug
	const unsigned long printInterval_ms = 500; 
	static unsigned long lastPrintTime = 0;
	
	if (currentTime - lastPrintTime >= printInterval_ms) {
		Serial.print(getTimestamp()); Serial.print(" | "); 
		Serial.print(busVoltage_V_last, 4); Serial.print(" V | ");
		Serial.print(current_mA_last, 4); Serial.print(" mA | ");
		Serial.print(power_mW_last, 4); Serial.print(" mW | "); 
		Serial.print(totalEnergy_Joules, 6); Serial.print(" J | "); 
		Serial.println(status_last);
		lastPrintTime = currentTime;
	}

	delay(10); 
}