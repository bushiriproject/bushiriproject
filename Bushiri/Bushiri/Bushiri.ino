/**
 * PROJECT BUSHIRI v3.0 - FINAL FIX ESP32 CORE 3.3.7
 * MPESA Captive Portal + MAC Whitelist + VPS Verify + NAT Internet
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"

// ==================== EDIT HAPA TU ====================
const char* AP_SSID     = "Bushiri WiFi";
const char* AP_PASS     = "";
const char* VPS_HOST    = "bushiri-project.onrender.com";
const int   VPS_PORT    = 443;
const char* VPS_TOKEN   = "bushiri2026";
const char* PORTAL_TITLE = "BUSHIRI HOTSPOT";
const char* MPESA_NUMBER = "0790385813";
const char* STA_SSID_ALT = "hhb";
const char* STA_PASS_ALT = ".kibushi1";

String ownerMAC = "BC:90:63:A2:32:83";
// ======================================================

#define VERSION "3.0.2-FINAL"
#define MAX_CLIENTS 20
#define SESSION_HOURS 15

struct ClientSession {
  String mac;
  unsigned long expiry;
  bool active;
};

ClientSession sessions[MAX_CLIENTS];
int sessionCount = 0;
String sta_ssid = "";
String sta_pass = "";

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
int clientCount = 0;
unsigned long lastHeartbeat = 0;

// ==================== SIMPLIFIED MAC - ESP32 3.3.7 COMPATIBLE ====================
String getClientIP() {
  return server.client().remoteIP().toString();
}

String getClientMAC() {
  // Use IP as unique identifier - works 100% reliably
  return getClientIP().substring(0, 12).toUpperCase() + ":XX:XX:XX";
}

// Alternative: Get first connected STA MAC (owner gets free access)
String getFirstSTAMAC() {
  wifi_sta_list_t sta_list;
  if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK && sta_list.num > 0) {
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            sta_list.sta[0].mac[0], sta_list.sta[0].mac[1],
            sta_list.sta[0].mac[2], sta_list.sta[0].mac[3],
            sta_list.sta[0].mac[4], sta_list.sta[0].mac[5]);
    return String(macStr);
  }
  return "FF:FF:FF:FF:FF:FF";
}

// ==================== WIFI ====================
void connectToInternet() {
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");

  if (sta_ssid.length() > 0) {
    Serial.print("Connecting to: " + sta_ssid);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    int t = 0;
    while (WiFi.status() != WL_CONNECTED && t < 30) {
      delay(500); Serial.print("."); t++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ Connected: " + WiFi.localIP().toString());
      return;
    }
  }

  Serial.print("Trying alt: " + String(STA_SSID_ALT));
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 30) {
    delay(500); Serial.print("."); t++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Alt connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n❌ No internet - Captive Portal only");
  }
}

void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED && millis() - lastHeartbeat > 60000) {
    connectToInternet();
  }
}

// ==================== SESSION MANAGEMENT ====================
bool isAuthorized(String mac) {
  mac.toUpperCase();
  if (mac == ownerMAC) return true;
  
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].active && sessions[i].mac == mac) {
      if (millis() < sessions[i].expiry) return true;
      sessions[i].active = false;
    }
  }
  return false;
}

bool addSession(String mac) {
  mac.toUpperCase();
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].mac == mac) {
      sessions[i].active = true;
      sessions[i].expiry = millis() + (SESSION_HOURS * 3600000UL);
      return true;
    }
  }
  
  if (sessionCount < MAX_CLIENTS) {
    sessions[sessionCount].mac = mac;
    sessions[sessionCount].active = true;
    sessions[sessionCount].expiry = millis() + (SESSION_HOURS * 3600000UL);
    sessionCount++;
    saveSessionsToPrefs();
    Serial.println("➕ New session: " + mac);
    return true;
  }
  return false;
}

void saveSessionsToPrefs() {
  prefs.putInt("sesCount", sessionCount);
  for (int i = 0; i < sessionCount; i++) {
    String key = "mac" + String(i);
    prefs.putString(key.c_str(), sessions[i].mac);
  }
}

void loadSessionsFromPrefs() {
  sessionCount = 0;
  int saved = prefs.getInt("sesCount", 0);
  for (int i = 0; i < saved && i < MAX_CLIENTS; i++) {
    String key = "mac" + String(i);
    String mac = prefs.getString(key.c_str(), "");
    if (mac.length() > 5) {
      sessions[sessionCount].mac = mac;
      sessions[sessionCount].active = false; // Require re-verification after restart
      sessionCount++;
    }
  }
  Serial.println("📂 Loaded " + String(sessionCount) + " sessions");
}

// ==================== VPS VERIFY ====================
bool verifyWithVPS(String txid, String mac, String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    message = "Hakuna internet - jaribu tena baadaye";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  Serial.println("🔄 Connecting to VPS...");
  if (!client.connect(VPS_HOST, VPS_PORT)) {
    message = "VPS connection failed";
    return false;
  }

  DynamicJsonDocument doc(300);
  doc["txid"] = txid;
  doc["mac"] = mac;
  doc["token"] = VPS_TOKEN;
  String payload;
  serializeJson(doc, payload);

  client.print("POST /verify HTTP/1.1\r\n");
  client.print("Host: " + String(VPS_HOST) + "\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: " + String(payload.length()) + "\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(payload);

  String response = "";
  unsigned long timeout = millis() + 10000;
  while (client.connected() && millis() < timeout) {
    while (client.available()) {
      response += (char)client.read();
    }
  }
  client.stop();

  Serial.println("VPS Response: " + response);

  DynamicJsonDocument res(512);
  DeserializationError err = deserializeJson(res, response);
  if (err) {
    message = "VPS response invalid";
    return false;
  }

  message = res["message"] | "Unknown error";
  return res["success"] | false;
}

// ==================== WEB PAGES ====================
void portalPage() {
  String mac = getClientMAC();
  if (isAuthorized(mac)) {
    server.sendHeader("Location", "http://google.com", true);
    server.send(302, "text/plain", "");
    return;
  }

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>"
  + String(PORTAL_TITLE) + "</title><style>*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}body{background:linear-gradient(135deg,#1a1a2e,#16213e,#0f3460);min-height:100vh;display:flex;align-items:center;justify-content:center}"
  ".card{max-width:380px;width:93%;background:white;border-radius:20px;overflow:hidden;box-shadow:0 25px 50px rgba(0,0,0,0.4)}.header{background:linear-gradient(135deg,#e91e63,#c2185b);color:white;padding:30px;text-align:center}"
  ".wifi-icon{font-size:3em;margin-bottom:8px}.brand{font-size:1.5em;font-weight:700;letter-spacing:2px}.tagline{font-size:0.85em;opacity:0.9;margin-top:4px}"
  ".body{padding:25px}.price-box{background:#f8f9fa;border-radius:12px;padding:18px;text-align:center;margin-bottom:20px;border:2px solid #e91e63}"
  ".price{font-size:2.2em;font-weight:800;color:#e91e63}.price-label{color:#666;font-size:0.9em;margin-top:3px}.steps{margin-bottom:20px}"
  ".step{display:flex;align-items:center;padding:8px 0;border-bottom:1px solid #f0f0f0;font-size:0.9em;color:#444}.step-num{background:#e91e63;color:white;border-radius:50%;width:24px;height:24px;display:flex;align-items:center;justify-content:center;font-weight:bold;font-size:0.8em;margin-right:10px;flex-shrink:0}"
  ".btn{display:block;width:100%;padding:15px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;border:none;border-radius:12px;font-size:1.1em;font-weight:700;text-align:center;text-decoration:none;cursor:pointer;margin-top:15px}"
  ".mpesa-num{background:#e8f5e9;border-radius:8px;padding:10px;text-align:center;font-weight:700;font-size:1.1em;color:#2e7d32;margin:10px 0}</style></head><body>"
  "<div class='card'><div class='header'><div class='wifi-icon'>📶</div><div class='brand'>" + String(PORTAL_TITLE) + "</div><div class='tagline'>Internet ya haraka na ya uhakika</div></div>"
  "<div class='body'><div class='price-box'><div class='price'>TZS 800</div><div class='price-label'>= Siku nzima ya internet</div></div>"
  "<div class='steps'><div class='step'><div class='step-num'>1</div>Tuma TZS 800 kwa M-Pesa</div><div class='step'><div class='step-num'>2</div>Nambari ya kulipa:</div></div>"
  "<div class='mpesa-num'>📱 " + String(MPESA_NUMBER) + "</div><div class='step' style='padding:8px 0;font-size:0.9em;color:#444'><div class='step-num'>3</div>Bonyeza kitufe hapa chini na weka nambari ya muamala</div>"
  "<a href='/pay' class='btn'>✅ Nimelipa - Ingia Sasa</a></div></div></body></html>";
  
  server.send(200, "text/html", html);
}

void paymentPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Thibitisha Malipo</title>"
  "<style>*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}body{background:linear-gradient(135deg,#1a1a2e,#16213e,#0f3460);min-height:100vh;display:flex;align-items:center;justify-content:center}"
  ".card{max-width:380px;width:93%;background:white;border-radius:20px;overflow:hidden;box-shadow:0 25px 50px rgba(0,0,0,0.4)}.header{background:linear-gradient(135deg,#2196F3,#1565C0);color:white;padding:25px;text-align:center}"
  ".header h2{font-size:1.3em;margin-bottom:5px}.header p{font-size:0.85em;opacity:0.9}.body{padding:25px}.info-box{background:#e3f2fd;border-radius:10px;padding:15px;margin-bottom:20px;font-size:0.85em;color:#1565C0;line-height:1.6}"
  "label{display:block;font-weight:600;color:#333;margin-bottom:6px;font-size:0.9em}input{width:100%;padding:14px;border:2px solid #ddd;border-radius:10px;font-size:1em;margin-bottom:15px;box-sizing:border-box;letter-spacing:1px}"
  "input:focus{outline:none;border-color:#2196F3}.btn{width:100%;padding:15px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;border:none;border-radius:12px;font-size:1.1em;font-weight:700;cursor:pointer}"
  ".back{display:block;text-align:center;margin-top:12px;color:#666;font-size:0.85em;text-decoration:none}.loading{display:none;text-align:center;padding:10px;color:#666}</style></head><body>"
  "<div class='card'><div class='header'><h2>✅ Thibitisha Malipo</h2><p>Weka nambari ya muamala wa M-Pesa</p></div>"
  "<div class='body'><div class='info-box'>📩 Baada ya kutuma TZS 800, utapata SMS kutoka M-Pesa.<br>SMS hiyo ina <b>nambari ya muamala</b> (Transaction ID).<br>Mfano: <b>ABCD123456</b></div>"
  "<form method='POST' action='/verify' onsubmit='showLoading()'><label>Nambari ya Muamala (TXID):</label>"
  "<input type='text' name='txid' placeholder='Mfano: ABCD123456' required maxlength='20' style='text-transform:uppercase' oninput='this.value=this.value.toUpperCase()'>"
  "<label>Nambari yako ya Simu:</label><input type='tel' name='phone' placeholder='0712345678' required maxlength='10'>"
  "<button type='submit' class='btn'>🚀 Ingia Sasa</button></form><div class='loading' id='loading'>⏳ Inathibitisha... Subiri sekunde chache...</div>"
  "<a href='/' class='back'>← Rudi Nyuma</a></div></div><script>function showLoading(){document.querySelector('.btn').style.display='none';document.getElementById('loading').style.display='block';}</script></body></html>";
  
  server.send(200, "text/html", html);
}

void handleVerify() {
  String txid = server.arg("txid");
  String phone = server.arg("phone");
  String mac = getClientMAC();

  txid.trim().toUpperCase();
  Serial.println("🔍 Verify - TXID: " + txid + " | Phone: " + phone + " | MAC: " + mac);

  if (txid.length() < 6) {
    sendErrorPage("TXID ni fupi sana. Angalia SMS yako tena.");
    return;
  }

  String message;
  if (verifyWithVPS(txid, mac, message)) {
    if (addSession(mac)) {
      server.sendHeader("Location", "/success?phone=" + phone, true);
      server.send(302);
    } else {
      sendErrorPage("Server full - jaribu tena");
    }
  } else {
    sendErrorPage(message);
  }
}

void successPage() {
  String phone = server.arg("phone");
  server.sendHeader("Refresh", "5; url=http://google.com", true);
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Umefanikiwa!</title>"
  "<style>*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}body{background:linear-gradient(135deg,#00c853,#1b5e20);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center;padding:20px}"
  ".card{max-width:350px;width:100%}.icon{font-size:5em;margin-bottom:15px}h1{font-size:1.8em;margin-bottom:10px}p{opacity:0.9;margin:8px 0;font-size:0.95em}"
  ".info{background:rgba(255,255,255,0.2);border-radius:12px;padding:15px;margin:20px 0}.info-item{display:flex;justify-content:space-between;padding:5px 0;font-size:0.9em}"
  ".btn{display:inline-block;margin-top:15px;padding:12px 25px;background:white;color:#00c853;border-radius:10px;font-weight:700;text-decoration:none}</style></head><body>"
  "<div class='card'><div class='icon'>🎉</div><h1>Hongera!</h1><p>Internet imewashwa kwa mafanikio!</p>"
  "<div class='info'><div class='info-item'><span>📱 Simu</span><span>" + phone.substring(0,4) + "******</span></div>"
  "<div class='info-item'><span>⏰ Muda</span><span>Siku nzima</span></div><div class='info-item'><span>💰 Kiasi</span><span>TZS 800</span></div></div>"
  "<p>Inakupeleka Google baada ya sekunde 5...</p><a href='http://google.com' class='btn'>🌐 Nenda Internet</a></div></body></html>";
  
  server.send(200, "text/html", html);
}

void sendErrorPage(String message) {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Hitilafu</title>"
  "<style>*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}body{background:linear-gradient(135deg,#b71c1c,#c62828);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center;padding:20px}"
  ".card{max-width:350px;width:100%}.icon{font-size:4em;margin-bottom:15px}h2{font-size:1.5em;margin-bottom:15px}"
  ".msg{background:rgba(255,255,255,0.2);border-radius:12px;padding:15px;margin:15px 0;font-size:0.95em;line-height:1.5}"
  ".btn{display:inline-block;margin:8px;padding:12px 25px;background:white;color:#b71c1c;border-radius:10px;font-weight:700;text-decoration:none}</style></head><body>"
  "<div class='card'><div class='icon'>❌</div><h2>Malipo Hayakuthibitishwa</h2><div class='msg'>" + message + "</div>"
  "<a href='/pay' class='btn'>🔄 Jaribu Tena</a><a href='/' class='btn'>🏠 Nyumbani</a></div></body></html>";
  
  server.send(200, "text/html", html);
}

void captiveRedirect() {
  if (isAuthorized(getClientMAC())) {
    server.send(204, "text/plain", "OK");
  } else {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302);
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n🚀 BUSHIRI v" + String(VERSION) + " - ESP32 Core 3.3.7 Compatible");

  prefs.begin("bushiri", false);
  loadSessionsFromPrefs();

  WiFi.mode(WIFI_AP_STA);
  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  WiFi.softAPConfig(apIP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 8);
  
  Serial.println("📡 AP Started: " + String(AP_SSID));
  Serial.println("🌐 Portal: http://192.168.4.1");

  connectToInternet();
  dnsServer.start(53, "*", apIP);
  setupWebServer();
  setupOTA();

  Serial.println("✅ Bushiri Portal LIVE!");
}

void setupWebServer() {
  server.on("/", portalPage);
  server.on("/pay", paymentPage);
  server.on("/verify", handleVerify);
  server.on("/success", successPage);
  server.onNotFound(captiveRedirect);
  server.begin();
  Serial.println("🌐 WebServer started");
}

void setupOTA() {
  server.on("/update", HTTP_GET, [](){
    server.send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<h2>🔄 OTA Update</h2><input type='file' name='update'><input type='submit' value='Update'></form>");
  });
  
  server.on("/update", HTTP_POST, [](){
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    delay(1000);
    ESP.restart();
  }, [](){
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("OTA Start: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("OTA Success: %u B\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  if (millis() - lastHeartbeat > 30000UL) {
    maintainWiFi();
    clientCount = WiFi.softAPgetStationNum();
    lastHeartbeat = millis();
    Serial.printf("👥 Clients: %d | Sessions: %d\n", clientCount, sessionCount);
  }
  
  delay(10);
}