// ============================================================
//  HAMOWNIA v3.4 - ESP32-C3 SuperMini
//  Changes:
//  - Heater no longer blocked by low battery (works at GZERO/0V)
//  - Heater settings: text input boxes instead of sliders
//  - Recording STOP saves CSV and auto-refreshes test list
//  - Input fields don't get overwritten while user is editing
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HX711.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================
//  KONFIGURACJA
// ============================================================
float CALIBRATION_FACTOR = -420.0f;

const float BAT_LOW_V = 3.5f;
const float BAT_CRIT_V = 3.3f;
const float VIN_LOW_V = 11.5f;

unsigned long heaterMaxOnMs = HEATER_DEFAULT_DURATION_MS;
int heaterPwmDuty = HEATER_DEFAULT_DUTY_PERCENT;
unsigned long HEATER_COUNTDOWN_MS = (unsigned long)HEATER_DEFAULT_COUNTDOWN_S * 1000;

// ============================================================
//  PINY
// ============================================================
#define PIN_BAT_ADC 0
#define PIN_BAT_ADC2 1
#define PIN_BT2 2
#define PIN_OLED_SCL 3
#define PIN_OLED_SDA 4
#define PIN_STATUS_LED 5
#define PIN_HEATER_EN 6
#define PIN_HX711_DT 7
#define PIN_HX711_SCK 8
#define PIN_SD_CS 9
#define PIN_SD_MOSI 10
#define PIN_SD_CLK 20
#define PIN_SD_MISO 21

#define HEATER_PWM_FREQ 1000
#define HEATER_PWM_RES 8

const float DIVIDER_RATIO = (100.0f + 33.0f) / 33.0f;
const float ADC_REF_V = 3.3f;
const int ADC_MAX = 4095;

// ADC kalibracja - korekcja offsetu ESP32-C3
// GPIO0 (logic battery): ADC czyta za nisko -> dodajemy 0.60V
// GPIO1 (heater battery): ADC czyta za wysoko -> odejmujemy 0.36V
float adcCorrection(int pin, float rawVoltage) {
    if (pin == PIN_BAT_ADC2) {  // Heater battery - GPIO1
        return rawVoltage - 0.36f;
    } else {  // Logic battery - GPIO0
        return rawVoltage + 0.60f;
    }
}

// ============================================================
//  WSZYSTKIE ZMIENNE GLOBALNE
// ============================================================

// Stany
enum DevState {
  STATE_BOOT, STATE_IDLE, STATE_TARE, STATE_READY, STATE_COUNTDOWN,
  STATE_RECORDING, STATE_SAVING, STATE_HEATER_COUNTDOWN, STATE_HEATER_ON, STATE_ERROR
};
DevState devState = STATE_BOOT;

enum LedState {
  LED_BOOT, LED_IDLE, LED_WIFI, LED_RECORDING,
  LED_HEATER_CDN, LED_HEATER_ON, LED_ERROR_SD, LED_TARE
};
LedState ledState = LED_BOOT;
unsigned long ledTimer = 0;
bool ledOn = false;

// Pomiary
#define MAX_SAMPLES 4000
struct Sample { float time_s; float force_N; };
Sample samples[MAX_SAMPLES];
int sampleCount = 0;
float forceRaw = 0, forceFiltered = 0, forcePeak = 0, forceAvg = 0;
float liveImpulse = 0, lastSampleTime = 0;
unsigned long testStartMs = 0;
bool recording = false;

// Napiecia
float vinVoltage = 0, batVoltage = 0;
unsigned long voltageTimer = 0;

// Grzalka
bool heaterOn = false, heaterCdn = false;
unsigned long heaterCdnStart = 0, heaterOnStart = 0;

// Przycisk
unsigned long btnPressTime = 0;
bool btnDown = false, btnLongFired = false;
const unsigned long LONG_MS = 1500, DEBOUNCE_MS = 30;

// SD
bool sdOk = false;
struct TestMeta {
  char name[32]; float peak_N, avg_N, burn_s, impulse_Ns;
  char motor_class[8]; bool valid;
};
TestMeta testIndex[3];
int newestTestSlot = -1;

// Obiekty
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
HX711 scale;
const float EMA_ALPHA = 0.2f;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long wsPushTimer = 0, wifiConnectStart = 0;
bool wifiConnected = false;
String localIP = "";

char indexHtmlBuffer[5000];

// ============================================================
//  LED
// ============================================================
void setLed(bool on) { ledOn = on; digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW); }

void updateLed() {
  unsigned long now = millis();
  switch (ledState) {
    case LED_BOOT: if (now - ledTimer > 100) { ledTimer = now; setLed(!ledOn); } break;
    case LED_IDLE: if (now - ledTimer > 1000) { ledTimer = now; setLed(!ledOn); } break;
    case LED_WIFI: if (now - ledTimer > 300) { ledTimer = now; setLed(!ledOn); } break;
    case LED_RECORDING: if (now - ledTimer > 250) { ledTimer = now; setLed(!ledOn); } break;
    case LED_HEATER_CDN: if (now - ledTimer > 50) { ledTimer = now; setLed(!ledOn); } break;
    case LED_HEATER_ON: if (now - ledTimer > 25) { ledTimer = now; setLed(!ledOn); } break;
    case LED_ERROR_SD: if (now - ledTimer > 80) { ledTimer = now; setLed(!ledOn); } break;
    case LED_TARE:
      if (now - ledTimer > 80) {
        ledTimer = now; setLed(!ledOn);
        static int tc = 0;
        tc++;
        if (tc >= 6) { tc = 0; ledState = LED_IDLE; setLed(false); }
      }
      break;
  }
}

// ============================================================
//  KLASA SILNIKA
// ============================================================
const char* motorClass(float ns) {
  if (ns < 0.3125f) return "1/4A"; if (ns < 0.625f) return "1/2A";
  if (ns < 1.25f) return "A"; if (ns < 2.5f) return "B";
  if (ns < 5.0f) return "C"; if (ns < 10.0f) return "D";
  if (ns < 20.0f) return "E"; if (ns < 40.0f) return "F";
  if (ns < 80.0f) return "G"; if (ns < 160.0f) return "H";
  if (ns < 320.0f) return "I"; if (ns < 640.0f) return "J";
  if (ns < 1280.0f) return "K"; return "L+";
}

// ============================================================
//  NAPIECIA
// ============================================================
float readVoltage(int pin) {
  long s = 0;
  for (int i = 0; i < 16; i++) { s += analogRead(pin); delay(1); }
  float raw = (s / 16.0f / ADC_MAX) * ADC_REF_V * DIVIDER_RATIO;
  return adcCorrection(pin, raw);
}

// ============================================================
//  SD
// ============================================================
void loadTestIndex() {
  for (int i = 0; i < 3; i++) testIndex[i].valid = false;
  if (!sdOk) return;
  File f = SD.open("/index.json");
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  for (int i = 0; i < 3; i++) {
    if (doc[i].isNull()) continue;
    testIndex[i].valid = true;
    strlcpy(testIndex[i].name, doc[i]["name"] | "", 32);
    strlcpy(testIndex[i].motor_class, doc[i]["motor_class"] | "", 8);
    testIndex[i].peak_N = doc[i]["peak_N"] | 0.0f;
    testIndex[i].avg_N = doc[i]["avg_N"] | 0.0f;
    testIndex[i].burn_s = doc[i]["burn_s"] | 0.0f;
    testIndex[i].impulse_Ns = doc[i]["impulse_Ns"] | 0.0f;
  }
}

void saveTestIndex() {
  if (!sdOk) return;
  JsonDocument doc;
  for (int i = 0; i < 3; i++) {
    if (!testIndex[i].valid) { doc[i] = nullptr; continue; }
    doc[i]["name"] = testIndex[i].name;
    doc[i]["peak_N"] = testIndex[i].peak_N;
    doc[i]["avg_N"] = testIndex[i].avg_N;
    doc[i]["burn_s"] = testIndex[i].burn_s;
    doc[i]["impulse_Ns"] = testIndex[i].impulse_Ns;
    doc[i]["motor_class"] = testIndex[i].motor_class;
  }
  File f = SD.open("/index.json", FILE_WRITE);
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

void saveCurrentTest() {
  devState = STATE_SAVING;
  if (!sdOk || sampleCount == 0) { devState = STATE_READY; ledState = LED_IDLE; return; }
  float peak = 0, totalI = 0, burnEnd = 0;
  for (int i = 0; i < sampleCount; i++) {
    if (samples[i].force_N > peak) peak = samples[i].force_N;
    if (i > 0) totalI += samples[i].force_N * (samples[i].time_s - samples[i - 1].time_s);
    if (samples[i].force_N > 0.5f) burnEnd = samples[i].time_s;
  }
  float avgF = (burnEnd > 0.001f) ? (totalI / burnEnd) : 0;
  int slot = (newestTestSlot + 1) % 3; newestTestSlot = slot;
  String fn = "/test" + String(slot) + ".csv";
  SD.remove(fn.c_str());
  File f = SD.open(fn.c_str(), FILE_WRITE);
  if (f) {
    f.println("time_s,force_N");
    for (int i = 0; i < sampleCount; i++) { f.print(samples[i].time_s, 4); f.print(","); f.println(samples[i].force_N, 4); }
    f.close();
  }
  testIndex[slot].valid = true;
  snprintf(testIndex[slot].name, 32, "T%d-%lus", slot, millis() / 1000);
  testIndex[slot].peak_N = peak; testIndex[slot].avg_N = avgF;
  testIndex[slot].burn_s = burnEnd; testIndex[slot].impulse_Ns = totalI;
  strlcpy(testIndex[slot].motor_class, motorClass(totalI), 8);
  saveTestIndex();
  Serial.printf("=== ZAPISANO: Peak=%.2fN Avg=%.2fN I=%.3fNs Klasa=%s ===\n", peak, avgF, totalI, motorClass(totalI));
  devState = STATE_READY; ledState = LED_IDLE;
}

// ============================================================
//  GRZALKA
// ============================================================
void heaterOff() {
  heaterOn = false; heaterCdn = false;
  ledcWrite(0, 0);
  devState = (recording ? STATE_RECORDING : STATE_READY);
  ledState = (recording ? LED_RECORDING : LED_IDLE);
  Serial.println("GRZALKA OFF");
}

void handleHeater() {
  unsigned long now = millis();
  if (heaterCdn) {
    if (now - heaterCdnStart >= HEATER_COUNTDOWN_MS) {
      heaterCdn = false;
      heaterOn = true; heaterOnStart = now;
      devState = STATE_HEATER_ON; ledState = LED_HEATER_ON;
      ledcWrite(0, (heaterPwmDuty * 255) / 100);
      Serial.printf("GRZALKA ON: %lus %d%%\n", heaterMaxOnMs / 1000, heaterPwmDuty);
    }
  }
  if (heaterOn && now - heaterOnStart >= heaterMaxOnMs) { heaterOff(); }
}

// ============================================================
//  PRZYCISK
// ============================================================
void handleButton() {
  int val = digitalRead(PIN_BT2);
  unsigned long now = millis();
  if (val == LOW && !btnDown && now - btnPressTime > DEBOUNCE_MS) {
    btnDown = true; btnPressTime = now; btnLongFired = false;
  }
  if (btnDown && !btnLongFired && (now - btnPressTime) >= LONG_MS) {
    btnLongFired = true;
    if (recording) { recording = false; saveCurrentTest(); Serial.println("BTN long: STOP i zapis"); }
    else if (devState == STATE_READY || devState == STATE_IDLE) {
      sampleCount = 0; liveImpulse = 0; forceAvg = 0; forcePeak = 0; lastSampleTime = 0;
      testStartMs = millis(); recording = true; devState = STATE_RECORDING; ledState = LED_RECORDING;
      Serial.println("BTN long: START nagrywania");
    }
  }
  if (val == HIGH && btnDown) {
    btnDown = false;
    unsigned long held = now - btnPressTime;
    if (!btnLongFired && held > DEBOUNCE_MS && held < LONG_MS) {
      devState = STATE_TARE; ledState = LED_TARE; ledTimer = millis();
      scale.tare(5); forceFiltered = 0; forcePeak = 0; liveImpulse = 0; forceAvg = 0;
      devState = STATE_READY; Serial.println("BTN short: TARE");
    }
  }
}

// ============================================================
//  OLED
// ============================================================
const char* stateStr() {
  switch (devState) {
    case STATE_BOOT: return "BOOT"; case STATE_IDLE: return "IDLE";
    case STATE_TARE: return "TARE..."; case STATE_READY: return "GOTOWY";
    case STATE_COUNTDOWN: return "ODLICZ"; case STATE_RECORDING: return "REC";
    case STATE_SAVING: return "ZAPIS..."; case STATE_HEATER_COUNTDOWN: return "GRZALKA!";
    case STATE_HEATER_ON: return "GRZANIE!"; case STATE_ERROR: return "ERROR";
  }
  return "?";
}

void updateOled() {
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
  oled.setCursor(0, 0);
  if (heaterCdn) {
    long left = (long)((HEATER_COUNTDOWN_MS - (millis() - heaterCdnStart)) / 1000);
    if (left < 0) left = 0;
    oled.printf("GRZALKA za: %lds %d%%", left, heaterPwmDuty);
  } else if (heaterOn) {
    long left2 = (long)((heaterMaxOnMs - (millis() - heaterOnStart)) / 1000);
    oled.printf("GRZANIE: %lds %d%%", left2, heaterPwmDuty);
  } else if (recording) {
    oled.printf("REC  Klasa: %s", motorClass(liveImpulse));
  } else {
    oled.printf("Stan: %s", stateStr());
  }
  oled.setCursor(0, 11); oled.setTextSize(2);
  oled.printf("%.1fN", forceFiltered);
  oled.setTextSize(1); oled.setCursor(0, 29);
  oled.printf("Pk:%.1fN  I:%.2fNs", forcePeak, liveImpulse);
  oled.setCursor(0, 41);
  oled.printf("VIN:%.1fV BAT:%.1fV", vinVoltage, batVoltage);
  oled.setCursor(0, 53);
  if (wifiConnected) oled.printf("STA: %s", localIP.c_str());
  else oled.printf("AP: %s", localIP.c_str());
  oled.display();
}

// ============================================================
//  WEBSOCKET PUSH
// ============================================================
void wsPush() {
  if (ws.count() == 0) return;
  unsigned long now = millis();
  if (now - wsPushTimer < 100) return;
  wsPushTimer = now;
  long cdn = heaterCdn ? (long)((HEATER_COUNTDOWN_MS - (now - heaterCdnStart)) / 1000) : -1;
  long heaterLeft = heaterOn ? (long)((heaterMaxOnMs - (now - heaterOnStart)) / 1000) : -1;
  char buf[400];
  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"force\":%.2f,\"peak\":%.2f,\"impulse\":%.3f,\"avg\":%.2f,"
    "\"class\":\"%s\",\"vin\":%.2f,\"bat\":%.2f,\"rec\":%d,\"samples\":%d,"
    "\"burn\":%.2f,\"countdown\":%ld,\"heaterLeft\":%ld,\"heaterDuty\":%d,\"heaterTime\":%lu}",
    stateStr(), forceFiltered, forcePeak, liveImpulse, forceAvg, motorClass(liveImpulse),
    vinVoltage, batVoltage, recording ? 1 : 0, sampleCount,
    (now - testStartMs) / 1000.0f, cdn, heaterLeft, heaterPwmDuty, heaterMaxOnMs / 1000);
  ws.textAll(buf);
}

// ============================================================
//  BOOT OLED HELPER
// ============================================================
void oledBootLine(const char* msg, int line, bool ok = true) {
  int y = line * 10;
  oled.fillRect(0, y, OLED_WIDTH, 10, SSD1306_BLACK);
  oled.setCursor(0, y); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
  if (ok) oled.print("[OK] "); else oled.print("[!!] ");
  oled.print(msg); oled.display();
}

// ============================================================
//  HTML TEMPLATE - values injected via snprintf
// ============================================================
#define HTML_TEMPLATE \
"<!DOCTYPE html>" \
"<html lang=\"pl\">" \
"<head>" \
"<meta charset=\"UTF-8\">" \
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" \
"<title>Hamownia</title>" \
"<style>" \
"*{box-sizing:border-box;margin:0;padding:0}" \
"body{font-family:sans-serif;background:#0f0f0f;color:#e0e0e0;padding:12px}" \
"h1{font-size:1.3rem;margin-bottom:10px;color:#4fc}" \
".grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-bottom:10px}" \
".full{grid-column:1/-1}" \
".card{background:#1a1a1a;border-radius:8px;padding:10px}" \
".card h2{font-size:.65rem;color:#666;text-transform:uppercase;letter-spacing:.08em;margin-bottom:4px}" \
".val{font-size:1.9rem;font-weight:700;color:#4fc;font-variant-numeric:tabular-nums}" \
".val.warn{color:#f90}.val.danger{color:#f44}" \
".val.impulse{color:#fa0;font-size:2.2rem}.val.cls{color:#d8a;font-size:2.4rem}" \
"canvas{width:100%;height:190px;background:#111;border-radius:6px;display:block;margin-top:6px}" \
".btns{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}" \
"button{padding:10px 16px;border:none;border-radius:6px;cursor:pointer;font-size:.85rem;font-weight:600;color:#fff;transition:opacity .15s}" \
"button:active{opacity:.6}" \
".btn-tare{background:#267}.btn-start{background:#246}.btn-stop{background:#622}" \
"#btnHeat{background:#853;min-width:150px}" \
"#btnHeat.cdn{background:#a60;animation:pulse .4s infinite alternate}" \
"#btnHeat.on{background:#f50;animation:pulse .2s infinite alternate}" \
"@keyframes pulse{from{opacity:.55}to{opacity:1}}" \
"#stbar{font-size:.8rem;padding:5px 10px;background:#1a1a1a;border-radius:6px;margin-bottom:8px;display:block}" \
"#cdnBig{font-size:4rem;font-weight:900;color:#f80;text-align:center;height:0;overflow:hidden;transition:height .25s,opacity .25s;opacity:0}" \
"#cdnBig.visible{height:72px;opacity:1;margin-bottom:8px}" \
".heat-settings{background:#1a1a1a;border-radius:8px;padding:10px;margin-bottom:10px}" \
".heat-settings h2{font-size:.65rem;color:#666;text-transform:uppercase;margin-bottom:8px}" \
".heat-row{display:flex;align-items:center;gap:8px;margin-bottom:6px;flex-wrap:wrap}" \
".heat-row label{font-size:.78rem;color:#888;width:120px;flex-shrink:0}" \
".heat-row input[type=number]{background:#0f0f0f;color:#4fc;border:1px solid #333;border-radius:4px;padding:6px 8px;width:80px;font-size:.9rem;font-weight:700}" \
".heat-row input[type=number]:focus{border-color:#4fc;outline:none}" \
".heat-row span{font-size:.78rem;color:#666}" \
".apply-btn{background:#267;padding:6px 16px;font-size:.8rem;margin-top:4px;float:right}" \
".tests{margin-top:10px}" \
".test-row{background:#1a1a1a;border-radius:6px;padding:8px 12px;margin-bottom:6px;font-size:.8rem;display:flex;justify-content:space-between;align-items:center}" \
".badge{font-size:.65rem;padding:2px 5px;border-radius:8px;background:#222;margin-right:2px;display:inline-block}" \
".cls-badge{background:#3a2550;color:#d8a;font-weight:700}" \
".dl{color:#4fc;text-decoration:none;font-weight:700}" \
"#impulseBar{height:5px;background:#222;border-radius:3px;margin-top:5px}" \
"#impulseBarFill{height:100%;background:#fa0;border-radius:3px;width:0%;transition:width .15s}" \
"</style>" \
"</head>" \
"<body>" \
"<h1>Hamownia v3.4</h1>" \
"<div id=\"stbar\">Laczenie...</div>" \
"<div id=\"cdnBig\">5</div>" \
"<div class=\"btns\">" \
"<button class=\"btn-tare\" onclick=\"cmd('tare')\">TARE</button>" \
"<button class=\"btn-start\" onclick=\"cmd('start')\">START</button>" \
"<button class=\"btn-stop\" onclick=\"cmd('stop')\">STOP</button>" \
"<button id=\"btnHeat\" onclick=\"toggleHeat()\">GRZALKA</button>" \
"</div>" \
"<div class=\"heat-settings\">" \
"<h2>Ustawienia grzalki</h2>" \
"<div class=\"heat-row\"><label>Czas grzania (s)</label>" \
"<input type=\"number\" id=\"inpTime\" min=\"1\" max=\"60\" value=\"%d\"><span>sekund</span></div>" \
"<div class=\"heat-row\"><label>Moc PWM (%)</label>" \
"<input type=\"number\" id=\"inpPwr\" min=\"0\" max=\"100\" value=\"%d\"><span>procent</span></div>" \
"<div class=\"heat-row\"><label>Opóźnienie (s)</label>" \
"<input type=\"number\" id=\"inpDelay\" min=\"0\" max=\"30\" value=\"%d\"><span>sekund</span></div>" \
"<button class=\"apply-btn\" onclick=\"sendHeatSettings()\">Zastosuj</button>" \
"<div style=\"clear:both\"></div>" \
"</div>" \
"<div class=\"grid\">" \
"<div class=\"card\"><h2>Sila</h2><div class=\"val\" id=\"force\">--</div><small>N</small></div>" \
"<div class=\"card\"><h2>Peak</h2><div class=\"val\" id=\"peak\">--</div><small>N</small></div>" \
"<div class=\"card full\"><h2>Impuls (live)</h2><div class=\"val impulse\" id=\"impulse\">0</div><small>Ns</small>" \
"<div id=\"impulseBar\"><div id=\"impulseBarFill\"></div></div></div>" \
"<div class=\"card\"><h2>Klasa</h2><div class=\"val cls\" id=\"cls\">--</div></div>" \
"<div class=\"card\"><h2>Srednia</h2><div class=\"val\" id=\"avg\">--</div><small>N</small></div>" \
"<div class=\"card\"><h2>VIN</h2><div class=\"val\" id=\"vin\">--</div><small>V</small></div>" \
"<div class=\"card\"><h2>Bateria</h2><div class=\"val\" id=\"bat\">--</div><small>V</small></div>" \
"<div class=\"card full\"><h2>Wykres</h2><canvas id=\"chart\"></canvas></div>" \
"</div>" \
"<div class=\"tests\"><h2 style=\"font-size:.8rem;color:#444;margin-bottom:8px\">OSTATNIE 3 TESTY</h2><div id=\"testList\">Ladowanie...</div></div>" \
"<script>" \
"const wsUrl='ws://'+location.hostname+'/ws';" \
"let socket,MAX_PTS=600,pts=[],impulsePeak=1,wasRec=false,cdnActive=false;" \
"function connect(){socket=new WebSocket(wsUrl);socket.onmessage=onMsg;socket.onclose=()=>{document.getElementById('stbar').textContent='Rozlaczono...';setTimeout(connect,3000);};}" \
"connect();" \
"function onMsg(e){" \
" const d=JSON.parse(e.data);" \
" document.getElementById('force').textContent=(+d.force).toFixed(1);" \
" document.getElementById('peak').textContent=(+d.peak).toFixed(1);" \
" document.getElementById('avg').textContent=(+d.avg).toFixed(1);" \
" document.getElementById('impulse').textContent=(+d.impulse).toFixed(3);" \
" document.getElementById('cls').textContent=d.class;" \
" if(+d.impulse>impulsePeak)impulsePeak=+d.impulse;" \
" document.getElementById('impulseBarFill').style.width=impulsePeak>0?Math.min(100,+d.impulse/impulsePeak*100)+'%':'0%';" \
" const vinEl=document.getElementById('vin');vinEl.textContent=(+d.vin).toFixed(2);vinEl.className='val'+(+d.vin<11.5?' warn':'');" \
" const batEl=document.getElementById('bat');batEl.textContent=(+d.bat).toFixed(2);batEl.className='val'+(+d.bat<3.3?' danger':+d.bat<3.5?' warn':'');" \
" let st='Stan: '+d.state;if(d.rec)st+=' REC '+d.samples+' t='+(+d.burn).toFixed(1)+'s';if(+d.heaterLeft>=0)st+=' GRZANIE: '+d.heaterLeft+'s';" \
" document.getElementById('stbar').textContent=st;" \
" if(d.rec){pts.push(+d.force);if(pts.length>MAX_PTS)pts.shift();drawChart();wasRec=true;}else if(wasRec){wasRec=false;drawChart();loadTests();}" \
" const cdn=+d.countdown,cdnEl=document.getElementById('cdnBig');" \
" if(cdn>=0){cdnEl.textContent=cdn>0?cdn:'FIRE';cdnEl.classList.add('visible');cdnActive=true;}else if(cdnActive){cdnActive=false;setTimeout(()=>cdnEl.classList.remove('visible'),900);}" \
" const btn=document.getElementById('btnHeat');" \
" if(cdn>=0){btn.className='cdn';btn.textContent=' '+cdn+'s...';}else if(+d.heaterLeft>=0){btn.className='on';btn.textContent=' GRZANIE '+d.heaterLeft+'s';}else{btn.className='';btn.textContent='GRZALKA';}" \
"}" \
"function cmd(c){fetch('/api/'+c,{method:'POST'}).then(()=>{if(c==='stop')setTimeout(loadTests,500);});}" \
"function sendHeatSettings(){" \
" const t=parseInt(document.getElementById('inpTime').value)||%d;" \
" const p=parseInt(document.getElementById('inpPwr').value)||%d;" \
" const d=parseInt(document.getElementById('inpDelay').value)||%d;" \
" document.getElementById('inpTime').value=Math.max(1,Math.min(60,t));" \
" document.getElementById('inpPwr').value=Math.max(0,Math.min(100,p));" \
" document.getElementById('inpDelay').value=Math.max(0,Math.min(30,d));" \
" return fetch('/api/heatset?time='+document.getElementById('inpTime').value+'&pwr='+document.getElementById('inpPwr').value+'&delay='+document.getElementById('inpDelay').value,{method:'POST'});" \
"}" \
"function toggleHeat(){sendHeatSettings().then(()=>fetch('/api/heat',{method:'POST'}));}" \
"function drawChart(){" \
" const c=document.getElementById('chart'),ctx=c.getContext('2d');" \
" c.width=c.offsetWidth||320;c.height=c.offsetHeight||190;ctx.clearRect(0,0,c.width,c.height);" \
" if(pts.length<2)return;const max=Math.max(...pts,0.1);ctx.strokeStyle='#222';ctx.lineWidth=1;" \
" for(let i=1;i<4;i++){const y=c.height*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(c.width,y);ctx.stroke();ctx.fillStyle='#444';ctx.font='10px sans-serif';ctx.fillText((max*(4-i)/4).toFixed(0)+'N',2,y-2);}" \
" ctx.strokeStyle='#4fc';ctx.lineWidth=2;ctx.beginPath();" \
" pts.forEach((v,i)=>{const x=i/(pts.length-1)*c.width,y=c.height-(v/max)*c.height*0.88-4;i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);});ctx.stroke();" \
" ctx.lineTo(c.width,c.height);ctx.lineTo(0,c.height);ctx.closePath();ctx.fillStyle='rgba(68,255,200,0.06)';ctx.fill();" \
"}" \
"function loadTests(){fetch('/api/tests').then(r=>r.json()).then(list=>{const el=document.getElementById('testList'),v=list.filter(t=>t&&t.name);if(!v.length){el.textContent='Brak.';return;}el.innerHTML=v.map(t=>'<div class=\"test-row\"><div><strong>'+t.name+'</strong><span class=\"badge cls-badge\">'+t.motor_class+'</span><br><span class=\"badge\">Peak: '+t.peak_N.toFixed(1)+' N</span><span class=\"badge\">t: '+t.burn_s.toFixed(2)+' s</span></div><a class=\"dl\" href=\"/download?id='+t.id+'\" target=\"_blank\">CSV</a></div>').join('');}).catch(()=>{});}" \
"loadTests();setInterval(loadTests,8000);window.addEventListener('resize',drawChart);" \
"</script>" \
"</body>" \
"</html>"

// ============================================================
//  SETUP v3.4
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== HAMOWNIA v3.4 ===");

  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_BT2, INPUT_PULLUP);
  digitalWrite(PIN_STATUS_LED, LOW);
  ledState = LED_BOOT;

  // PWM grzalki (ESP32-C3 Core v3.x)
  ledcAttachPin(PIN_HEATER_EN, 0);
  ledcSetup(0, HEATER_PWM_FREQ, HEATER_PWM_RES);
  ledcWrite(0, 0);

  // OLED
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  bool oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOk) {
    oled.clearDisplay();
    oled.setTextSize(2); oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(4, 0); oled.print("HAMOWNIA");
    oled.setTextSize(1); oled.setCursor(20, 18); oled.print("v3.4 boot...");
    oled.display(); delay(1000);
    oled.clearDisplay();
    oled.setCursor(0, 0); oled.print("-- HAMOWNIA BOOT --");
    oled.display();
  } else { Serial.println("OLED BRAK!"); }

  // HX711
  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  delay(400);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare(10);
  delay(100);
  float testReading = scale.get_units(3);
  bool hxOk = !isnan(testReading);
  Serial.printf("HX711: %s (%.2f)\n", hxOk ? "OK" : "BRAK", testReading);
  if (oledOk) oledBootLine(hxOk ? "HX711 OK" : "HX711 BRAK!", 1, hxOk);

  // SD (JEDEN RAZ)
  SPI.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  delay(50);
  if (SD.begin(PIN_SD_CS)) {
    sdOk = true; loadTestIndex();
    Serial.println("SD: OK");
    if (oledOk) oledBootLine("SD karta OK", 2, true);
  } else {
    sdOk = false;
    Serial.println("SD: BRAK");
    if (oledOk) oledBootLine("SD BRAK/BLAD!", 2, false);
    ledState = LED_ERROR_SD; ledTimer = millis();
  }

  // WiFi (JEDEN RAZ z blokada 15s)
  if (oledOk) oledBootLine("WiFi laczenie...", 3, true);
  Serial.print("WiFi");
  WiFi.disconnect(true); delay(200);
  WiFi.mode(WIFI_STA); delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiConnectStart = millis();
  bool wifiOk = false;
  while (millis() - wifiConnectStart < 15000) {
    if (WiFi.status() == WL_CONNECTED) { wifiOk = true; break; }
    delay(500);
    updateLed();
    Serial.print('.');
    if (oledOk) {
      char wbuf[24];
      snprintf(wbuf, sizeof(wbuf), "WiFi %ds", (int)((millis() - wifiConnectStart) / 1000));
      oledBootLine(wbuf, 3, true);
    }
  }
  if (wifiOk) {
    wifiConnected = true; localIP = WiFi.localIP().toString();
    Serial.printf("\nWiFi OK  IP: %s\n", localIP.c_str());
    if (oledOk) {
      oledBootLine("WiFi OK!", 3, true);
      char ipBuf[26]; snprintf(ipBuf, sizeof(ipBuf), "IP: %s", localIP.c_str());
      oledBootLine(ipBuf, 4, true);
    }
  } else {
    WiFi.mode(WIFI_AP); delay(200);
    WiFi.softAP("Hamownia", "hamownia123"); delay(500);
    wifiConnected = false; localIP = WiFi.softAPIP().toString();
    Serial.printf("\nWiFi FAIL -> AP  IP: %s\n", localIP.c_str());
    if (oledOk) {
      oledBootLine("WiFi FAIL->AP", 3, false);
      char apBuf[26]; snprintf(apBuf, sizeof(apBuf), "AP: %s", localIP.c_str());
      oledBootLine(apBuf, 4, false);
    }
  }

  // Build HTML with config values injected
  snprintf(indexHtmlBuffer, sizeof(indexHtmlBuffer), HTML_TEMPLATE,
    HEATER_DEFAULT_DURATION_S,
    HEATER_DEFAULT_DUTY_PERCENT,
    HEATER_DEFAULT_COUNTDOWN_S,
    HEATER_DEFAULT_DURATION_S,
    HEATER_DEFAULT_DUTY_PERCENT,
    HEATER_DEFAULT_COUNTDOWN_S);

  // WebSocket
  ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType t, void*, uint8_t*, size_t) {
    if (t == WS_EVT_CONNECT) Serial.printf("WS #%u connected\n", client->id());
    else if (t == WS_EVT_DISCONNECT) Serial.printf("WS #%u disconnected\n", client->id());
  });
  server.addHandler(&ws);

  // HTTP routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/html", indexHtmlBuffer);
  });
  server.on("/api/tare", HTTP_POST, [](AsyncWebServerRequest* r) {
    devState = STATE_TARE; ledState = LED_TARE; ledTimer = millis();
    scale.tare(5); forceFiltered = 0; forcePeak = 0; liveImpulse = 0; forceAvg = 0;
    devState = STATE_READY; r->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest* r) {
    sampleCount = 0; forcePeak = 0; liveImpulse = 0; forceAvg = 0; lastSampleTime = 0;
    testStartMs = millis(); recording = true; devState = STATE_RECORDING; ledState = LED_RECORDING;
    r->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (recording) { recording = false; saveCurrentTest(); }
    r->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/heat", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (heaterOn || heaterCdn) { heaterOff(); r->send(200, "application/json", "{\"on\":false}"); }
    else {
      // Auto-start recording immediately when heater is engaged
      if (!recording && sampleCount == 0) {
        sampleCount = 0; liveImpulse = 0; forceAvg = 0; forcePeak = 0; lastSampleTime = 0;
        testStartMs = millis(); recording = true;
        Serial.println("HEAT: REC recording auto-start");
      }
      heaterCdn = true; heaterCdnStart = millis();
      devState = STATE_HEATER_COUNTDOWN; ledState = LED_HEATER_CDN; ledTimer = millis();
      Serial.printf("COUNTDOWN: %lus %d%%\n", heaterMaxOnMs / 1000, heaterPwmDuty);
      r->send(200, "application/json", "{\"on\":true}");
    }
  });
  server.on("/api/heatset", HTTP_POST, [](AsyncWebServerRequest* r) {
    auto g = [&](const char* k)->String{ if(r->hasParam(k,true)) return r->getParam(k,true)->value(); if(r->hasParam(k)) return r->getParam(k)->value(); return ""; };
    String ts = g("time"), ps = g("pwr"), ds = g("delay");
    if (ts.length()) { int t = ts.toInt(); if (t >= 1 && t <= 60) heaterMaxOnMs = (unsigned long)t * 1000; }
    if (ps.length()) { int p = ps.toInt(); if (p >= 0 && p <= 100) heaterPwmDuty = p; }
    if (ds.length()) { int d = ds.toInt(); if (d >= 0 && d <= 30) HEATER_COUNTDOWN_MS = (unsigned long)d * 1000; }
    char buf[64]; snprintf(buf, sizeof(buf), "{\"time\":%lu,\"pwr\":%d}", heaterMaxOnMs / 1000, heaterPwmDuty);
    r->send(200, "application/json", buf);
  });
  server.on("/api/tests", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < 3; i++) {
      if (!testIndex[i].valid) continue;
      JsonObject o = arr.add<JsonObject>();
      o["id"] = i; o["name"] = testIndex[i].name;
      o["peak_N"] = testIndex[i].peak_N; o["avg_N"] = testIndex[i].avg_N;
      o["burn_s"] = testIndex[i].burn_s; o["impulse_Ns"] = testIndex[i].impulse_Ns;
      o["motor_class"] = testIndex[i].motor_class;
    }
    String out; serializeJson(arr, out); r->send(200, "application/json", out);
  });
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("id")) { r->send(400); return; }
    int id = r->getParam("id")->value().toInt();
    if (id < 0 || id > 2 || !testIndex[id].valid || !sdOk) { r->send(404); return; }
    r->send(SD, ("/test" + String(id) + ".csv").c_str(), "text/csv", true);
  });
  server.begin();
  Serial.println("HTTP server OK");
  if (oledOk) oledBootLine("WWW serwer OK", 5, true);

  delay(2000);
  devState = STATE_READY;
  ledState = (sdOk ? LED_IDLE : LED_ERROR_SD);
  Serial.println("=== GOTOWY ===");
}

// ============================================================
//  LOOP
// ============================================================
unsigned long oledTimer = 0, sampleTimer = 0;
const unsigned long SAMPLE_INTERVAL_MS = 12, VOLTAGE_INTERVAL_MS = 2000;

void loop() {
  unsigned long now = millis();
  updateLed();
  handleButton();
  handleHeater();

  if (now - voltageTimer >= VOLTAGE_INTERVAL_MS) {
    voltageTimer = now;
    vinVoltage = readVoltage(PIN_BAT_ADC);
    batVoltage = readVoltage(PIN_BAT_ADC2);
  }
  if (now - sampleTimer >= SAMPLE_INTERVAL_MS) {
    sampleTimer = now;
    if (scale.is_ready()) {
      forceRaw = scale.get_units(1);
      if (forceRaw < 0) forceRaw = 0;
      forceFiltered = EMA_ALPHA * forceRaw + (1.0f - EMA_ALPHA) * forceFiltered;
      if (forceFiltered > forcePeak) forcePeak = forceFiltered;
      if (recording && sampleCount < MAX_SAMPLES) {
        float t_now = (now - testStartMs) / 1000.0f;
        if (sampleCount > 0) { float dt = t_now - lastSampleTime; if (dt > 0 && dt < 0.5f) liveImpulse += forceFiltered * dt; }
        lastSampleTime = t_now;
        if (t_now > 0.001f) forceAvg = liveImpulse / t_now;
        samples[sampleCount] = { t_now, forceFiltered }; sampleCount++;
      }
    }
  }
  if (now - oledTimer >= 200) { oledTimer = now; updateOled(); }
  wsPush();
  ws.cleanupClients();
  delay(1);
}