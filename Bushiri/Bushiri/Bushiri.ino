/**
PROJECT BUSHIRI v3.1
MPESA/MIXX Captive Portal + NAT Router + VPS Verify + WiFi Repeater

Bei: TZS 800 = Masaa 15
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "lwip/lwip_napt.h"

// ==================== EDIT HAPA TU ====================
const char* AP_SSID      = "Bushiri WiFi";
const char* AP_PASS      = "";
const char* VPS_HOST     = "bushiri-project.onrender.com";
const int   VPS_PORT     = 443;
const char* VPS_TOKEN    = "bushiri2026";
const char* PORTAL_TITLE = "BUSHIRI HOTSPOT";
const char* MIXX_NUMBER  = "0717633805";
const char* STA_SSID_ALT = "infinitynetwork";
const char* STA_PASS_ALT = ".kibushi1";
String ownerIP = "192.168.4.2";
// ======================================================

#define PROJECT_NAME "BUSHIRI"
#define VERSION "3.2.0"  // Updated version
#define MAX_CLIENTS 20

struct ClientSession {
  String ip;
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
bool natEnabled = false;

// ==================== FORWARD DECLARATIONS ====================
void portalPage();
void paymentPage();
void handleVerify();
void successPage();
void adminPanel();
void wifiConfigPage();
void saveWifiConfig();
void sendErrorPage(String message);
void captiveRedirect();
void setupOTA();
bool verifyWithVPS(String txid, String mac, String &message);
void enableNAT();
void maintainWiFiRepeater();

// ==================== SESSION ====================
bool isAuthorized(String ip) {
  if (ip == ownerIP) return true;
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].active && sessions[i].ip == ip) {
      if (millis() < sessions[i].expiry) return true;
      else sessions[i].active = false;
    }
  }
  return false;
}

bool addSession(String ip, unsigned long durationMs) {
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].ip == ip) {
      sessions[i].active = true;
      sessions[i].expiry = millis() + durationMs;
      return true;
    }
  }
  if (sessionCount < MAX_CLIENTS) {
    sessions[sessionCount].ip = ip;
    sessions[sessionCount].active = true;
    sessions[sessionCount].expiry = millis() + durationMs;
    sessionCount++;
    return true;
  }
  return false;
}

String getClientIP() {
  return server.client().remoteIP().toString();
}

// ==================== WIFI REPEATER + NAT ====================
void enableNAT() {
  if (!natEnabled && WiFi.status() == WL_CONNECTED) {
    delay(2000);  // Wait for stable connection
    ip_napt_enable(htonl(0xC0A80401), 1);  // 192.168.4.1
    natEnabled = true;
    Serial.println("✅ NAT Enabled - WiFi Repeater ACTIVE!");
  }
}

void connectToInternet() {
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");

  if (sta_ssid.length() > 0) {
    Serial.print("🔄 Connecting to Main: " + sta_ssid);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    int t = 0;
    while (WiFi.status() != WL_CONNECTED && t < 30) {
      delay(500); 
      Serial.print("."); 
      t++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ Main Connected: " + WiFi.localIP().toString());
      enableNAT();
      return;
    }
    Serial.println("\n❌ Main failed - trying alt...");
  }

  // Alt connection
  Serial.print("🔄 Alt: " + String(STA_SSID_ALT));
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 30) {
    delay(500); 
    Serial.print("."); 
    t++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Alt Connected: " + WiFi.localIP().toString());
    enableNAT();
  } else {
    Serial.println("\n❌ No internet connection");
    natEnabled = false;
  }
}

void maintainWiFiRepeater() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000) {  // Check every 30s
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("🔄 Reconnecting WiFi...");
      connectToInternet();
    } else {
      enableNAT();  // Ensure NAT stays active
    }
    lastCheck = millis();
  }
}

// ==================== VPS VERIFY ====================
bool verifyWithVPS(String txid, String ip, String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    message = "Hakuna internet - jaribu tena";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);

  if (!client.connect(VPS_HOST, VPS_PORT)) {
    message = "VPS haipatikani - jaribu tena";
    return false;
  }

  DynamicJsonDocument doc(256);
  doc["txid"] = txid;
  doc["mac"] = ip;
  doc["token"] = VPS_TOKEN;
  String payload;
  serializeJson(doc, payload);

  client.println("POST /verify HTTP/1.1");
  client.println("Host: " + String(VPS_HOST));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(payload.length()));
  client.println("Connection: close");
  client.println();
  client.print(payload);

  String response = "";
  unsigned long timeout = millis() + 15000;
  bool headersEnded = false;
  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") headersEnded = true;
      if (headersEnded) response += line;
    }
  }
  client.stop();
  response.trim();

  DynamicJsonDocument res(512);
  DeserializationError err = deserializeJson(res, response);
  if (err) {
    message = "VPS ilijibu vibaya";
    return false;
  }

  bool success = res["success"] | false;
  message = res["message"] | "Hitilafu";

  // Pata muda kutoka VPS
  if (success) {
    unsigned long durationMs = 15UL * 3600000UL;  // Default masaa 15
    addSession(ip, durationMs);
  }

  return success;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("🚀 BUSHIRI v" VERSION " - MIXX + WiFi Repeater Edition");

  prefs.begin("bushiri");

  // WiFi AP+STA Mode (REPEATER MODE)
  WiFi.mode(WIFI_AP_STA);
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 8);  // Channel 6, max 8 clients
  
  Serial.println("📶 AP Started: " + String(AP_SSID) + " @ 192.168.4.1");

  // Connect to internet (STA)
  connectToInternet();

  // DNS Captive Portal
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", local_IP);

  setupWebServer();
  setupOTA();

  Serial.println("🌐 Portal LIVE + WiFi Repeater ACTIVE!");
  Serial.println("📡 AP: " + String(AP_SSID));
  Serial.println("📶 STA: " + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Disconnected"));
}

// ==================== WEB SERVER ====================
void setupWebServer() {
  server.on("/", HTTP_GET, portalPage);
  server.on("/pay", HTTP_GET, paymentPage);
  server.on("/verify", HTTP_POST, handleVerify);
  server.on("/success", HTTP_GET, successPage);
  server.on("/admin", HTTP_GET, adminPanel);
  server.on("/wifi-config", HTTP_GET, wifiConfigPage);
  server.on("/wifi-save", HTTP_POST, saveWifiConfig);

  // Captive portal detection pages
  server.on("/generate_204", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(204, "text/plain", "");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    }
  });

  server.on("/hotspot-detect.html", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    }
  });

  server.on("/connecttest.txt", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(200, "text/plain", "Microsoft Connect Test");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    }
  });

  server.on("/success.txt", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(200, "text/plain", "success");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    }
  });

  server.onNotFound(captiveRedirect);
  server.begin();
}

// ==================== PORTAL PAGE (HAKUJABIWA) ====================
void portalPage() {
  String ip = getClientIP();
  if (isAuthorized(ip)) {
    server.sendHeader("Location", "http://google.com");
    server.send(302);
    return;
  }

  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>

<title>BUSHIRI HOTSPOT</title>  
<style>  
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}  
body{background:linear-gradient(135deg,#1a1a2e,#16213e,#0f3460);min-height:100vh;display:flex;align-items:center;justify-content:center}  
.card{max-width:380px;width:93%;background:white;border-radius:20px;overflow:hidden;box-shadow:0 25px 50px rgba(0,0,0,0.4)}  
.header{background:linear-gradient(135deg,#e91e63,#c2185b);color:white;padding:30px;text-align:center}  
.wifi-icon{font-size:3em;margin-bottom:8px}  
.brand{font-size:1.5em;font-weight:700;letter-spacing:2px}  
.tagline{font-size:0.85em;opacity:0.9;margin-top:4px}  
.body{padding:25px}  
.price-box{background:#f8f9fa;border-radius:12px;padding:18px;text-align:center;margin-bottom:20px;border:2px solid #e91e63}  
.price{font-size:2.2em;font-weight:800;color:#e91e63}  
.price-label{color:#666;font-size:0.9em;margin-top:3px}  
.steps{margin-bottom:20px}  
.step{display:flex;align-items:center;padding:8px 0;border-bottom:1px solid #f0f0f0;font-size:0.9em;color:#444}  
.step-num{background:#e91e63;color:white;border-radius:50%;width:24px;height:24px;display:flex;align-items:center;justify-content:center;font-weight:bold;font-size:0.8em;margin-right:10px;flex-shrink:0}  
.btn{display:block;width:100%;padding:15px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;border:none;border-radius:12px;font-size:1.1em;font-weight:700;text-align:center;text-decoration:none;cursor:pointer;margin-top:15px}  
.mixx-num{background:#e8f5e9;border-radius:8px;padding:10px;text-align:center;font-weight:700;font-size:1.1em;color:#2e7d32;margin:10px 0}  
</style></head><body>  
<div class='card'>  
  <div class='header'>  
    <div class='wifi-icon'>📶</div>  
    <div class='brand'>)" + String(PORTAL_TITLE) + R"(</div>  
    <div class='tagline'>Internet ya haraka na ya uhakika</div>  
  </div>  
  <div class='body'>  
    <div class='price-box'>  
      <div class='price'>TZS 800</div>  
      <div class='price-label'>= Masaa 15 ya internet</div>  
    </div>  
    <div class='steps'>  
      <div class='step'><div class='step-num'>1</div>Tuma TZS 800 kwa MIXX BY YAS</div>  
      <div class='step'><div class='step-num'>2</div>Nambari ya kulipa:</div>  
    </div>  
    <div class='mixx-num'>📱 )" + String(MIXX_NUMBER) + R"(</div>  
    <div class='step' style='padding:8px 0;font-size:0.9em;color:#444'>  
      <div class='step-num'>3</div>Bonyeza hapa chini - weka namba ya kumbukumbu  
    </div>  
    <a href='/pay' class='btn'>✅ Nimelipa - Ingia Sasa</a>  
  </div>  
</div></body></html>)";
  server.send(200, "text/html", html);
}

// ==================== BAKI YA FUNCTIONS (HAKUJABIWA) ====================
void paymentPage() {
  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>

<title>Thibitisha Malipo</title>  
<style>  
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}  
body{background:linear-gradient(135deg,#1a1a2e,#16213e,#0f3460);min-height:100vh;display:flex;align-items:center;justify-content:center}  
.card{max-width:380px;width:93%;background:white;border-radius:20px;overflow:hidden;box-shadow:0 25px 50px rgba(0,0,0,0.4)}  
.header{background:linear-gradient(135deg,#2196F3,#1565C0);color:white;padding:25px;text-align:center}  
.header h2{font-size:1.3em;margin-bottom:5px}  
.header p{font-size:0.85em;opacity:0.9}  
.body{padding:25px}  
.info-box{background:#e3f2fd;border-radius:10px;padding:15px;margin-bottom:20px;font-size:0.85em;color:#1565C0;line-height:1.6}  
label{display:block;font-weight:600;color:#333;margin-bottom:6px;font-size:0.9em}  
input{width:100%;padding:14px;border:2px solid #ddd;border-radius:10px;font-size:1em;margin-bottom:15px;box-sizing:border-box}  
input:focus{outline:none;border-color:#2196F3}  
.btn{width:100%;padding:15px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;border:none;border-radius:12px;font-size:1.1em;font-weight:700;cursor:pointer}  
.back{display:block;text-align:center;margin-top:12px;color:#666;font-size:0.85em;text-decoration:none}  
.loading{display:none;text-align:center;padding:10px;color:#666}  
</style></head><body>  
<div class='card'>  
  <div class='header'>  
    <h2>✅ Thibitisha Malipo</h2>  
    <p>Weka namba ya kumbukumbu ya MIXX BY YAS</p>  
  </div>  
  <div class='body'>  
    <div class='info-box'>  
      📩 Baada ya kutuma pesa utapata SMS kutoka MIXX BY YAS.<br>  
      SMS hiyo ina <b>Kumbukumbu no.</b><br>  
      Mfano: <b>26205921931320</b>  
    </div>  
    <form method='