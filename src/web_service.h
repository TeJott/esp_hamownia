/**
 * @file web_service.h
 * @brief Web server and WebSocket service for ESP32-C3 Hamownia
 * 
 * Provides:
 * - HTTP server with embedded HTML
 * - REST API endpoints
 * - WebSocket for live updates
 * - Wi-Fi STA + AP simultaneous mode (like working Arduino code)
 * 
 * Fixes:
 * - Added blocking STA wait in begin() so WiFi connects before proceeding
 * - Added IP update callback so display updates when STA connects
 * - Added STA reconnection logic in update()
 * - Removed large delays from begin() for stability
 * - Fixed TX power to use valid ESP32-C3 enum values
 */

#ifndef WEB_SERVICE_H
#define WEB_SERVICE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "config.h"
#include "app_state.h"

// Forward declarations
class HX711Service;
class BatteryService;
class HeaterService;
class StorageService;

class WebService {
public:
    WebService() : server(WEB_SERVER_PORT), webSocket(WEBSOCKET_PORT),
                   initialized(false), lastWsUpdate(0), wifiStartTime(0), 
                   wifiConnecting(false), lastStaReconnectAttempt(0) {}
    
    void begin(AppContext& ctx) {
        DEBUG_PRINT("[WEB] Starting WiFi...");
        delay(WIFI_BOOT_DELAY_MS);
        
        // Reduce WiFi power
        WiFi.setTxPower((wifi_power_t)WIFI_TX_POWER);
        
        // Start in AP+STA mode
        WiFi.mode(WIFI_AP_STA);
        delay(100);
        
        // Start AP with retries (non-blocking delays)
        bool apOK = false;
        for (int retries = 0; retries < 3; retries++) {
            DEBUG_PRINTF("[WEB] AP attempt %d...\n", retries + 1);
            
            apOK = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CONNECTIONS);
            
            if (apOK) {
                break;
            }
            
            DEBUG_PRINTF("[WEB] AP failed (attempt %d). Retrying...\n", retries + 1);
            WiFi.mode(WIFI_OFF);
            delay(200);
            WiFi.mode(WIFI_AP_STA);
            delay(100);
        }
        
        if (apOK) {
            DEBUG_PRINT("[WEB] AP softAP() returned SUCCESS");
            ctx.apMode = true;
            ctx.wifiConnected = false;
            String apIP = WiFi.softAPIP().toString();
            apIP.toCharArray(ctx.ipAddress, sizeof(ctx.ipAddress));
            DEBUG_PRINTF("[WEB] AP started: %s, IP: %s\n", AP_SSID, ctx.ipAddress);
        } else {
            DEBUG_PRINT("[WEB] CRITICAL: AP failed to start after 3 attempts!");
            DEBUG_PRINT("[WEB] Try: GPIO0 pull-up, check AMS1117 voltage");
            ctx.apMode = false;
            ctx.wifiConnected = false;
            strncpy(ctx.ipAddress, "AP FAILED", sizeof(ctx.ipAddress) - 1);
        }
        
        // Try connecting to home WiFi - BLOCKING wait with yield()
        DEBUG_PRINTF("[WEB] Connecting to STA: %s (timeout: %dms)...\n", WIFI_SSID, WIFI_CONNECT_TIMEOUT_MS);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifiStartTime = millis();
        
        // Block here and wait for STA to connect (with yield for background tasks)
        uint32_t staTimeout = WIFI_CONNECT_TIMEOUT_MS;
        uint32_t staStart = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - staStart < staTimeout)) {
            delay(50);  // Small delay allows WiFi driver to process
            yield();    // Keep ESP watchdog happy
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            ctx.wifiConnected = true;
            ctx.apMode = true;  // AP is still active too
            String staIP = WiFi.localIP().toString();
            staIP.toCharArray(ctx.ipAddress, sizeof(ctx.ipAddress));
            DEBUG_PRINTF("[WEB] STA connected! IP: %s\n", ctx.ipAddress);
            wifiConnecting = false;
            
            // Notify main that IP changed (display update)
            if (onIpChanged) {
                onIpChanged(ctx.ipAddress, !ctx.apMode);
            }
        } else {
            DEBUG_PRINTF("[WEB] STA not connected (status=%d). Will retry in loop.\n", WiFi.status());
            wifiConnecting = true;  // Will retry in update()
        }
        
        // Setup HTTP routes
        setupRoutes();
        
        // Start HTTP server
        server.begin();
        DEBUG_PRINT("[WEB] HTTP server started");
        
        // Start WebSocket
        webSocket.begin();
        webSocket.onEvent([this](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
            this->webSocketEvent(num, type, payload, length);
        });
        DEBUG_PRINT("[WEB] WebSocket server started");
        
        initialized = true;
    }
    
    void update(AppContext& ctx) {
        if (!initialized) return;
        
        // Non-blocking WiFi connection check AND reconnection
        if (wifiConnecting) {
            wl_status_t status = WiFi.status();
            if (status == WL_CONNECTED) {
                wifiConnecting = false;
                ctx.wifiConnected = true;
                String staIP = WiFi.localIP().toString();
                staIP.toCharArray(ctx.ipAddress, sizeof(ctx.ipAddress));
                DEBUG_PRINTF("[WEB] STA connected! IP: %s (AP still active)\n", ctx.ipAddress);
                
                // Notify main that IP changed
                if (onIpChanged) {
                    onIpChanged(ctx.ipAddress, !ctx.apMode);
                }
            } else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
                wifiConnecting = false;
                DEBUG_PRINTF("[WEB] STA failed (status=%d). AP still active.\n", status);
            } else if (millis() - wifiStartTime > WIFI_CONNECT_TIMEOUT_MS) {
                wifiConnecting = false;
                DEBUG_PRINT("[WEB] STA timeout (AP still active)");
            }
        }
        
        // STA reconnection: if WiFi was connected but dropped, try to reconnect
        if (!wifiConnecting && ctx.wifiConnected && WiFi.status() != WL_CONNECTED) {
            uint32_t now = millis();
            if (now - lastStaReconnectAttempt > 10000) {  // Retry every 10s
                lastStaReconnectAttempt = now;
                ctx.wifiConnected = false;
                DEBUG_PRINT("[WEB] STA disconnected! Reconnecting...\n");
                WiFi.reconnect();
                wifiStartTime = now;
                wifiConnecting = true;
            }
        }
        
        server.handleClient();
        webSocket.loop();
        
        uint32_t now = millis();
        if (now - lastWsUpdate >= WS_UPDATE_INTERVAL_MS) {
            lastWsUpdate = now;
            broadcastStatus(ctx);
        }
    }
    
    void setServices(HX711Service* hx711, BatteryService* battery, 
                     HeaterService* heater, StorageService* storage) {
        hx711Service = hx711;
        batteryService = battery;
        heaterService = heater;
        storageService = storage;
    }
    
    void setTareCallback(void (*callback)()) {
        tareCallback = callback;
    }
    
    void setOnIpChanged(void (*callback)(const char* ip, bool isSta)) {
        onIpChanged = callback;
    }

private:
    WebServer server;
    WebSocketsServer webSocket;
    bool initialized;
    uint32_t lastWsUpdate;
    uint32_t wifiStartTime;
    bool wifiConnecting;
    uint32_t lastStaReconnectAttempt;
    
    HX711Service* hx711Service = nullptr;
    BatteryService* batteryService = nullptr;
    HeaterService* heaterService = nullptr;
    StorageService* storageService = nullptr;
    
    void (*tareCallback)() = nullptr;
    void (*onIpChanged)(const char* ip, bool isSta) = nullptr;
    
    void setupRoutes() {
        server.on("/", [this]() { this->handleRoot(); });
        server.on("/api/status", HTTP_GET, [this]() { this->handleApiStatus(); });
        server.on("/api/tare", HTTP_POST, [this]() { this->handleApiTare(); });
        server.on("/api/heater/start", HTTP_POST, [this]() { this->handleApiHeaterStart(); });
        server.on("/api/heater/stop", HTTP_POST, [this]() { this->handleApiHeaterStop(); });
        server.on("/api/tests", HTTP_GET, [this]() { this->handleApiTests(); });
        server.on("/download", HTTP_GET, [this]() { this->handleDownload(); });
        server.on("/api/recording/start", HTTP_POST, [this]() { this->handleApiRecordingStart(); });
        server.on("/api/recording/stop", HTTP_POST, [this]() { this->handleApiRecordingStop(); });
        server.onNotFound([this]() { this->handleNotFound(); });
        server.enableCORS(true);
    }
    
    void handleRoot() {
        DEBUG_PRINT("[WEB] Serving root page");
        server.send(200, "text/html", INDEX_HTML);
    }
    
    void handleApiStatus() {
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    }
    
    void handleApiTare() {
        if (tareCallback) {
            tareCallback();
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Tare completed\"}");
        } else {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"No tare callback\"}");
        }
    }
    
    void handleApiHeaterStart() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"No body\"}");
            return;
        }
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Heater start requested\"}");
    }
    
    void handleApiHeaterStop() {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Heater stop requested\"}");
    }
    
    void handleApiTests() {
        server.send(200, "application/json", "{\"tests\":[]}");
    }
    
    void handleDownload() {
        if (!server.hasArg("id")) {
            server.send(400, "text/plain", "Missing id parameter");
            return;
        }
        server.send(404, "text/plain", "File not found");
    }
    
    void handleApiRecordingStart() {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Recording start requested\"}");
    }
    
    void handleApiRecordingStop() {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Recording stop requested\"}");
    }
    
    void handleNotFound() {
        server.send(404, "text/plain", "Not found");
    }
    
    void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                DEBUG_PRINTF("[WS] Client %u disconnected\n", num);
                break;
            case WStype_CONNECTED:
                DEBUG_PRINTF("[WS] Client %u connected\n", num);
                break;
            case WStype_TEXT:
                if (payload[0] == 't' && tareCallback) tareCallback();
                break;
            default:
                break;
        }
    }
    
    void broadcastStatus(const AppContext& ctx) {
        String json = "{";
        json += "\"state\":\"" + String(stateToString(ctx.currentState)) + "\",";
        json += "\"force\":" + String(ctx.forceFiltered * HX711_SCALE_FACTOR, 2) + ",";
        json += "\"peak\":" + String(ctx.forcePeak * HX711_SCALE_FACTOR, 2) + ",";
        json += "\"logicBatt\":" + String(ctx.logicBatteryVoltage, 2) + ",";
        json += "\"heaterBatt\":" + String(ctx.heaterBatteryVoltage, 2) + ",";
        json += "\"heaterRunning\":" + String(ctx.heaterRunning ? "true" : "false") + ",";
        json += "\"heaterCountdown\":" + String(ctx.heaterCountdownRemaining) + ",";
        json += "\"isRecording\":" + String(ctx.isRecording ? "true" : "false") + ",";
        json += "\"wifiConnected\":" + String(ctx.wifiConnected ? "true" : "false") + ",";
        json += "\"apMode\":" + String(ctx.apMode ? "true" : "false");
        json += "}";
        webSocket.broadcastTXT(json);
    }
    
    static const char INDEX_HTML[];
};

const char WebService::INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="pl">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Hamownia ESP32-C3</title>
    <style>
        * { box-sizing:border-box; margin:0; padding:0; }
        body { font-family:Arial,sans-serif; background:#1a1a2e; color:#eee; padding:10px; }
        .container { max-width:800px; margin:0 auto; }
        h1 { text-align:center; color:#4ecca3; margin-bottom:20px; }
        .card { background:#16213e; border-radius:10px; padding:15px; margin-bottom:15px; }
        .card h2 { color:#4ecca3; font-size:1.1em; margin-bottom:10px; border-bottom:1px solid #4ecca3; padding-bottom:5px; }
        .grid { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
        .value { font-size:2em; color:#4ecca3; }
        .label { font-size:0.8em; color:#888; }
        .status { padding:5px 10px; border-radius:5px; display:inline-block; }
        .status.idle { background:#2d4a3e; color:#4ecca3; }
        .status.recording { background:#4a4a2d; color:#ffe66d; }
        .status.heating { background:#4a2d2d; color:#ff6b6b; }
        .status.error { background:#4a2d2d; color:#ff6b6b; }
        button { background:#4ecca3; color:#1a1a2e; border:none; padding:10px 20px; border-radius:5px; cursor:pointer; font-size:1em; margin:5px; }
        button:hover { background:#3db892; }
        button.danger { background:#e94560; }
        button.danger:hover { background:#c73e54; }
        input,select { background:#0f3460; color:#eee; border:1px solid #4ecca3; padding:8px; border-radius:5px; margin:5px 0; width:100%; }
        .chart-container { background:#0f3460; border-radius:5px; height:200px; overflow:hidden; }
        canvas { width:100%; height:100%; }
        .heater-controls { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
        @media(max-width:500px) { .grid,.heater-controls { grid-template-columns:1fr; } }
    </style>
</head>
<body>
<div class="container">
<h1>Hamownia ESP32-C3</h1>
<div class="card"><h2>Status</h2>
<div class="grid">
<div><div class="label">Stan</div><div class="status idle" id="state">BOOT</div></div>
<div><div class="label">WiFi</div><div id="wifiInfo">AP: Hamownia (192.168.4.1)</div></div>
<div><div class="label">Logika</div><div class="value" id="logicBatt">0.0V</div></div>
<div><div class="label">Grzałka</div><div class="value" id="heaterBatt">0.0V</div></div>
</div></div>
<div class="card"><h2>Siła</h2>
<div class="grid">
<div><div class="label">Aktualna</div><div class="value" id="force">0.0 kg</div></div>
<div><div class="label">Szczytowa</div><div class="value" id="peak">0.0 kg</div></div>
</div>
<button onclick="tare()">Tare</button></div>
<div class="card"><h2>Wykres</h2><div class="chart-container"><canvas id="chart"></canvas></div></div>
<div class="card"><h2>Grzałka</h2>
<div id="heaterMsg"></div>
<div class="heater-controls">
<div><label class="label">Odliczanie (s)</label><input type="number" id="countdown" value="5" min="0" max="60"></div>
<div><label class="label">Tryb</label><select id="mode"><option value="full_on">Pełna</option><option value="pwm">PWM</option></select></div>
<div><label class="label">Czas (ms)</label><input type="number" id="duration" value="5000" min="100" max="60000"></div>
<div><label class="label">PWM (%)</label><input type="number" id="duty" value="50" min="0" max="100"></div>
</div>
<button onclick="startHeater()">Start</button>
<button class="danger" onclick="stopHeater()">Stop</button></div>
<div class="card"><h2>Nagrywanie</h2>
<button id="recBtn" onclick="toggleRec()">Start</button></div>
</div>
<script>
let ws, data=[], maxP=100, rec=false;
function connect(){ws=new WebSocket('ws://'+location.hostname+':81/');ws.onmessage=e=>update(JSON.parse(e.data));ws.onclose=()=>setTimeout(connect,2000);}
function update(d){
    document.getElementById('state').textContent=d.state;
    document.getElementById('state').className='status '+(d.state||'').toLowerCase();
    document.getElementById('force').textContent=d.force.toFixed(2)+' kg';
    document.getElementById('peak').textContent=d.peak.toFixed(2)+' kg';
    document.getElementById('logicBatt').textContent=d.logicBatt.toFixed(2)+'V';
    document.getElementById('heaterBatt').textContent=d.heaterBatt.toFixed(2)+'V';
    document.getElementById('wifiInfo').textContent=d.wifiConnected?'STA: '+location.hostname:'AP: Hamownia';
    data.push(d.force);if(data.length>maxP)data.shift();drawChart();
    var m=document.getElementById('heaterMsg');
    if(d.heaterRunning&&d.heaterCountdown>0)m.innerHTML='Odliczanie: '+d.heaterCountdown+'s';
    else if(d.heaterRunning)m.innerHTML='Grzanie!';
    else m.innerHTML='';
    rec=d.isRecording;document.getElementById('recBtn').textContent=rec?'Stop Rec':'Start Rec';
}
function drawChart(){
    var c=document.getElementById('chart'),cx=c.getContext('2d'),w=c.width=c.offsetWidth,h=c.height=c.offsetHeight;
    cx.fillStyle='#0f3460';cx.fillRect(0,0,w,h);if(data.length<2)return;
    var mx=Math.max(...data,1),mn=Math.min(...data,0),r=mx-mn||1;
    cx.strokeStyle='#4ecca3';cx.lineWidth=2;cx.beginPath();
    for(var i=0;i<data.length;i++){var x=i/(maxP-1)*w,y=h-(data[i]-mn)/r*(h-20)-10;i===0?cx.moveTo(x,y):cx.lineTo(x,y);}
    cx.stroke();
}
function tare(){fetch('/api/tare',{method:'POST'});}
function startHeater(){fetch('/api/heater/start',{method:'POST'});}
function stopHeater(){fetch('/api/heater/stop',{method:'POST'});}
function toggleRec(){fetch(rec?'/api/recording/stop':'/api/recording/start',{method:'POST'});}
connect();
</script>
</body>
</html>
)=====";

#endif // WEB_SERVICE_H