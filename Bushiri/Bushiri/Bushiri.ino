/**
 * PROJECT BUSHIRI v3.0 - PERFECT ESP32 CORE 3.3.7
 * MPESA Captive Portal + MAC Whitelist + VPS Verify
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

// ==================== CONFIG ====================
const char* AP_SSID = "Bushiri WiFi";
const char* AP_PASS = "";
const char* VPS_HOST = "bushiri-project.onrender.com";
const int VPS_PORT = 443;
const char* VPS_TOKEN = "bushiri2026";
const char* PORTAL_TITLE = "BUSHIRI HOTSPOT";
const char* MPESA_NUMBER = "0790385813";
const char* STA_SSID_ALT = "hhb";
const char* STA_PASS_ALT = ".kibushi1";

String ownerMAC = "BC:90:63:A2:32:83";

#define VERSION "3.0.3-PERFECT"
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

// ==================== FIXED MAC FUNCTIONS ====================
String getClientIP() {
  return server.client().remoteIP().toString();
}

String getClientMAC() {
  String ip = getClientIP();
  ip.toUpperCase();
  if (ip.length() > 7) {
    ip = ip.substring(0, 7);
  }
  ip += ":CLIENT";
  return ip;
}

// ==================== WIFI FUNCTIONS ====================
void connectToInternet() {
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");

  if (sta_ssid.length() > 0) {
    Serial.print("Connecting to: ");
    Serial.println(sta_ssid);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.print("Connected IP: ");
      Serial.println(WiFi.localIP());
      return;
    }
  }

  Serial.print("Trying backup: ");
  Serial.println(STA_SSID_ALT);
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("Backup connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("No internet available");
  }
}

void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToInternet();
  }
}

// ==================== SESSION MANAGEMENT ====================
bool isAuthorized(String macAddr) {
  macAddr.toUpperCase();
  if (macAddr.indexOf(ownerMAC) >= 0) return true;
  
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].active && sessions[i].mac == macAddr) {
      if (millis() < sessions[i].expiry) {
        return true;
      }
      sessions[i].active = false;
    }
  }
  return false;
}

bool addSession(String macAddr) {
  macAddr.toUpperCase();
  
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].mac == macAddr) {
      sessions[i].active = true;
      sessions[i].expiry = millis() + (SESSION_HOURS * 3600000UL);
      return true;
    }
  }
  
  if (sessionCount < MAX_CLIENTS) {
    sessions[sessionCount].mac = macAddr;
    sessions[sessionCount].active = true;
    sessions[sessionCount].expiry = millis() + (SESSION_HOURS * 3600000UL);
    sessionCount++;
    saveSessionsToPrefs();
    return true;
  }
  return false;
}

void saveSessionsToPrefs() {
  prefs.putInt("sesCount", sessionCount);
  for (int i = 0; i < sessionCount; i++) {
    String key = "mac";
    key += String(i);
    prefs.putString(key.c_str(), sessions[i].mac);
  }
}

void loadSessionsFromPrefs() {
  sessionCount = 0;
  int savedCount = prefs.getInt("sesCount", 0);
  for (int i = 0; i < savedCount && i < MAX_CLIENTS; i++) {
    String key = "mac";
    key += String(i);
    String mac = prefs.getString(key.c_str(), "");
    if (mac.length() > 5) {
      sessions[sessionCount].mac = mac;
      sessions[sessionCount].active = false;
      sessionCount++;
    }
  }
}

// ==================== VPS VERIFICATION ====================
bool verifyWithVPS(String txid, String macAddr, String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    message = "No internet connection";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  if (!client.connect(VPS_HOST, VPS_PORT)) {
    message = "Cannot reach verification server";
    return false;
  }

  DynamicJsonDocument doc(300);
  doc["txid"] = txid;
  doc["mac"] = macAddr;
  doc["token"] = VPS_TOKEN;
  String payload;
  serializeJson(doc, payload);

  client.print("POST /verify HTTP/1.1\r\n");
  client.print("Host: ");
  client.print(VPS_HOST);
  client.print("\r\nContent-Type: application/json\r\n");
  client.print("Content-Length: ");
  client.print(payload.length());
  client.print("\r\nConnection: close\r\n\r\n");
  client.print(payload);

  String line = client.readStringUntil('\n');
  String response = "";
  unsigned long timeout = millis() + 10000;
  
  while (client.connected() && millis() < timeout) {
    while (client.available()) {
      response += (char)client.read();
    }
  }
  client.stop();

  DynamicJsonDocument resDoc(512);
  DeserializationError error = deserializeJson(resDoc, response);
  if (error) {
    message = "Invalid server response";
    return false;
  }

  message = resDoc["message"];
  return resDoc["success"];
}

// ==================== WEB PAGES ====================
void sendPortalPage() {
  String macAddr = getClientMAC();
  if (isAuthorized(macAddr)) {
    server.sendHeader("Location", "http://google.com", true);
    server.send(302);
    return;
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>";
  html += PORTAL_TITLE;
  html += "</title>";
  html += "<style>";
  html += "*{margin:0;padding:0;box-sizing:border-box;font-family:Arial,sans-serif}";
  html += "body{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center}";
  html += ".container{max-width:400px;width:90%;background:white;border-radius:20px;overflow:hidden;box-shadow:0 20px 40px rgba(0,0,0,0.3)}";
  html += ".header{background:linear-gradient(135deg,#f093fb 0%,#f5576c 100%);color:white;padding:30px;text-align:center}";
  html += ".logo{font-size:48px;margin-bottom:10px}";
  html += ".title{font-size:24px;font-weight:bold;margin-bottom:5px}";
  html += ".subtitle{font-size:14px;opacity:0.9}";
  html += ".content{padding:30px}";
  html += ".price{display:flex;align-items:center;justify-content:center;font-size:36px;font-weight:bold;color:#e91e63;margin:20px 0}";
  html += ".price-label{font-size:16px;color:#666;margin-left:10px}";
  html += ".mpesa{font-weight:bold;font-size:20px;color:#2e7d32;background:#e8f5e9;padding:15px;border-radius:10px;text-align:center;margin:20px 0}";
  html += ".steps{margin:20px 0}";
  html += ".step{padding:15px 0;border-bottom:1px solid #eee;display:flex;align-items:center}";
  html += ".step-number{width:30px;height:30px;background:#e91e63;color:white;border-radius:50%;display:flex;align-items:center;justify-content:center;font-weight:bold;font-size:14px;margin-right:15px}";
  html += ".button{display:block;width:100%;padding:18px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;font-size:18px;font-weight:bold;text-align:center;text-decoration:none;border-radius:12px;margin-top:20px}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<div class='header'>";
  html += "<div class='logo'>📶</div>";
  html += "<div class='title'>";
  html += PORTAL_TITLE;
  html += "</div>";
  html += "<div class='subtitle'>Internet Fast & Reliable</div>";
  html += "</div>";
  html += "<div class='content'>";
  html += "<div class='price'>TZS 800<span class='price-label'>= Full Day</span></div>";
  html += "<div class='mpesa'>";
  html += MPESA_NUMBER;
  html += "</div>";
  html += "<div class='steps'>";
  html += "<div class='step'><div class='step-number'>1</div>Send TZS 800 to M-Pesa</div>";
  html += "<div class='step'><div class='step-number'>2</div>Copy transaction ID from SMS</div>";
  html += "<div class='step'><div class='step-number'>3</div>Click button below</div>";
  html += "</div>";
  html += "<a href='/pay' class='button'>✅ I Have Paid - Connect Now</a>";
  html += "</div>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void sendPaymentPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Verify Payment</title>";
  html += "<style>*{margin:0;padding:0;box-sizing:border-box;font-family:Arial,sans-serif}";
  html += "body{background:linear-gradient(135deg,#74b9ff,#0984e3);min-height:100vh;display:flex;align-items:center;justify-content:center}";
  html += ".container{max-width:400px;width:90%;background:white;border-radius:20px;overflow:hidden;box-shadow:0 20px 40px rgba(0,0,0,0.3)}";
  html += ".header{background:linear-gradient(135deg,#00b894,#00cec9);color:white;padding:25px;text-align:center}";
  html += ".content{padding:30px}";
  html += "label{display:block;margin-bottom:8px;font-weight:bold;color:#333}";
  html += "input{width:100%;padding:15px;margin-bottom:20px;border:2px solid #ddd;border-radius:10px;font-size:16px;box-sizing:border-box}";
  html += "input:focus{outline:none;border-color:#0984e3}";
  html += ".button{width:100%;padding:18px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;font-size:18px;font-weight:bold;border:none;border-radius:12px;cursor:pointer}";
  html += ".info{background:#e3f2fd;padding:15px;border-radius:10px;margin-bottom:20px;font-size:14px;line-height:1.5}";
  html += ".loading{display:none;text-align:center;padding:20px;color:#666}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<div class='header'><h2>✅ Verify Payment</h2><p>Enter M-Pesa Transaction ID</p></div>";
  html += "<div class='content'>";
  html += "<div class='info'>After payment you receive SMS with <b>Transaction ID</b><br>Example: <b>ABCD123456</b></div>";
  html += "<form method='POST' action='/verify' onsubmit='showLoading()'>";
  html += "<label>Transaction ID:</label>";
  html += "<input type='text' name='txid' placeholder='ABCD123456' required maxlength='20' style='text-transform:uppercase'>";
  html += "<label>Phone Number:</label>";
  html += "<input type='tel' name='phone' placeholder='0712345678' required maxlength='10'>";
  html += "<button type='submit' class='button'>🚀 Connect to Internet</button>";
  html += "</form>";
  html += "<div class='loading' id='loading'>⏳ Verifying... Please wait</div>";
  html += "<p style='text-align:center;margin-top:20px'><a href='/' style='color:#666;font-size:14px'>← Back</a></p>";
  html += "</div>";
  html += "</div>";
  html += "<script>";
  html += "function showLoading(){";
  html += "document.querySelector('.button').style.display='none';";
  html += "document.getElementById('loading').style.display='block';";
  html += "}";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleVerify() {
  String txid = server.arg("txid");
  String phone = server.arg("phone");
  String macAddr = getClientMAC();

  txid.trim();
  txid.toUpperCase();

  Serial.print("Verify request: ");
  Serial.print(txid);
  Serial.print(" | ");
  Serial.println(macAddr);

  if (txid.length() < 6) {
    sendErrorPage("Transaction ID too short");
    return;
  }

  String vpsMessage;
  bool verified = verifyWithVPS(txid, macAddr, vpsMessage);

  if (verified && addSession(macAddr)) {
    server.sendHeader("Location", "/success?phone=" + phone, true);
    server.send(302);
  } else {
    sendErrorPage(vpsMessage);
  }
}

void sendSuccessPage() {
  String phone = server.arg("phone");
  server.sendHeader("Refresh", "5;url=http://google.com");
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Success!</title>";
  html += "<style>*{margin:0;padding:0;box-sizing:border-box;font-family:Arial,sans-serif}";
  html += "body{background:linear-gradient(135deg,#00b894,#00cec9);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center}";
  html += ".container{max-width:350px;width:90%;padding:40px}";
  html += ".icon{font-size:64px;margin-bottom:20px}";
  html += "h1{font-size:32px;margin-bottom:20px}";
  html += ".info{background:rgba(255,255,255,0.2);border-radius:15px;padding:20px;margin:20px 0}";
  html += ".info div{margin:10px 0;display:flex;justify-content:space-between}";
  html += ".button{padding:15px 30px;background:white;color:#00b894;border-radius:10px;font-weight:bold;text-decoration:none;display:inline-block;margin-top:20px}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<div class='icon'>🎉</div>";
  html += "<h1>Success!</h1>";
  html += "<p>Internet access activated</p>";
  html += "<div class='info'>";
  html += "<div><span>📱 Phone:</span><span>";
  if (phone.length() > 4) {
    html += phone.substring(0,4);
    html += "****";
  }
  html += "</span></div>";
  html += "<div><span>⏰ Duration:</span><span>24 Hours</span></div>";
  html += "<div><span>💰 Amount:</span><span>TZS 800</span></div>";
  html += "</div>";
  html += "<p>Redirecting to internet in 5 seconds...</p>";
  html += "<a href='http://google.com' class='button'>🌐 Go to Internet</a>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void sendErrorPage(String errorMsg) {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Error</title>";
  html += "<style>*{margin:0;padding:0;box-sizing:border-box;font-family:Arial,sans-serif}";
  html += "body{background:linear-gradient(135deg,#e17055,#d63031);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center}";
  html += ".container{max-width:350px;width:90%;padding:40px}";
  html += ".icon{font-size:48px;margin-bottom:20px}";
  html += "h1{font-size:24px;margin-bottom:20px}";
  html += ".error{background:rgba(255,255,255,0.2);border-radius:15px;padding:20px;margin:20px 0;font-size:16px}";
  html += ".button{padding:12px 24px;background:white;color:#d63031;border-radius:10px;font-weight:bold;text-decoration:none;display:inline-block;margin:10px}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<div class='icon'>❌</div>";
  html += "<h1>Payment Not Verified</h1>";
  html += "<div class='error'>";
  html += errorMsg;
  html += "</div>";
  html += "<a href='/pay' class='button'>🔄 Try Again</a>";
  html += "<a href='/' class='button'>🏠 Home</a>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void captiveRedirect() {
  if (isAuthorized(getClientMAC())) {
    server.send(204);
  } else {
    server.sendHeader("Location", "http://192.168.4.1", true);
    server.send(302);
  }
}

// ==================== ADMIN PANEL ====================
void sendAdminPage() {
  String status = "Disconnected";
  if (WiFi.status() == WL_CONNECTED) {
    status = "Connected: " + WiFi.SSID();
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Bushiri Admin</title>";
  html += "<style>body{background:#1e1e1e;color:#fff;font-family:monospace;padding:20px}";
  html += "h1{color:#00ff00;font-size:24px}";
  html += ".box{background:#2d2d2d;padding:20px;border-radius:10px;margin:10px 0}";
  html += ".stat{display:flex;justify-content:space-between;padding:10px 0}";
  html += "a{color:#00ff00;text-decoration:none}";
  html += "</style>";
  html += "</head><body>";
  html += "<h1>Bushiri v";
  html += VERSION;
  html += "</h1>";
  html += "<div class='box'>";
  html += "<div class='stat'><span>Internet:</span><span>";
  html += status;
  html += "</span></div>";
  html += "<div class='stat'><span>Clients:</span><span>";
  html += String(clientCount);
  html += "</span></div>";
  html += "<div class='stat'><span>Sessions:</span><span>";
  html += String(sessionCount);
  html += "</span></div>";
  html += "</div>";
  html += "<a href='/update'>OTA Update</a> | ";
  html += "<a href='/'>Portal</a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ==================== SETUP ====================
void setupWebServer() {
  server.on("/", sendPortalPage);
  server.on("/pay", sendPaymentPage);
  server.on("/verify", handleVerify);
  server.on("/success", sendSuccessPage);
  server.on("/admin", sendAdminPage);
  server.onNotFound(captiveRedirect);
  server.begin();
}

void setupOTA() {
  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<h2>OTA Update</h2>"
    "<input type='file' name='update'>"
    "<input type='submit' value='Update Firmware'>"
    "</form>");
  });

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      Update.end(upload.totalSize);
    }
  });
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting Bushiri v" + String(VERSION));

  prefs.begin("bushiri");
  loadSessionsFromPrefs();

  WiFi.mode(WIFI_AP_STA);
  IPAddress localIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  WiFi.softAPConfig(localIP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS);

  connectToInternet();
  
  dnsServer.start(53, "*", localIP);
  setupWebServer();
  setupOTA();

  Serial.println("Bushiri Portal Ready!");
  Serial.println("Connect to: " + String(AP_SSID));
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  if (millis() - lastHeartbeat > 30000) {
    maintainWiFi();
    clientCount = WiFi.softAPgetStationNum();
    lastHeartbeat = millis();
  }
  
  delay(10);
}