/*
 * FIRMWARE: SPRCP MONITORING SYSTEM
 * VERSION: v24.4 (STABLE RELEASE)
 * PLATFORM: ESP32 DEVKIT V1
 * ---------------------------------------------------------------------------
 * DESCRIPTION:
 * Sistem akuisisi data presisi tinggi untuk monitoring potensial korosi
 * pada beton bertulang. Mendukung metode SACP (Phase 1 & 2) dan HCP Natural.
 * Menggunakan ADC 16-bit dengan kompensasi kalibrasi linear.
 * ---------------------------------------------------------------------------
 * DEVELOPER: Lead Research Developer
 */

#include <Wire.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_INA219.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <time.h>
#include <Preferences.h> 

// ==========================================
// 1. NETWORK & CREDENTIALS
// ==========================================
#define WIFI_SSID "NAMA_WIFI_ANDA"       
#define WIFI_PASSWORD "PASSWORD_WIFI"    
#define API_KEY "FIREBASE_API_KEY_ANDA"
#define DATABASE_URL "URL_DATABASE_ANDA"

// ==========================================
// 2. HARDWARE PIN MAPPING
// ==========================================
// Multiplexer Control
#define PIN_EN_MUX1  17  
#define PIN_EN_MUX2  16  
#define MUX_S0 23
#define MUX_S1 19
#define MUX_S2 18
#define MUX_S3 5

// Relay Actuators (Active Low)
const int RELAY_PINS[8] = {13, 12, 14, 27, 26, 25, 33, 32};
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// Sensors & UI
#define ONE_WIRE_BUS 15 
#define BTN_OK   35 
#define BTN_UP   34 
#define BTN_DOWN 39 
#define BTN_BACK 36 

// ==========================================
// 3. SYSTEM CONSTANTS & PATHS
// ==========================================
const String basePath = "/artifacts/korosi-iot/public/data";
const String pathLogs = basePath + "/logs";
const String pathCurrentLogs = basePath + "/current_logs";
const String pathConfig = basePath + "/research_config";
const String pathCalib = basePath + "/kalibrasi"; 
const String pathCheck = basePath + "/completion_map";
const String pathStatus = basePath + "/status_global";
const char* const BU_NAMES[] = {"M1", "M4", "M10", "M14", "MK22", "MK21", "MK18", "MK17"};

// ==========================================
// 4. DATA STRUCTURES
// ==========================================
// Struktur Kalibrasi Instrumen (Linear Regression Coefficient)
// Digunakan untuk mengkompensasi toleransi komponen elektronik
struct CalibData { 
  float a; // Quadratic term (biasanya 0 untuk linear)
  float b; // Slope/Gain correction
  float c; // Intercept/Offset hardware
};
CalibData calibs[32]; 

// Objects
Adafruit_ADS1115 ads;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_INA219 ina219;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
FirebaseData fbdo; FirebaseAuth auth; FirebaseConfig config;
Preferences preferences; 

// Parameters
#define WINDOW_SIZE 128     // Sample averaging size for noise reduction
#define SETTLING_DELAY_MS 1000 
#define CURRENT_SAMPLES 30 

// State Management
bool signupOK = false;
bool isMeasuring = false;
int menuState = 0;
int selectedMode = 1;
int selectedBU_Idx = 0;
int selectedTul = 1;
int selectedPoint = 1;
bool allProtectionOn = true;
unsigned long lastLogTimestamp = 0; 
unsigned long lastWiFiCheckMillis = 0;
long startTimestamp = 0;

// ==========================================
// 5. UTILITY FUNCTIONS
// ==========================================

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startWait = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) { delay(100); }
  }
}

int calculateDayCount() {
  if (startTimestamp <= 0) return 1;
  time_t now; time(&now);
  long diffSeconds = (long)now - startTimestamp;
  if (diffSeconds < 0) return 1;
  return (diffSeconds / 86400) + 1;
}

String getWaktuLokal() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "N/A";
  char buf[25]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

// I2C Multiplexer Helper (TCA9548A if used, else standard Mux)
void tcaSelect(uint8_t i) { 
  Wire.beginTransmission(0x70); Wire.write(1 << i); Wire.endTransmission(); 
}

void updateSystemStatus() {
  if (WiFi.status() != WL_CONNECTED || !signupOK) return;
  time_t now; time(&now); 
  FirebaseJson json; 
  json.set("status", allProtectionOn ? "ACTIVE" : "IDLE");
  json.set("firmware", "v24.4-STABLE");
  json.set("last_update", getWaktuLokal());
  json.set("uptime_ms", (double)millis());
  Firebase.RTDB.setJSON(&fbdo, pathStatus, &json);
}

// ==========================================
// 6. CORE MEASUREMENT ENGINE
// ==========================================

// Akuisisi Data ADC dengan Kompensasi Kalibrasi
float acquireVoltage() {
    double totalRaw = 0;
    // Oversampling loop untuk meningkatkan stabilitas pembacaan
    for(int i=0; i<WINDOW_SIZE; i++) { 
      totalRaw += ads.readADC_Differential_0_1(); 
      delayMicroseconds(50); 
    }
    
    // Konversi Raw ADC ke Tegangan (Gain 2/3: +/- 6.144V -> 0.1875mV/bit)
    float avgRaw = totalRaw / WINDOW_SIZE;
    float voltage_mv = (avgRaw * 0.1875F);

    // Penerapan Kalibrasi Instrumen (y = ax^2 + bx + c)
    // Mengoreksi deviasi pembacaan akibat toleransi resistor/kabel
    int baseIndex = (selectedMode == 3) ? 16 : 0;
    int finalIndex = baseIndex + (selectedBU_Idx * 2) + (selectedTul - 1);
    
    float a = calibs[finalIndex].a; 
    float b = calibs[finalIndex].b; 
    float c = calibs[finalIndex].c;

    float corrected_mv = (a * pow(voltage_mv, 2)) + (b * voltage_mv) + c;
    return corrected_mv;
}

// Pembacaan Arus High-Side
float acquireCurrent() {
    float maxCurrent_mA = 0.0;
    // Peak detection untuk menangkap arus stabil
    for(int i=0; i<CURRENT_SAMPLES; i++) {
      float val_mV = ina219.getShuntVoltage_mV();
      // Konversi shunt voltage ke mA (Rshunt = 0.1 Ohm assumed inside lib or calibrated)
      // Menggunakan pembacaan langsung dari library yang sudah terkalibrasi
      float val_mA = ina219.getCurrent_mA();
      if (fabs(val_mA) > fabs(maxCurrent_mA)) maxCurrent_mA = val_mA; 
      delay(5);
    }
    return fabs(maxCurrent_mA);
}

void syncConfiguration() {
  if (WiFi.status() != WL_CONNECTED || !signupOK) return;
  display.clearDisplay(); display.setCursor(0,0); display.println("Syncing Data..."); display.display();
  
  // Sinkronisasi Start Date Riset
  if (Firebase.RTDB.getJSON(&fbdo, pathConfig)) { 
    FirebaseJsonData jd; FirebaseJson &json = fbdo.jsonObject(); 
    if (json.get(jd, "start_timestamp")) startTimestamp = (long)jd.intValue; 
  }
  
  // Sinkronisasi Koefisien Kalibrasi Hardware
  for(int i=0; i<8; i++) {
    String buPath = pathCalib + "/" + BU_NAMES[i];
    if(Firebase.RTDB.getJSON(&fbdo, buPath)) {
        FirebaseJson &json = fbdo.jsonObject();
        auto loadCal = [&](String keyName, int slotIdx) {
            // Default: Unity Gain (b=1, c=0) jika data belum ada
            calibs[slotIdx].a = 0; calibs[slotIdx].b = 1; calibs[slotIdx].c = 0; 
            FirebaseJsonData d; 
            if(json.get(d, keyName + "/a")) calibs[slotIdx].a = d.floatValue; 
            if(json.get(d, keyName + "/b")) calibs[slotIdx].b = d.floatValue; 
            if(json.get(d, keyName + "/c")) calibs[slotIdx].c = d.floatValue;
        };
        // Load data untuk mode SACP dan HCP
        loadCal("SACP_T1", (i*2)); loadCal("SACP_T2", (i*2)+1); 
        loadCal("HCP_T1", 16+(i*2)); loadCal("HCP_T2", 16+(i*2)+1);
    }
    display.print("."); display.display();
  }
}

void routineCurrentLog() {
  if (!allProtectionOn || isMeasuring || WiFi.status() != WL_CONNECTED || !signupOK) return;
  time_t now; time(&now); unsigned long currentEpoch = (unsigned long)now;
  
  // Interval logging: 1 Jam (3600 detik)
  if ((currentEpoch - lastLogTimestamp) >= 3600) {
      display.clearDisplay(); display.setCursor(0,25); display.println("AUTO-LOGGING..."); display.display();
      int hari = calculateDayCount(); String waktu = getWaktuLokal();
      
      for (int i = 0; i < 8; i++) {
        tcaSelect(i); 
        ina219.setCalibration_32V_1A(); 
        float current_mA = acquireCurrent();
        
        FirebaseJson json; 
        json.set("timestamp", (int)currentEpoch); 
        json.set("waktu_lokal", waktu); 
        json.set("hari_ke", hari); 
        json.set("bu", BU_NAMES[i]); 
        json.set("i_ma", current_mA); 
        
        Firebase.RTDB.pushJSON(&fbdo, pathCurrentLogs, &json); delay(200);
      }
      lastLogTimestamp = currentEpoch; preferences.putULong("lastLog", lastLogTimestamp); 
      ESP.restart(); // Refresh system memory
  }
}

// ==========================================
// 7. OPERATION & UI LOGIC
// ==========================================
void setMultiplexer(int mode, int tulangan, int bu_idx) {
  digitalWrite(PIN_EN_MUX1, HIGH); digitalWrite(PIN_EN_MUX2, HIGH); // Disable all first
  
  int ch = (bu_idx * 2) + (tulangan - 1);
  digitalWrite(MUX_S0, (ch & 0x01)); 
  digitalWrite(MUX_S1, (ch >> 1) & 0x01);
  digitalWrite(MUX_S2, (ch >> 2) & 0x01); 
  digitalWrite(MUX_S3, (ch >> 3) & 0x01);
  
  // Enable specific MUX based on Mode (SACP vs HCP)
  if (mode == 3) digitalWrite(PIN_EN_MUX1, LOW); // HCP uses Mux 1
  else digitalWrite(PIN_EN_MUX2, LOW); // SACP uses Mux 2
}

void startMeasurementSequence() {
  isMeasuring = true; checkWiFiConnection();
  
  // Validasi Duplikasi Data Harian
  if (isDataExist()) {
     bool resolve = false;
     while(!resolve) {
        display.clearDisplay(); display.setCursor(0,0); display.println("DATA EXISTS!"); 
        display.setCursor(0,20); display.println("[OK] Overwrite"); 
        display.setCursor(0,30); display.println("[BACK] Cancel"); display.display();
        if(digitalRead(BTN_OK)==LOW) { resolve=true; delay(300); }
        if(digitalRead(BTN_BACK)==LOW) { isMeasuring=false; delay(300); return; }
     }
  }

  display.clearDisplay(); display.setCursor(0,25); display.println("SAMPLING..."); display.display();
  setMultiplexer(selectedMode, selectedTul, selectedBU_Idx);
  delay(SETTLING_DELAY_MS); 

  float temp = 25.0;
  sensors.requestTemperatures(); temp = sensors.getTempCByIndex(0);
  
  int rPin = RELAY_PINS[selectedBU_Idx];
  float v1=0, v2=0, v3=0, v4=0, current_mA=0;
  
  // --- SEQUENCE PENGUKURAN ---
  if (selectedMode == 1) { // Mode: Phase 1 (Instant Off)
    // Step 1: Kondisi ON
    digitalWrite(rPin, RELAY_ON); delay(2000); // Stabilisasi
    tcaSelect(selectedBU_Idx); ina219.setCalibration_32V_1A();
    current_mA = acquireCurrent();
    
    v1 = acquireVoltage(); delay(100); v2 = acquireVoltage(); 
    
    // Step 2: Instant OFF (Timing Critical)
    digitalWrite(rPin, RELAY_OFF); 
    delay(150); // Delay standar ASTM/NACE
    v3 = acquireVoltage(); 
    delay(50); 
    v4 = acquireVoltage(); 
    
    // Restore State
    digitalWrite(rPin, allProtectionOn ? RELAY_ON : RELAY_OFF);
  } 
  else { // Mode: Phase 2 (Rest) or HCP
    if (selectedMode == 2) digitalWrite(rPin, RELAY_OFF); // Pastikan OFF untuk Rest
    v3 = acquireVoltage(); delay(100); v4 = acquireVoltage(); 
    digitalWrite(rPin, allProtectionOn ? RELAY_ON : RELAY_OFF);
  }

  // --- RESULT REVIEW ---
  while(true) {
    display.clearDisplay(); display.setTextSize(1); display.setCursor(0,0);
    display.print(selectedMode == 1 ? "PH1 RESULT" : (selectedMode == 2 ? "PH2 RESULT" : "HCP RESULT"));
    display.drawLine(0,9,128,9,WHITE); 
    
    if (selectedMode == 1) { 
      display.setCursor(0,12); display.print("ON Avg :"); display.print((v1+v2)/2.0, 1);
      display.setCursor(0,21); display.print("Diff ON:"); display.print(fabs(v1-v2), 1);
      display.setCursor(0,30); display.print("OFF Avg:"); display.print((v3+v4)/2.0, 1);
      display.setCursor(0,39); display.print("DiffOFF:"); display.print(fabs(v3-v4), 1);
      display.setCursor(0,48); display.print("Temp   :"); display.print(temp, 1);
    } else { 
      String lbl = (selectedMode == 2) ? "Rest" : "HCP ";
      display.setCursor(0,12); display.print(lbl + " Avg :"); display.print((v3+v4)/2.0, 1);
      display.setCursor(0,22); display.print("Diff " + lbl + ":"); display.print(fabs(v3-v4), 1);
      display.setCursor(0,32); display.print("Temp     :"); display.print(temp, 1);
    }
    display.setCursor(0,56); display.println("[OK]SEND  [UP]RETRY"); display.display();
    
    if(digitalRead(BTN_OK) == LOW) {
      uploadData(selectedMode, v1, v2, v3, v4, current_mA, temp); 
      updateCompletionMap(); 
      selectedPoint++; if(selectedPoint > 5) selectedPoint = 1; // Next Point
      isMeasuring = false; break;
    }
    if(digitalRead(BTN_UP) == LOW) { isMeasuring = false; startMeasurementSequence(); return; }
    if(digitalRead(BTN_BACK) == LOW) { isMeasuring = false; break; } 
    delay(10);
  }
}

void uploadData(int mode, float r1, float r2, float r3, float r4, float ima, float t) { 
  checkWiFiConnection(); if (WiFi.status() != WL_CONNECTED) return; 
  FirebaseJson json; int hari = calculateDayCount();
  
  const char* tTitik = (selectedPoint <= 2) ? "Patch Repair" : (selectedPoint == 3 ? "Bounding" : "Existing"); 
  
  json.set("timestamp", (int)time(NULL)); 
  json.set("waktu_lokal", getWaktuLokal()); 
  json.set("hari_ke", hari); 
  json.set("bu", BU_NAMES[selectedBU_Idx]);
  json.set("tp", "T" + String(selectedTul) + "/P" + String(selectedPoint)); 
  json.set("tipe_titik", tTitik); 
  json.set("suhu", t);

  if (mode == 1) { 
    json.set("fase", "PH1"); 
    json.set("i_ma", ima); 
    // Menyimpan nilai rata-rata dari 2 sampel
    json.set("on_avg", (r1+r2)/2.0); 
    json.set("off_avg", (r3+r4)/2.0);
    // Menyimpan raw sampel untuk validasi
    json.set("v_on_1", r1); json.set("v_on_2", r2); 
    json.set("v_off_1", r3); json.set("v_off_2", r4); 
  } else { 
    json.set("fase", mode == 2 ? "PH2" : "HCP"); 
    json.set("v_off_avg", (r3+r4)/2.0); 
    json.set("v_off_1", r3); json.set("v_off_2", r4); 
  } 
  
  if (Firebase.RTDB.pushJSON(&fbdo, pathLogs, &json)) { 
    display.clearDisplay(); display.setCursor(0,25); display.println("UPLOAD SUCCESS"); display.display(); delay(1000); 
  } 
}

void updateCompletionMap() { 
  if (WiFi.status() != WL_CONNECTED) return; 
  int hari = calculateDayCount();
  String faseStr = (selectedMode == 1) ? "PH1" : (selectedMode == 2 ? "PH2" : "HCP");
  String path = pathCheck + "/H" + String(hari) + "/" + BU_NAMES[selectedBU_Idx] + "/T" + String(selectedTul) + "P" + String(selectedPoint) + "/" + faseStr;
  Firebase.RTDB.setBool(&fbdo, path, true); 
}

bool isDataExist() { 
  if (WiFi.status() != WL_CONNECTED || !signupOK) return false; 
  int hari = calculateDayCount();
  String faseStr = (selectedMode == 1) ? "PH1" : (selectedMode == 2 ? "PH2" : "HCP");
  String path = pathCheck + "/H" + String(hari) + "/" + BU_NAMES[selectedBU_Idx] + "/T" + String(selectedTul) + "P" + String(selectedPoint) + "/" + faseStr;
  if (Firebase.RTDB.getBool(&fbdo, path)) return fbdo.to<bool>(); 
  return false; 
}

// ==========================================
// 8. SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200); Wire.begin(21, 22);
  preferences.begin("sprcp", false); 
  lastLogTimestamp = preferences.getULong("lastLog", 0);
  
  // Display Init
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); 
  display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1); 
  display.setCursor(0, 10); display.println("SYSTEM INIT v24.4"); display.display();
  
  // Hardware Init
  ads.begin(); ads.setGain(GAIN_TWOTHIRDS); 
  sensors.begin(); 
  ina219.begin();

  // GPIO Init
  pinMode(MUX_S0, OUTPUT); pinMode(MUX_S1, OUTPUT); pinMode(MUX_S2, OUTPUT); pinMode(MUX_S3, OUTPUT);
  pinMode(PIN_EN_MUX1, OUTPUT); digitalWrite(PIN_EN_MUX1, HIGH); 
  pinMode(PIN_EN_MUX2, OUTPUT); digitalWrite(PIN_EN_MUX2, HIGH); 
  
  for(int i=0; i<8; i++){ pinMode(RELAY_PINS[i], OUTPUT); digitalWrite(RELAY_PINS[i], RELAY_OFF); }
  if(allProtectionOn) { for(int i=0; i<8; i++) digitalWrite(RELAY_PINS[i], RELAY_ON); } 
  
  pinMode(BTN_OK, INPUT_PULLUP); pinMode(BTN_UP, INPUT_PULLUP); 
  pinMode(BTN_DOWN, INPUT_PULLUP); pinMode(BTN_BACK, INPUT_PULLUP);
  
  // Network Init
  WiFi.setAutoReconnect(true); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); display.print("."); display.display(); }
  
  configTime(7 * 3600, 0, "pool.ntp.org"); 
  struct tm timeinfo; while(!getLocalTime(&timeinfo)){ delay(100); }
  
  // Firebase Init
  config.api_key = API_KEY; config.database_url = DATABASE_URL; config.token_status_callback = tokenStatusCallback;
  if (Firebase.signUp(&config, &auth, "", "")) signupOK = true; 
  Firebase.begin(&config, &auth); Firebase.reconnectWiFi(true);
  
  // Load Parameters
  for(int i=0;i<32;i++) { calibs[i].a=0; calibs[i].b=1; calibs[i].c=0; }
  syncConfiguration(); updateSystemStatus();
}

void loop() {
  if (millis() - lastWiFiCheckMillis > 30000) { checkWiFiConnection(); lastWiFiCheckMillis = millis(); }
  
  // Menu Navigation Logic
  if (digitalRead(BTN_BACK) == LOW) { if (menuState > 0) menuState--; delay(250); }
  
  if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
    int dir = (digitalRead(BTN_UP) == LOW) ? -1 : 1;
    if (menuState == 0) { 
        selectedMode += dir; 
        if(selectedMode > 3) selectedMode = 0; if(selectedMode < 0) selectedMode = 3; 
    }
    else if (menuState == 1) selectedBU_Idx = (selectedBU_Idx + dir + 8) % 8;
    else if (menuState == 2) selectedTul = (selectedTul == 1) ? 2 : 1;
    else if (menuState == 3) selectedPoint = max(1, min(5, selectedPoint + dir));
    delay(200);
  }
  
  if (digitalRead(BTN_OK) == LOW) {
    if (selectedMode == 0 && menuState == 0) {
       allProtectionOn = !allProtectionOn;
       for(int i=0; i<8; i++) digitalWrite(RELAY_PINS[i], allProtectionOn ? RELAY_ON : RELAY_OFF); 
       updateSystemStatus();
       delay(300);
    } else { 
        menuState++; 
        if (menuState > 4) { startMeasurementSequence(); menuState = 3; } 
    } 
    delay(300);
  }
  
  routineCurrentLog();
  
  // Dashboard UI
  display.clearDisplay(); display.setTextSize(1); display.setCursor(0,0); 
  display.print("SPRCP v24.4 | "); display.print(allProtectionOn ? "PROT" : "IDLE");
  if(WiFi.status() != WL_CONNECTED) display.print(" !"); 
  display.drawLine(0,9,128,9,WHITE); display.setCursor(0,12);
  
  if(menuState == 0) {
    display.println("MODE OPERASI:"); 
    display.print(selectedMode == 0 ? "> ANODE CTRL " : "  ANODE CTRL "); display.println(allProtectionOn ? "[ON]" : "[OFF]");
    display.print(selectedMode == 1 ? "> PHASE 1 (O/I)" : "  PHASE 1 (O/I)"); display.setCursor(0,42);
    display.print(selectedMode == 2 ? "> PHASE 2 (REST)" : "  PHASE 2 (REST)"); display.setCursor(0,52);
    display.print(selectedMode == 3 ? "> HCP NATURAL" : "  HCP NATURAL");
    if(allProtectionOn) { 
      time_t now; time(&now); long elapsed = (long)now - (long)lastLogTimestamp; 
      display.setCursor(95,12); display.print("["); display.print((3600 - elapsed)/60); display.print("m]"); 
    }
  }
  else if(menuState == 1) { display.println("BENDA UJI:"); display.setTextSize(2); display.setCursor(35,30); display.println(BU_NAMES[selectedBU_Idx]); }
  else if(menuState == 2) { display.println("TULANGAN:"); display.setTextSize(2); display.setCursor(50,30); display.print("T"); display.println(selectedTul); }
  else if(menuState == 3) { display.println("TITIK UKUR:"); display.setTextSize(2); display.setCursor(50,30); display.print("P"); display.println(selectedPoint); }
  else if(menuState == 4) { 
    display.println("READY SCAN?"); display.setTextSize(1); display.setCursor(0,25); 
    display.print("D"); display.print(calculateDayCount()); display.print("|"); 
    display.print(BU_NAMES[selectedBU_Idx]); display.print("|T"); display.print(selectedTul); display.print("P"); display.println(selectedPoint); 
    display.setCursor(0,50); display.println("[OK] START SAMPLING"); 
  }
  display.display(); 
  
  static unsigned long lastStatusUpdate = 0; 
  if(millis() - lastStatusUpdate > 60000) { updateSystemStatus(); lastStatusUpdate = millis(); }
}