/**
 * PROJECT BUSHIRI v3.0
 * MPESA Captive Portal + MAC Whitelist + VPS Verify + NAT Internet
 * Bei: TZS 800 = Siku nzima
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>

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

// MAC/IP yako - unapata internet bure (weka IP yako: 192.168.4.x)
String ownerMAC = "192.168.4.2";

// ======================================================

#define PROJECT_NAME "BUSHIRI"
#define VERSION "3.0.0"
#define MAX_CLIENTS 20
#define SESSION_HOURS 15

struct ClientSession {
  String mac;
  unsigned long expiry; // millis
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
unsigned long lastSessionCheck = 0;

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

  Serial.print("Connecting to alt: " + String(STA_SSID_ALT));
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 20) {
    delay(500); Serial.print("."); t++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nAlt connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nNo internet - offline mode");
  }
}

void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost - reconnecting...");
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
      else sessions[i].active = false; // Muda umeisha
    }
  }
  return false;
}

bool addSession(String mac) {
  mac.toUpperCase();
  // Angalia kama tayari ipo
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].mac == mac) {
      sessions[i].active = true;
      sessions[i].expiry = millis() + ((unsigned long)SESSION_HOURS * 3600000);
      return true;
    }
  }
  // Ongeza mpya
  if (sessionCount < MAX_CLIENTS) {
    sessions[sessionCount].mac = mac;
    sessions[sessionCount].active = true;
    sessions[sessionCount].expiry = millis() + ((unsigned long)SESSION_HOURS * 3600000);
    sessionCount++;
    // Hifadhi kwenye Preferences
    saveSessionsToPrefs();
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
  int saved = prefs.getInt("sesCount", 0);
  for (int i = 0; i < saved && i < MAX_CLIENTS; i++) {
    String key = "mac" + String(i);
    String mac = prefs.getString(key.c_str(), "");
    if (mac.length() > 0) {
      sessions[i].mac = mac;
      sessions[i].active = false; // Baada ya restart - waache waverify tena
      sessionCount++;
    }
  }
}

// ==================== GET CLIENT MAC ====================
// Pata identifier ya client - tumia IP kama session key
// ESP32 core v3.x hauna tcpip_adapter - IP inatosha kwa sessions
String getClientMAC() {
  return server.client().remoteIP().toString();
}

String getMACFromIP(String ip) {
  return ip; // IP ndiyo identifier yetu
}

// ==================== VPS VERIFY ====================
bool verifyWithVPS(String txid, String mac, String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    message = "Hakuna internet - jaribu tena";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10);

  if (!client.connect(VPS_HOST, VPS_PORT)) {
    message = "VPS haipatikani - jaribu tena";
    return false;
  }

  // Tengeneza JSON payload
  DynamicJsonDocument doc(256);
  doc["txid"] = txid;
  doc["mac"] = mac;
  doc["token"] = VPS_TOKEN;
  String payload;
  serializeJson(doc, payload);

  // Tuma HTTP POST
  client.println("POST /verify HTTP/1.1");
  client.println("Host: " + String(VPS_HOST));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(payload.length()));
  client.println("Connection: close");
  client.println();
  client.print(payload);

  // Soma response
  String response = "";
  unsigned long timeout = millis() + 10000;
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
  Serial.println("VPS Response: " + response);

  // Parse JSON response
  DynamicJsonDocument res(512);
  DeserializationError err = deserializeJson(res, response);
  if (err) {
    message = "VPS ilijibu vibaya";
    return false;
  }

  bool success = res["success"] | false;
  message = res["message"] | "Hitilafu isiyojulikana";
  return success;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("BUSHIRI v3.0 - MPESA Edition");

  prefs.begin("bushiri");
  loadSessionsFromPrefs();

  WiFi.mode(WIFI_AP_STA);
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 8);

  connectToInternet();

  dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
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
  server.onNotFound(captiveRedirect);
  server.begin();
}

// ==================== PORTAL PAGE (Ukurasa wa Kwanza) ====================
void portalPage() {
  String mac = getMACFromIP(server.client().remoteIP().toString());

  // Kama tayari ameidhinishwa - mpeleka internet
  if (isAuthorized(mac)) {
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
.mpesa-num{background:#e8f5e9;border-radius:8px;padding:10px;text-align:center;font-weight:700;font-size:1.1em;color:#2e7d32;margin:10px 0}
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
      <div class='price-label'>= Siku nzima ya internet</div>
    </div>
    <div class='steps'>
      <div class='step'><div class='step-num'>1</div>Tuma TZS 800 kwa M-Pesa</div>
      <div class='step'><div class='step-num'>2</div>Nambari ya kulipa:</div>
    </div>
    <div class='mpesa-num'>📱 )" + String(MPESA_NUMBER) + R"(</div>
    <div class='step' style='padding:8px 0;font-size:0.9em;color:#444'>
      <div class='step-num'>3</div>Bonyeza kitufe hapa chini na weka nambari ya muamala
    </div>
    <a href='/pay' class='btn'>✅ Nimelipa - Ingia Sasa</a>
  </div>
</div></body></html>)";

  server.send(200, "text/html", html);
}

// ==================== PAYMENT PAGE (Weka TXID) ====================
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
input{width:100%;padding:14px;border:2px solid #ddd;border-radius:10px;font-size:1em;margin-bottom:15px;box-sizing:border-box;letter-spacing:1px}
input:focus{outline:none;border-color:#2196F3}
.btn{width:100%;padding:15px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;border:none;border-radius:12px;font-size:1.1em;font-weight:700;cursor:pointer}
.back{display:block;text-align:center;margin-top:12px;color:#666;font-size:0.85em;text-decoration:none}
.loading{display:none;text-align:center;padding:10px;color:#666}
</style></head><body>
<div class='card'>
  <div class='header'>
    <h2>✅ Thibitisha Malipo</h2>
    <p>Weka nambari ya muamala wa M-Pesa</p>
  </div>
  <div class='body'>
    <div class='info-box'>
      📩 Baada ya kutuma TZS 800, utapata SMS kutoka M-Pesa.<br>
      SMS hiyo ina <b>nambari ya muamala</b> (Transaction ID).<br>
      Mfano: <b>ABCD123456</b>
    </div>
    <form method='POST' action='/verify' onsubmit='showLoading()'>
      <label>Nambari ya Muamala (TXID):</label>
      <input type='text' name='txid' placeholder='Mfano: ABCD123456' 
             required maxlength='20' style='text-transform:uppercase'
             oninput='this.value=this.value.toUpperCase()'>
      <label>Nambari yako ya Simu:</label>
      <input type='tel' name='phone' placeholder='0712345678' 
             required maxlength='10'>
      <button type='submit' class='btn'>🚀 Ingia Sasa</button>
    </form>
    <div class='loading' id='loading'>⏳ Inathibitisha... Subiri sekunde chache...</div>
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
  String mac = getMACFromIP(server.client().remoteIP().toString());

  txid.trim();
  txid.toUpperCase();

  Serial.println("Verify: TXID=" + txid + " Phone=" + phone + " MAC=" + mac);

  if (txid.length() < 6) {
    sendErrorPage("TXID ni fupi mno. Angalia SMS yako tena.");
    return;
  }

  // Verify na VPS
  String message = "";
  bool success = verifyWithVPS(txid, mac, message);

  if (success) {
    // Ongeza session
    addSession(mac);
    Serial.println("Session added for: " + mac);

    // Peleka success page
    server.sendHeader("Location", "/success?phone=" + phone);
    server.send(302);
  } else {
    sendErrorPage(message);
  }
}

// ==================== SUCCESS PAGE ====================
void successPage() {
  String phone = server.arg("phone");
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
  <p>Internet imewashwa kwa mafanikio!</p>
  <div class='info'>
    <div class='info-item'><span>📱 Simu</span><span>)" + phone.substring(0,4) + R"(******</span></div>
    <div class='info-item'><span>⏰ Muda</span><span>Siku nzima</span></div>
    <div class='info-item'><span>💰 Kiasi</span><span>TZS 800</span></div>
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
  String mac = getMACFromIP(server.client().remoteIP().toString());

  if (isAuthorized(mac)) {
    server.send(200, "text/plain", "OK - Internet Imeunganika");
  } else {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302);
  }
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
  html += ".mac{font-size:11px;color:#8b949e;padding:3px 0}</style></head><body>";
  html += "<h1>🛜 BUSHIRI v" VERSION " Admin</h1>";
  html += "<div class='box'>";
  html += "<div class='stat'><span>Internet</span><span>" + internetStatus + "</span></div>";
  html += "<div class='stat'><span>Wateja Sasa</span><span>" + String(clientCount) + "</span></div>";
  html += "<div class='stat'><span>Sessions Aktif</span><span>" + String(sessionCount) + "</span></div>";
  html += "<div class='stat'><span>Bei</span><span>TZS 800/Siku</span></div>";
  html += "</div>";
  html += "<h3>Sessions Zilizo Aktif:</h3><div class='box'>";
  int active = 0;
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].active) {
      unsigned long remaining = 0;
      if (sessions[i].expiry > millis()) {
        remaining = (sessions[i].expiry - millis()) / 3600000;
      }
      html += "<div class='mac'>" + sessions[i].mac;
      html += " | Masaa " + String(remaining) + " zimebaki</div>";
      active++;
    }
  }
  if (active == 0) html += "<div class='mac'>Hakuna sessions za sasa</div>";
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
  html += "h2{color:#e91e63}input{width:100%;padding:12px;margin:6px 0 14px;background:#222;color:#fff;border:1px solid #444;border-radius:8px;font-size:15px;box-sizing:border-box}";
  html += ".btn{width:100%;padding:14px;background:#e91e63;color:white;border:none;border-radius:8px;font-size:16px;cursor:pointer;font-weight:bold}";
  html += ".status{padding:12px;background:#1a1a1a;border-left:4px solid #e91e63;margin:12px 0;border-radius:4px}";
  html += "a{color:#e91e63}</style></head><body>";
  html += "<h2>⚙️ WiFi Internet Config</h2>";
  html += "<div class='status'>" + status + "</div>";
  html += "<form method='POST' action='/wifi-save'>";
  html += "<label>SSID (Jina la WiFi/Hotspot):</label>";
  html += "<input type='text' name='ssid' value='" + sta_ssid + "' placeholder='Jina la hotspot yako'>";
  html += "<label>Password:</label>";
  html += "<input type='password' name='pass' value='" + sta_pass + "' placeholder='Password (acha wazi kama open)'>";
  html += "<button type='submit' class='btn'>💾 Hifadhi na Restart</button>";
  html += "</form><br><a href='/admin'>← Admin Panel</a>";
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
      "<p>Unganika tena kwa: <b>" + String(AP_SSID) + "</b></p>"
      "<p>Kisha nenda: 192.168.4.1/admin</p></body></html>");
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
      "<!DOCTYPE html><html><head><meta charset='utf-8'><title>OTA Update</title>"
      "<style>body{background:#111;color:#fff;font-family:monospace;padding:30px;text-align:center}"
      "input,button{padding:12px;margin:10px;border-radius:8px;font-size:15px}"
      "button{background:#e91e63;color:white;border:none;cursor:pointer;width:200px}"
      "a{color:#e91e63}</style></head><body>"
      "<h2>🔄 OTA Firmware Update</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='update' accept='.bin'><br>"
      "<button type='submit'>Upload Firmware</button></form>"
      "<br><a href='/admin'>← Admin Panel</a></body></html>");
  });

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.println("OTA: " + upload.filename);
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

  // Maintain WiFi
  if (millis() - lastHeartbeat > 30000) {
    maintainWiFi();
    lastHeartbeat = millis();
    clientCount = WiFi.softAPgetStationNum();
    Serial.println("Clients: " + String(clientCount) + " | Sessions: " + String(sessionCount));
  }

  delay(10);
}
