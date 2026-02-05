/*
 * FIRMWARE MASTER: SPRCP v24.6 (DIAGNOSTIC & OFFSET)
 * ---------------------------------------------------------------------------
 * BASIS: v24.4 + OFFSET SYSTEM + SERIAL DIAGNOSTICS
 * * FITUR BARU:
 * 1. OFFSET SYSTEM: Manipulasi nilai (Raw -> Offset) terjadi di dalam ESP32.
 * - OLED menampilkan nilai Offset.
 * - Database (Main Key) menyimpan nilai Offset.
 * - Database (Org Key) menyimpan nilai Asli (Backup).
 * 2. DIAGNOSTICS: Serial Monitor menampilkan status detil untuk pelacakan error/hang.
 * 3. STABILITY: Watchdog Timer & Token Refresh aktif.
 * ---------------------------------------------------------------------------
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

// --- 1. KONFIGURASI PIN ---
#define PIN_EN_MUX1  17  
#define PIN_EN_MUX2  16  
#define MUX_S0 23
#define MUX_S1 19
#define MUX_S2 18
#define MUX_S3 5

const int RELAY_PINS[8] = {13, 12, 14, 27, 26, 25, 33, 32};
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

#define ONE_WIRE_BUS 15 
#define BTN_OK   35 
#define BTN_UP   34 
#define BTN_DOWN 39 
#define BTN_BACK 36 

// --- 2. NETWORK & DATABASE ---
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define API_KEY "YOUR_FIREBASE_API_KEY"
#define DATABASE_URL "YOUR_FIREBASE_DATABASE_URL"

const String basePath = "/artifacts/korosi-iot/public/data";
const String pathLogs = basePath + "/logs";
const String pathCurrentLogs = basePath + "/current_logs";
const String pathConfig = basePath + "/research_config";
const String pathCalib = basePath + "/kalibrasi";
const String pathCheck = basePath + "/completion_map";
const String pathStatus = basePath + "/status_global";
const char* const BU_NAMES[] = {"M1", "M4", "M10", "M14", "MK22", "MK21", "MK18", "MK17"};

// --- 3. OBJEK HARDWARE ---
Adafruit_ADS1115 ads;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_INA219 ina219;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
FirebaseData fbdo; FirebaseAuth auth; FirebaseConfig config;

// --- 4. PARAMETER KALIBRASI ---
#define BUFFER_COMPENSATION 1.111F 
#define WINDOW_SIZE 128 
#define SETTLING_DELAY_MS 1200 
#define CURRENT_SAMPLES 30 

const float CALIBRATION_FACTOR = 1.1381F;
float poly_a = 0.0, poly_b = 1.0, poly_c = 0.0; 
long startTimestamp = 0; 

// [OFFSET DATA]
struct OffsetData {
  float minVal;
  float maxVal;
};
OffsetData offsets[8][3]; // 8 BU x 3 Zones (0=Patch, 1=Boundary, 2=Existing)

// STATE VARIABLES
bool signupOK = false;
bool isMeasuring = false;
int menuState = 0, selectedMode = 1, selectedBU_Idx = 0, selectedTul = 1, selectedPoint = 1;
bool allProtectionOn = true;
unsigned long lastCurrentLogMillis = 0, lastStatusSyncMillis = 0, lastWiFiCheckMillis = 0;
const unsigned long INTERVAL_CURRENT_LOG = 3600000;
unsigned long wifiDisconnectStart = 0;

// --- 5. FUNGSI UTILS & DIAGNOSTIK ---
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Connection LOST! Attempting Reconnect...");
    
    if (wifiDisconnectStart == 0) wifiDisconnectStart = millis();
    
    // Watchdog: Restart jika putus > 5 menit
    if (millis() - wifiDisconnectStart > 300000) { 
      Serial.println("[WATCHDOG] WiFi Down > 5 Min. FORCING RESTART!");
      ESP.restart(); 
    }

    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startWait = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) { 
      delay(100); 
    }
    
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("[WIFI] Reconnected Successfully!");
      Serial.print("[WIFI] IP: "); Serial.println(WiFi.localIP());
      wifiDisconnectStart = 0;
    } else {
      Serial.println("[WIFI] Reconnect Failed. Retrying later.");
    }
  } else {
    wifiDisconnectStart = 0;
  }
}

int calculateDayCount() {
  if (startTimestamp <= 0) return 0;
  time_t now; time(&now);
  long diffSeconds = (long)now - startTimestamp;
  if (diffSeconds < 0) return 0;
  return (diffSeconds / 86400) + 1;
}

String getWaktuLokal() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "Unknown";
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

void tcaSelect(uint8_t i) {
  Wire.beginTransmission(0x70); Wire.write(1 << i); Wire.endTransmission();
}

void updateFirebaseStatus() {
  if (WiFi.status() != WL_CONNECTED || !signupOK) return;
  long nextLogMin = (allProtectionOn) ?
  (INTERVAL_CURRENT_LOG - (millis() - lastCurrentLogMillis)) / 60000 : 0;
  
  FirebaseJson json; 
  json.set("status", allProtectionOn ? "PROTECTED" : "OFF");
  json.set("next_sync_min", max(0L, nextLogMin));
  json.set("last_update", getWaktuLokal());
  json.set("uptime_ms", (double)millis()); // Diagnostik Uptime
  
  if(Firebase.RTDB.setJSON(&fbdo, pathStatus, &json)) {
    // Serial.println("[STATUS] Heartbeat Sent.");
  } else {
    Serial.print("[STATUS] Failed to send Heartbeat: ");
    Serial.println(fbdo.errorReason());
  }
}

float readFilteredCurrent() {
  float maxVal = 0.0;
  for(int i=0; i<CURRENT_SAMPLES; i++) {
    float val = ina219.getCurrent_mA();
    if (fabs(val) > fabs(maxVal)) maxVal = val;
    delay(5);
  }
  return maxVal;
}

// --- 6. FUNGSI SELECTOR MUX ---
void selectChannel(int mode, int tulangan, int bu_idx) {
  digitalWrite(PIN_EN_MUX1, HIGH); digitalWrite(PIN_EN_MUX2, HIGH);
  int localCh = (bu_idx * 2) + (tulangan - 1);
  digitalWrite(MUX_S0, (localCh & 0x01)); digitalWrite(MUX_S1, (localCh >> 1) & 0x01);
  digitalWrite(MUX_S2, (localCh >> 2) & 0x01); digitalWrite(MUX_S3, (localCh >> 3) & 0x01);
  if (mode == 3) digitalWrite(PIN_EN_MUX1, LOW); else digitalWrite(PIN_EN_MUX2, LOW);
}

// --- 7. ADC ENGINE ---
float ambilSatuData() {
  double totalResult = 0;
  for(int i=0; i<WINDOW_SIZE; i++) { totalResult += ads.readADC_Differential_0_1(); delayMicroseconds(50); }
  float voltage = ((totalResult / WINDOW_SIZE) * 0.1875F) / 1000.0;
  float final_mv = (voltage * CALIBRATION_FACTOR * BUFFER_COMPENSATION) * 1000.0;
  return (poly_a * pow(final_mv, 2)) + (poly_b * final_mv) + poly_c;
}

// --- 8. LOGGING ARUS OTOMATIS (SCHEDULED RESTART) ---
void sendHourlyCurrentLogs() {
  Serial.println("[AUTO-LOG] Starting Hourly Current Log...");
  checkWiFiConnection();
  if (!allProtectionOn || isMeasuring || WiFi.status() != WL_CONNECTED || !signupOK) {
    Serial.println("[AUTO-LOG] Skipped (Busy/No WiFi/Off).");
    return;
  }
  
  display.clearDisplay(); display.setCursor(0,25); display.println("AUTO-SYNC CURRENT..."); display.display();
  int hari = calculateDayCount();
  String waktu = getWaktuLokal();
  
  for (int i = 0; i < 8; i++) {
    tcaSelect(i); ina219.begin();
    float current_ma = readFilteredCurrent(); 
    FirebaseJson json; 
    json.set("timestamp", (int)time(NULL)); 
    json.set("waktu_lokal", waktu);
    json.set("hari_ke", hari); 
    json.set("bu", BU_NAMES[i]); 
    json.set("i_ma", current_ma);
    
    if(Firebase.RTDB.pushJSON(&fbdo, pathCurrentLogs, &json)){
      Serial.print("[AUTO-LOG] Sent BU: "); Serial.println(BU_NAMES[i]);
    } else {
      Serial.print("[AUTO-LOG] Failed BU: "); Serial.print(BU_NAMES[i]);
      Serial.print(" Error: "); Serial.println(fbdo.errorReason());
    }
    delay(200);
  }
  
  // SCHEDULED RESTART UNTUK MENCEGAH HANG
  Serial.println("[SYSTEM] Hourly Task Done. Performing Scheduled Restart to Clear Memory...");
  delay(1000); 
  ESP.restart();
}

// --- 9. CONFIG & OFFSET FETCH (DIPERBARUI) ---
void fetchConfig() {
  if (WiFi.status() != WL_CONNECTED || !signupOK) return;
  
  Serial.println("[CONFIG] Fetching Research Config...");
  if (Firebase.RTDB.getJSON(&fbdo, pathConfig)) {
    FirebaseJsonData jd; FirebaseJson &json = fbdo.jsonObject();
    if (json.get(jd, "start_timestamp")) startTimestamp = (long)jd.intValue;
  }
  
  Serial.println("[CONFIG] Fetching Calibration...");
  if(Firebase.RTDB.getJSON(&fbdo, pathCalib)){
    FirebaseJsonData jd; FirebaseJson &json = fbdo.jsonObject();
    if (json.get(jd, "poly_a")) poly_a = jd.floatValue;
    if (json.get(jd, "poly_b")) poly_b = jd.floatValue;
    if (json.get(jd, "poly_c")) poly_c = jd.floatValue;
  }

  // AMBIL OFFSET
  Serial.println("[CONFIG] Downloading OFFSETS...");
  display.clearDisplay(); display.setCursor(0,0); display.println("Downloading"); display.println("Offset Data..."); display.display();
  
  for(int i=0; i<8; i++) {
    String buPath = pathConfig + "/offsets/" + BU_NAMES[i];
    if(Firebase.RTDB.getJSON(&fbdo, buPath)) {
        FirebaseJson &json = fbdo.jsonObject();
        FirebaseJsonData jd;
        // Patch Repair (Zone 0)
        if(json.get(jd, "Patch Repair/min")) offsets[i][0].minVal = jd.floatValue;
        if(json.get(jd, "Patch Repair/max")) offsets[i][0].maxVal = jd.floatValue;
        // Boundary (Zone 1)
        if(json.get(jd, "Boundary/min")) offsets[i][1].minVal = jd.floatValue;
        if(json.get(jd, "Boundary/max")) offsets[i][1].maxVal = jd.floatValue;
        // Existing (Zone 2)
        if(json.get(jd, "Existing/min")) offsets[i][2].minVal = jd.floatValue;
        if(json.get(jd, "Existing/max")) offsets[i][2].maxVal = jd.floatValue;
    }
    Serial.print("."); display.print("."); display.display();
  }
  Serial.println(" Done!");
}

// --- HELPER: HITUNG OFFSET ---
float getOffsetValue(float rawVal) {
  int zoneIdx = 0; // Default Patch
  if(selectedPoint == 3) zoneIdx = 1; // Boundary
  else if(selectedPoint > 3) zoneIdx = 2; // Existing

  float minV = offsets[selectedBU_Idx][zoneIdx].minVal;
  float maxV = offsets[selectedBU_Idx][zoneIdx].maxVal;

  if (maxV > minV) {
    float noise = minV + (random(0, 1000) / 1000.0) * (maxV - minV);
    // Serial Diagnostic
    // Serial.print(" [OFFSET] Zone: "); Serial.print(zoneIdx);
    // Serial.print(" Raw: "); Serial.print(rawVal);
    // Serial.print(" Add: "); Serial.println(noise);
    return rawVal + noise;
  }
  return rawVal; 
}

// --- 10. CORE MEASUREMENT LOGIC ---
void runMeasurement() {
  isMeasuring = true;
  checkWiFiConnection();
  
  if (isDataDuplicate()) {
     Serial.println("[MEASURE] Duplicate data detected!");
     bool resolve = false;
     while(!resolve) {
        display.clearDisplay(); display.setCursor(0,0); display.println("DATA EXISTS!");
        display.setCursor(0,20); display.println("[OK] Overwrite");
        display.setCursor(0,30); display.println("[BACK] Cancel"); display.display();
        if(digitalRead(BTN_OK)==LOW) { resolve=true; delay(300); }
        if(digitalRead(BTN_BACK)==LOW) { isMeasuring=false; delay(300); return; }
     }
  }
  
  Serial.println("[MEASURE] Starting Sequence...");
  display.clearDisplay(); display.setCursor(0,25); display.println("SETTLING..."); display.display();
  selectChannel(selectedMode, selectedTul, selectedBU_Idx);
  delay(SETTLING_DELAY_MS); 

  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  int rPin = RELAY_PINS[selectedBU_Idx];
  
  float raw_v1=0, raw_v2=0, raw_v3=0, raw_v4=0, i_ma=0;
  float disp_v1=0, disp_v2=0, disp_v3=0, disp_v4=0;

  if (selectedMode == 1) { // PHASE 1
    digitalWrite(rPin, RELAY_ON);
    delay(2500); 
    tcaSelect(selectedBU_Idx); ina219.begin(); i_ma = readFilteredCurrent();
    
    display.clearDisplay(); display.setCursor(0,25); display.println("BACA ON..."); display.display();
    raw_v1 = ambilSatuData(); delay(200); raw_v2 = ambilSatuData(); 
    
    digitalWrite(rPin, RELAY_OFF); delay(200);
    
    display.clearDisplay(); display.setCursor(0,25); display.println("BACA INSTANT OFF..."); display.display();
    raw_v3 = ambilSatuData(); delay(500); raw_v4 = ambilSatuData(); 
    
    digitalWrite(rPin, allProtectionOn ? RELAY_ON : RELAY_OFF);
  } else { // PHASE 2 / HCP
    if (selectedMode == 2) digitalWrite(rPin, RELAY_OFF);
    display.clearDisplay(); display.setCursor(0,25); display.println("BACA DATA..."); display.display();
    raw_v3 = ambilSatuData(); delay(1000); raw_v4 = ambilSatuData(); 
    digitalWrite(rPin, allProtectionOn ? RELAY_ON : RELAY_OFF);
  }

  // --- HITUNG OFFSET ---
  disp_v1 = getOffsetValue(raw_v1);
  disp_v2 = getOffsetValue(raw_v2);
  disp_v3 = getOffsetValue(raw_v3);
  disp_v4 = getOffsetValue(raw_v4);
  
  Serial.print("[RESULT] RAW OffAvg: "); Serial.print((raw_v3+raw_v4)/2);
  Serial.print(" -> OFFSET OffAvg: "); Serial.println((disp_v3+disp_v4)/2);

  // UI CONFIRMATION
  while(true) {
    display.clearDisplay(); 
    display.setTextSize(1); display.setCursor(0,0);
    if (selectedMode == 1) display.print("SACP PHASE 1");
    else if (selectedMode == 2) display.print("SACP PHASE 2");
    else display.print("HCP NATURAL");
    
    display.drawLine(0,9,128,9,WHITE); display.setCursor(0,12);
    // TAMPILKAN NILAI OFFSET (DISP) DI OLED
    if (selectedMode == 1) {
      display.print("ON Avg : "); display.print((disp_v1+disp_v2)/2.0, 0); display.println("mV");
      display.print("IOffAvg: "); display.print((disp_v3+disp_v4)/2.0, 0); display.println("mV");
      display.print("Arus   : "); display.print(i_ma, 2); display.println("mA"); 
    } else {
      String label = (selectedMode == 3) ? "HCP" : "Rest";
      display.print(label + " Avg : "); display.print((disp_v3+disp_v4)/2.0, 1); display.println("mV");
      display.print("Diff    : "); display.print(abs(disp_v3-disp_v4), 1); display.println("mV");
      display.println(""); 
    }
    display.print("Suhu    : "); display.print(temp, 1); display.println("C");

    display.setCursor(0,55); display.println("[OK]KIRIM [UP]ULANG");
    display.display();
    if(digitalRead(BTN_OK) == LOW) {
      kirimDataRiset(selectedMode, raw_v1, raw_v2, raw_v3, raw_v4, disp_v1, disp_v2, disp_v3, disp_v4, i_ma, temp); 
      markAsCompleted(); 
      selectedPoint++;
      if(selectedPoint > 5) selectedPoint = 1;
      isMeasuring = false; break;
    }
    if(digitalRead(BTN_UP) == LOW) { isMeasuring = false; runMeasurement(); return; }
    if(digitalRead(BTN_BACK) == LOW) { isMeasuring = false; break; } delay(10);
  }
}

// --- 11. DATABASE UPLOAD HELPER ---
void kirimDataRiset(int mode, float r_on1, float r_on2, float r_off1, float r_off2, 
                              float d_on1, float d_on2, float d_off1, float d_off2, 
                              float i_ma, float temp) { 
  checkWiFiConnection();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[UPLOAD] Upload Aborted: No WiFi.");
    return; 
  }
  
  Serial.println("[UPLOAD] Preparing Data Packet...");
  FirebaseJson json; 
  int hari = calculateDayCount();
  const char* tTitik = (selectedPoint <= 2) ? "Patch Repair" : (selectedPoint == 3 ? "Bounding" : "Existing"); 
  
  json.set("timestamp", (int)time(NULL)); 
  json.set("waktu_lokal", getWaktuLokal()); 
  json.set("hari_ke", hari); 
  json.set("bu", BU_NAMES[selectedBU_Idx]);
  json.set("tp", "T" + String(selectedTul) + "/P" + String(selectedPoint)); 
  json.set("tipe_titik", tTitik); 
  json.set("suhu", temp);
  
  if (mode == 1) { 
    json.set("fase", "PH1"); json.set("i_ma", i_ma); 
    // Main Keys = Offset Values
    json.set("v_on_1", d_on1); json.set("v_on_2", d_on2);
    json.set("on_avg", (d_on1 + d_on2)/2.0); 
    json.set("v_off_1", d_off1); json.set("v_off_2", d_off2); 
    json.set("off_avg", (d_off1 + d_off2)/2.0);
    // Backup Keys = Raw Values
    json.set("org_on_avg", (r_on1 + r_on2)/2.0);
    json.set("org_off_avg", (r_off1 + r_off2)/2.0);
  } else { 
    json.set("fase", mode == 2 ? "PH2" : "HCP"); 
    // Main Keys = Offset Values
    json.set("v_off_1", d_off1); json.set("v_off_2", d_off2);
    json.set("v_off_avg", (d_off1 + d_off2)/2.0); 
    // Backup Keys = Raw Values
    json.set("org_v_off_avg", (r_off1 + r_off2)/2.0);
  }
  
  json.set("is_offset", true); 

  if (Firebase.RTDB.pushJSON(&fbdo, pathLogs, &json)) { 
    display.clearDisplay(); display.setCursor(0,25);
    display.println("DATABASE UPDATED!"); display.display(); 
    Serial.println("[UPLOAD] Success!");
    delay(1000); 
  } else {
    Serial.print("[UPLOAD] FAILED! Reason: ");
    Serial.println(fbdo.errorReason());
    display.clearDisplay(); display.setCursor(0,25);
    display.println("UPLOAD FAILED!"); display.display(); delay(2000);
  }
}

void markAsCompleted() { 
  if (WiFi.status() != WL_CONNECTED) return; 
  int hari = calculateDayCount();
  String faseStr = (selectedMode == 1) ? "PH1" : (selectedMode == 2 ? "PH2" : "HCP");
  String tpStr = "T" + String(selectedTul) + "P" + String(selectedPoint);
  String checkPath = pathCheck + "/H" + String(hari) + "/" + BU_NAMES[selectedBU_Idx] + "/" + tpStr + "/" + faseStr;
  Firebase.RTDB.setBool(&fbdo, checkPath, true); 
}

bool isDataDuplicate() { 
  if (WiFi.status() != WL_CONNECTED || !signupOK) return false; 
  int hari = calculateDayCount();
  String faseStr = (selectedMode == 1) ? "PH1" : (selectedMode == 2 ? "PH2" : "HCP");
  String tpStr = "T" + String(selectedTul) + "P" + String(selectedPoint);
  String checkPath = pathCheck + "/H" + String(hari) + "/" + BU_NAMES[selectedBU_Idx] + "/" + tpStr + "/" + faseStr;
  if (Firebase.RTDB.getBool(&fbdo, checkPath)) return fbdo.to<bool>(); return false; 
}

// --- 12. SETUP & LOOP ---
void setup() {
  Serial.begin(115200); Wire.begin(21, 22);
  Wire.setClock(10000L);
  ads.begin(); ads.setGain(GAIN_TWOTHIRDS); 
  sensors.begin(); 
  
  pinMode(MUX_S0, OUTPUT); pinMode(MUX_S1, OUTPUT); pinMode(MUX_S2, OUTPUT); pinMode(MUX_S3, OUTPUT);
  pinMode(PIN_EN_MUX1, OUTPUT); digitalWrite(PIN_EN_MUX1, HIGH);
  pinMode(PIN_EN_MUX2, OUTPUT); digitalWrite(PIN_EN_MUX2, HIGH); 
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); display.clearDisplay(); display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(35, 20); display.println("SPRCP");
  display.setTextSize(1); display.setCursor(20, 45); display.println("v24.6 DIAG"); display.display();
  
  for(int i=0; i<8; i++){ pinMode(RELAY_PINS[i], OUTPUT); digitalWrite(RELAY_PINS[i], RELAY_OFF); }
  if(allProtectionOn) { for(int i=0; i<8; i++) digitalWrite(RELAY_PINS[i], RELAY_ON); }

  pinMode(BTN_OK, INPUT_PULLUP); pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DOWN, INPUT_PULLUP); pinMode(BTN_BACK, INPUT_PULLUP);
  
  Serial.println("[BOOT] Connecting to WiFi...");
  WiFi.setAutoReconnect(true); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(" Connected!");
  
  configTime(7 * 3600, 0, "pool.ntp.org");
  struct tm timeinfo; while(!getLocalTime(&timeinfo)){ delay(100); }

  config.api_key = API_KEY; config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;
  
  if (Firebase.signUp(&config, &auth, "", "")) signupOK = true; Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  fetchConfig(); 
  lastCurrentLogMillis = millis() - (INTERVAL_CURRENT_LOG - 60000); updateFirebaseStatus();
}

void loop() {
  if (millis() - lastWiFiCheckMillis > 30000) { checkWiFiConnection(); lastWiFiCheckMillis = millis(); }
  
  if (digitalRead(BTN_BACK) == LOW) { if (menuState > 0) menuState--; delay(250); }
  
  if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
    int dir = (digitalRead(BTN_UP) == LOW) ? -1 : 1;
    if (menuState == 0) { selectedMode += dir; if(selectedMode > 3) selectedMode = 0; if(selectedMode < 0) selectedMode = 3; }
    else if (menuState == 1) selectedBU_Idx = (selectedBU_Idx + dir + 8) % 8;
    else if (menuState == 2) selectedTul = (selectedTul == 1) ? 2 : 1;
    else if (menuState == 3) selectedPoint = max(1, min(5, selectedPoint + dir));
    delay(200);
  }
  
  if (digitalRead(BTN_OK) == LOW) {
    if (selectedMode == 0 && menuState == 0) {
       allProtectionOn = !allProtectionOn;
       for(int i=0; i<8; i++) digitalWrite(RELAY_PINS[i], allProtectionOn ? RELAY_ON : RELAY_OFF);
       lastCurrentLogMillis = millis() - (INTERVAL_CURRENT_LOG - 60000); updateFirebaseStatus(); delay(300);
    } else { menuState++; if (menuState > 4) { runMeasurement(); menuState = 3; } } delay(300);
  }
  
  if (allProtectionOn && !isMeasuring && (millis() - lastCurrentLogMillis >= INTERVAL_CURRENT_LOG)) { sendHourlyCurrentLogs(); lastCurrentLogMillis = millis(); updateFirebaseStatus(); }
  
  if (millis() - lastStatusSyncMillis >= 60000) { updateFirebaseStatus(); lastStatusSyncMillis = millis(); }

  // MENU DISPLAY
  display.clearDisplay();
  display.setTextSize(1); display.setCursor(0,0); display.print("SPRCP v24.6 | "); display.print(allProtectionOn ? "PROT" : "OFF");
  if (WiFi.status() != WL_CONNECTED) display.print(" !"); display.drawLine(0,9,128,9,WHITE); display.setCursor(0,12);
  if(menuState == 0) {
    display.println("MODE KERJA:"); display.print(selectedMode == 0 ? "> ANODA CTRL " : "  ANODA CTRL ");
    display.println(allProtectionOn ? "[ON]" : "[OFF]");
    display.print(selectedMode == 1 ? "> PHASE 1 (O/I)" : "  PHASE 1 (O/I)"); display.setCursor(0,42);
    display.print(selectedMode == 2 ? "> PHASE 2 (REST)" : "  PHASE 2 (REST)"); display.setCursor(0,52);
    display.print(selectedMode == 3 ? "> HCP NATURAL" : "  HCP NATURAL");
    if(allProtectionOn) { long nextLog = (INTERVAL_CURRENT_LOG - (millis() - lastCurrentLogMillis)) / 60000; display.setCursor(95,12); display.print("["); display.print(nextLog); display.print("m]"); }
  }
  else if(menuState == 1) { display.println("BENDA UJI:"); display.setTextSize(2); display.setCursor(35,30); display.println(BU_NAMES[selectedBU_Idx]); }
  else if(menuState == 2) { display.println("TULANGAN:"); display.setTextSize(2); display.setCursor(50,30); display.print("T"); display.println(selectedTul); }
  else if(menuState == 3) { display.println("TITIK UKUR:"); display.setTextSize(2); display.setCursor(50,30); display.print("P"); display.println(selectedPoint); }
  else if(menuState == 4) { display.println("READY SCAN?"); display.setTextSize(1); display.setCursor(0,25); display.print("H-"); display.print(calculateDayCount()); display.print(" | "); display.print(BU_NAMES[selectedBU_Idx]); display.print(" | T");
  display.print(selectedTul); display.print(" | P"); display.println(selectedPoint); display.setCursor(0,50); display.println("[OK] START"); }
  display.display(); static unsigned long lastFetch = 0;
  if (millis() - lastFetch > 30000) { fetchConfig(); lastFetch = millis(); }
}