/**
 * PROJECT BUSHIRI v3.1
 * MPESA/MIXX Captive Portal + NAT Router + VPS Verify
 * Bei: TZS 800 = Masaa 15
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
#define VERSION "3.1.0"
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

// ==================== WIFI ====================
void connectToInternet() {
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");

  if (sta_ssid.length() > 0) {
    Serial.print("Connecting to: " + sta_ssid);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    int t = 0;
    while (WiFi.status() != WL_CONNECTED && t < 20) {
      delay(500); Serial.print("."); t++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected: " + WiFi.localIP().toString());
      return;
    }
    Serial.println("\nMain failed - trying alt...");
  }

  Serial.print("Alt: " + String(STA_SSID_ALT));
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 20) {
    delay(500); Serial.print("."); t++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nAlt connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nNo internet");
  }
}

void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToInternet();
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
    // Default masaa 15 kama VPS haitumi duration
    unsigned long durationMs = 15UL * 3600000UL;
    addSession(ip, durationMs);
  }

  return success;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("BUSHIRI v3.1 - MIXX Edition");

  prefs.begin("bushiri");

  WiFi.mode(WIFI_AP_STA);
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 8);

  connectToInternet();

  // Washa NAT baada ya WiFi kuunganika
  if (WiFi.status() == WL_CONNECTED) {
    delay(1000);
    ip_napt_enable(htonl(0xC0A80401), 1);
    Serial.println("NAT Enabled!");
  }

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", local_IP);

  setupWebServer();
  setupOTA();

  Serial.println("Portal LIVE: " + String(AP_SSID));
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

  // Captive portal detection - Android
  server.on("/generate_204", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(204, "text/plain", "");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    }
  });

  // Captive portal detection - iPhone
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    }
  });

  // Captive portal detection - Windows
  server.on("/connecttest.txt", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(200, "text/plain", "Microsoft Connect Test");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    }
  });

  // Captive portal detection - Samsung/Android
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

// ==================== PORTAL PAGE ====================
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

// ==================== PAYMENT PAGE ====================
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
    <form method='POST' action='/verify' onsubmit='showLoading()'>
      <label>Namba ya Kumbukumbu (TXID):</label>
      <input type='text' name='txid' placeholder='Mfano: 26205921931320' required maxlength='20'>
      <label>Namba yako ya Simu:</label>
      <input type='tel' name='phone' placeholder='0717633805' required maxlength='13'>
      <button type='submit' class='btn'>🚀 Ingia Sasa</button>
    </form>
    <div class='loading' id='loading'>⏳ Inathibitisha... Subiri...</div>
    <a href='/' class='back'>← Rudi Nyuma</a>
  </div>
</div>
<script>
function showLoading(){
  document.querySelector('.btn').style.display='none';
  document.getElementById('loading').style.display='block';
}
</script>
</body></html>)";

  server.send(200, "text/html", html);
}

// ==================== HANDLE VERIFY ====================
void handleVerify() {
  String txid = server.arg("txid");
  String phone = server.arg("phone");
  String ip = getClientIP();

  txid.trim();
  Serial.println("Verify: TXID=" + txid + " Phone=" + phone + " IP=" + ip);

  // Trial ya bure
  if (txid == "BUSHIRIPROJECT") {
    addSession(ip, 30UL * 60000UL); // Dakika 30
    server.sendHeader("Location", "/success?phone=" + phone + "&trial=1");
    server.send(302);
    return;
  }

  if (txid.length() < 6) {
    sendErrorPage("TXID ni fupi mno. Angalia SMS yako tena.");
    return;
  }

  String message = "";
  bool success = verifyWithVPS(txid, ip, message);

  if (success) {
    server.sendHeader("Location", "/success?phone=" + phone);
    server.send(302);
  } else {
    sendErrorPage(message);
  }
}

// ==================== SUCCESS PAGE ====================
void successPage() {
  String phone = server.arg("phone");
  bool isTrial = server.arg("trial") == "1";

  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta http-equiv='refresh' content='5;url=http://google.com'>
<title>Umefanikiwa!</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:linear-gradient(135deg,#00c853,#1b5e20);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center;padding:20px}
.card{max-width:350px;width:100%}
.icon{font-size:5em;margin-bottom:15px}
h1{font-size:1.8em;margin-bottom:10px}
p{opacity:0.9;margin:8px 0;font-size:0.95em}
.info{background:rgba(255,255,255,0.2);border-radius:12px;padding:15px;margin:20px 0}
.info-item{display:flex;justify-content:space-between;padding:5px 0;font-size:0.9em}
.btn{display:inline-block;margin-top:15px;padding:12px 25px;background:white;color:#00c853;border-radius:10px;font-weight:700;text-decoration:none}
</style></head><body>
<div class='card'>
  <div class='icon'>🎉</div>
  <h1>Hongera!</h1>
  <p>Internet imewashwa!</p>
  <div class='info'>
    <div class='info-item'><span>📱 Simu</span><span>)" + phone.substring(0,4) + R"(******</span></div>
    <div class='info-item'><span>⏰ Muda</span><span>)" + String(isTrial ? "Dakika 30 (Trial)" : "Masaa 15") + R"(</span></div>
  </div>
  <p>Inakupeleka Google baada ya sekunde 5...</p>
  <a href='http://google.com' class='btn'>🌐 Nenda Internet</a>
</div>
</body></html>)";

  server.send(200, "text/html", html);
}

// ==================== ERROR PAGE ====================
void sendErrorPage(String message) {
  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>Hitilafu</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:linear-gradient(135deg,#b71c1c,#c62828);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center;padding:20px}
.card{max-width:350px;width:100%}
.icon{font-size:4em;margin-bottom:15px}
h2{font-size:1.5em;margin-bottom:15px}
.msg{background:rgba(255,255,255,0.2);border-radius:12px;padding:15px;margin:15px 0;font-size:0.95em;line-height:1.5}
.btn{display:inline-block;margin-top:15px;padding:12px 25px;background:white;color:#b71c1c;border-radius:10px;font-weight:700;text-decoration:none;margin:8px}
</style></head><body>
<div class='card'>
  <div class='icon'>❌</div>
  <h2>Malipo Hayakuthibitishwa</h2>
  <div class='msg'>)" + message + R"(</div>
  <a href='/pay' class='btn'>🔄 Jaribu Tena</a>
  <a href='/' class='btn'>🏠 Nyumbani</a>
</div>
</body></html>)";

  server.send(200, "text/html", html);
}

// ==================== CAPTIVE REDIRECT ====================
void captiveRedirect() {
  String ip = getClientIP();
  String host = server.hostHeader();

  if (isAuthorized(ip)) {
    server.send(204, "text/plain", "");
    return;
  }

  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/plain", "");
}

// ==================== ADMIN PANEL ====================
void adminPanel() {
  String internetStatus;
  if (WiFi.status() == WL_CONNECTED) {
    internetStatus = "🟢 " + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")";
  } else {
    internetStatus = "🔴 Haijaunganika";
  }

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Admin - " + String(PORTAL_TITLE) + "</title>";
  html += "<style>body{background:#0d1117;color:#e6edf3;font-family:monospace;padding:20px;font-size:14px}";
  html += "h1{color:#58a6ff;margin-bottom:10px}h3{color:#3fb950;margin:15px 0 8px}";
  html += ".box{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px;margin:10px 0}";
  html += ".stat{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #21262d}";
  html += "a{color:#58a6ff;text-decoration:none;margin-right:15px}";
  html += ".ip{font-size:11px;color:#8b949e;padding:3px 0}</style></head><body>";
  html += "<h1>🛜 BUSHIRI v" VERSION " Admin</h1>";
  html += "<div class='box'>";
  html += "<div class='stat'><span>Internet</span><span>" + internetStatus + "</span></div>";
  html += "<div class='stat'><span>Wateja Sasa</span><span>" + String(clientCount) + "</span></div>";
  html += "<div class='stat'><span>Sessions Aktif</span><span>" + String(sessionCount) + "</span></div>";
  html += "<div class='stat'><span>Bei</span><span>TZS 800/Masaa 15</span></div>";
  html += "</div>";
  html += "<h3>Sessions:</h3><div class='box'>";
  int active = 0;
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].active && millis() < sessions[i].expiry) {
      unsigned long remaining = (sessions[i].expiry - millis()) / 60000;
      html += "<div class='ip'>" + sessions[i].ip;
      html += " | Dakika " + String(remaining) + " zimebaki</div>";
      active++;
    }
  }
  if (active == 0) html += "<div class='ip'>Hakuna sessions za sasa</div>";
  html += "</div>";
  html += "<h3>Mipangilio:</h3>";
  html += "<a href='/wifi-config'>⚙️ WiFi Config</a>";
  html += "<a href='/update'>🔄 OTA Update</a><br><br>";
  html += "<a href='/'>🏠 Portal</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ==================== WIFI CONFIG ====================
void wifiConfigPage() {
  String status;
  if (WiFi.status() == WL_CONNECTED) {
    status = "🟢 " + WiFi.SSID() + " | " + WiFi.localIP().toString();
  } else {
    status = "🔴 Haijaunganika";
  }

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>WiFi Config</title>";
  html += "<style>body{background:#111;color:#fff;font-family:monospace;padding:20px}";
  html += "h2{color:#e91e63}";
  html += "input{width:100%;padding:12px;margin:6px 0 14px;background:#222;color:#fff;border:1px solid #444;border-radius:8px;font-size:15px;box-sizing:border-box}";
  html += ".btn{width:100%;padding:14px;background:#e91e63;color:white;border:none;border-radius:8px;font-size:16px;cursor:pointer;font-weight:bold}";
  html += ".status{padding:12px;background:#1a1a1a;border-left:4px solid #e91e63;margin:12px 0;border-radius:4px}";
  html += "a{color:#e91e63}</style></head><body>";
  html += "<h2>⚙️ WiFi Config</h2>";
  html += "<div class='status'>" + status + "</div>";
  html += "<form method='POST' action='/wifi-save'>";
  html += "<label>SSID:</label>";
  html += "<input type='text' name='ssid' value='" + sta_ssid + "' placeholder='Jina la WiFi'>";
  html += "<label>Password:</label>";
  html += "<input type='password' name='pass' value='" + sta_pass + "' placeholder='Password'>";
  html += "<button type='submit' class='btn'>💾 Hifadhi na Restart</button>";
  html += "</form><br><a href='/admin'>← Admin</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void saveWifiConfig() {
  String new_ssid = server.arg("ssid");
  String new_pass = server.arg("pass");
  if (new_ssid.length() > 0) {
    prefs.putString("sta_ssid", new_ssid);
    prefs.putString("sta_pass", new_pass);
    server.send(200, "text/html",
      "<html><body style='background:#111;color:lime;font-family:monospace;text-align:center;padding:50px'>"
      "<h1>✅ Imehifadhiwa!</h1><p>ESP32 inarestart...</p>"
      "<p>Unganika: <b>" + String(AP_SSID) + "</b></p>"
      "<p>Nenda: 192.168.4.1/admin</p></body></html>");
    delay(2000);
    ESP.restart();
  } else {
    server.sendHeader("Location", "/wifi-config");
    server.send(302);
  }
}

// ==================== OTA ====================
void setupOTA() {
  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='utf-8'><title>OTA</title>"
      "<style>body{background:#111;color:#fff;font-family:monospace;padding:30px;text-align:center}"
      "input,button{padding:12px;margin:10px;border-radius:8px;font-size:15px}"
      "button{background:#e91e63;color:white;border:none;cursor:pointer;width:200px}"
      "a{color:#e91e63}</style></head><body>"
      "<h2>🔄 OTA Firmware Update</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='update' accept='.bin'><br>"
      "<button type='submit'>Upload</button></form>"
      "<br><a href='/admin'>← Admin</a></body></html>");
  });

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      Update.write(upload.buf, upload.currentSize);
    }
  });
}

// ==================== LOOP ====================
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (millis() - lastHeartbeat > 30000) {
    maintainWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      ip_napt_enable(htonl(0xC0A80401), 1);
    }
    lastHeartbeat = millis();
    clientCount = WiFi.softAPgetStationNum();
    Serial.println("Clients: " + String(clientCount) + " Sessions: " + String(sessionCount));
  }

  delay(10);
}
