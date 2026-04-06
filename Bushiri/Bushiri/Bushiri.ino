
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_wifi.h"
#include "tcpip_adapter.h"

// ====================================
// BUSHIRI v2.0 CONFIGURATION
// ====================================
const char* AP_SSID      = "Bushiri WiFi";
const char* AP_PASS      = "";
const char* OWNER_MAC    = "bc:90:63:a2:32:83";
const char* VPS_HOST     = "bushiri-project.onrender.com";
const char* VPS_TOKEN    = "BUSHIRI_SECRET_2024";
const char* ADMIN_PASS   = "admin123";
const char* STA_PASS_ALT = ".kibushi1";
const int   VPS_PORT     = 443;

// ====================================
// STATE
// ====================================
bool   vps_connected      = false;
bool   hhb_connected      = false;
String current_ssid       = "";
String current_ip         = "";
unsigned long last_heartbeat     = 0;
unsigned long last_reconnect_try = 0;

WebServer        server(80);
Preferences      prefs;
WiFiClientSecure secureClient;

// ====================================
// HELPER: Pata MAC ya mteja
// ====================================
String getMacOfClient() {
  IPAddress clientIP = server.client().remoteIP();
  wifi_sta_list_t stationList;
  esp_wifi_ap_get_sta_list(&stationList);
  tcpip_adapter_sta_list_t adapterList;
  if (tcpip_adapter_get_sta_list(&stationList, &adapterList) == ESP_OK) {
    for (int i = 0; i < adapterList.num; i++) {
      IPAddress sta_ip = adapterList.sta[i].ip.addr;
      if (sta_ip == clientIP) {
        uint8_t* m = stationList.sta[i].mac;
        char mac[18];
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                 m[0], m[1], m[2], m[3], m[4], m[5]);
        return String(mac);
      }
    }
  }
  return "unknown";
}

// ====================================
// VPS: Thibitisha malipo
// Inarudisha dakika za muda au -1 (hakuna malipo) au -2 (error)
// ====================================
int verifyPaymentVPS(String phone, String mac) {
  if (!hhb_connected) return -2;
  secureClient.setInsecure();
  HTTPClient http;
  String url = "https://" + String(VPS_HOST) +
               "/verify-payment?phone=" + phone +
               "&mac=" + mac +
               "&token=" + String(VPS_TOKEN);
  http.begin(secureClient, url);
  http.setTimeout(10000);
  int code = http.GET();
  if (code == 200) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, http.getString());
    int minutes = doc["minutes"] | 0;
    http.end();
    return minutes > 0 ? minutes : -1;
  }
  http.end();
  return (code == 404) ? -1 : -2;
}

// ====================================
// VPS: Angalia session hai
// ====================================
bool checkSessionVPS(String mac) {
  if (!hhb_connected || mac == "unknown") return false;
  secureClient.setInsecure();
  HTTPClient http;
  String url = "https://" + String(VPS_HOST) +
               "/check-session?mac=" + mac +
               "&token=" + String(VPS_TOKEN);
  http.begin(secureClient, url);
  http.setTimeout(5000);
  int code = http.GET();
  http.end();
  return (code == 200);
}

// ====================================
// VPS: Heartbeat
// ====================================
void heartbeatVPS() {
  if (!hhb_connected) { vps_connected = false; return; }
  secureClient.setInsecure();
  HTTPClient http;
  String url = "https://" + String(VPS_HOST) +
               "/heartbeat?mac=" + WiFi.macAddress() +
               "&fw=v2.0&token=" + String(VPS_TOKEN);
  http.begin(secureClient, url);
  http.setTimeout(5000);
  int code = http.GET();
  http.end();
  vps_connected = (code == 200);
  Serial.println(vps_connected ? "💚 VPS OK" : "💔 VPS offline");
}

// ====================================
// WiFi: Unganika na hhb
// ====================================
void connectToHHB() {
  if (current_ssid.length() == 0) return;
  String pass = prefs.getString("pass", STA_PASS_ALT);
  WiFi.begin(current_ssid.c_str(), pass.c_str());
  Serial.print("📡 Connecting: " + current_ssid);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); Serial.print("."); attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    current_ip = WiFi.localIP().toString();
    hhb_connected = true;
    Serial.println("\n✅ hhb: " + current_ip);
  } else {
    hhb_connected = false;
    WiFi.disconnect();
    Serial.println("\n❌ hhb failed");
  }
}

// ====================================
// HTML: Login Page
// ====================================
void serveLoginPage(String error = "") {
  String html = R"rawliteral(<!DOCTYPE html><html><head>
<title>Bushiri WiFi</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="UTF-8">
<style>
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:'Segoe UI',sans-serif;min-height:100vh;
  background:linear-gradient(135deg,#0f2027,#203a43,#2c5364);
  display:flex;align-items:center;justify-content:center;padding:20px;}
.box{background:rgba(255,255,255,0.07);backdrop-filter:blur(20px);
  border:1px solid rgba(255,255,255,0.15);padding:40px 30px;
  border-radius:24px;max-width:380px;width:100%;text-align:center;
  box-shadow:0 20px 60px rgba(0,0,0,0.5);}
.logo{font-size:52px;margin-bottom:10px;}
h2{color:#fff;font-size:22px;margin-bottom:6px;}
.sub{color:rgba(255,255,255,0.6);font-size:14px;margin-bottom:20px;}
.price{background:linear-gradient(135deg,#11998e,#38ef7d);color:#fff;
  padding:10px 20px;border-radius:50px;font-weight:700;font-size:15px;
  display:inline-block;margin-bottom:24px;}
input[type=tel]{width:100%;padding:16px;font-size:18px;
  background:rgba(255,255,255,0.12);border:1px solid rgba(255,255,255,0.2);
  color:#fff;border-radius:12px;text-align:center;letter-spacing:2px;
  outline:none;margin-bottom:16px;}
input::placeholder{color:rgba(255,255,255,0.4);}
button{width:100%;padding:16px;
  background:linear-gradient(135deg,#11998e,#38ef7d);
  color:#fff;font-size:17px;font-weight:700;border:none;
  border-radius:12px;cursor:pointer;}
.error{background:rgba(255,68,68,0.2);border:1px solid rgba(255,68,68,0.4);
  color:#ff8080;padding:12px;border-radius:10px;font-size:14px;margin-bottom:16px;}
.steps{text-align:left;background:rgba(255,255,255,0.05);
  border-radius:12px;padding:16px;margin-top:20px;}
.steps p{color:rgba(255,255,255,0.7);font-size:13px;padding:5px 0;line-height:1.6;}
</style></head><body><div class="box">
<div class="logo">📶</div>
<h2>Bushiri WiFi</h2>
<p class="sub">Lipa kupata internet ya haraka</p>
<div class="price">💰 Ksh 700 = Masaa 15</div>)rawliteral";

  if (error != "") html += "<div class='error'>" + error + "</div>";

  html += R"rawliteral(
<form method="POST" action="/login">
<input type="tel" name="phone" placeholder="0712 345 678" maxlength="13" required>
<button>✅ Thibitisha Malipo</button></form>
<div class="steps">
<p>1️⃣ Lipa: <strong>0790385813</strong></p>
<p>2️⃣ Ingiza nambari yako hapa</p>
<p>3️⃣ Internet inawaka! 🚀</p>
</div></div></body></html>)rawliteral";

  server.send(200, "text/html", html);
}

// ====================================
// HTML: Dashboard
// ====================================
void serveDashboard(String mac) {
  bool isOwner = (mac == String(OWNER_MAC));
  String html = R"rawliteral(<!DOCTYPE html><html><head>
<title>Bushiri - Online</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="UTF-8">
<style>
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:'Segoe UI',sans-serif;background:#0d0d0d;color:#fff;padding:20px;}
h1{font-size:20px;margin-bottom:16px;text-align:center;}
.badge{display:block;text-align:center;
  background:linear-gradient(135deg,#11998e,#38ef7d);
  color:#fff;padding:8px;border-radius:50px;font-weight:700;
  font-size:13px;margin-bottom:16px;}
.card{background:#1a1a1a;border:1px solid #2a2a2a;padding:16px;
  border-radius:14px;margin-bottom:12px;}
.card h3{font-size:12px;color:#666;margin-bottom:10px;
  text-transform:uppercase;letter-spacing:1px;}
.row{display:flex;justify-content:space-between;
  padding:7px 0;border-bottom:1px solid #222;font-size:14px;}
.row:last-child{border:none;}
.val{color:#38ef7d;font-weight:600;}
a.btn{display:block;background:#1a1a1a;border:1px solid #2a2a2a;
  color:#38ef7d;padding:13px;border-radius:10px;text-decoration:none;
  margin-bottom:8px;font-size:14px;text-align:center;}
</style></head><body>
<h1>🎉 Umeingia Bushiri WiFi!</h1>)rawliteral";

  if (isOwner) html += "<div class='badge'>👑 OWNER - Bure Daima</div>";
  else         html += "<div class='badge'>✅ Umeunganishwa</div>";

  html += "<div class='card'><h3>Hali ya Mfumo</h3>";
  html += "<div class='row'><span>VPS</span><span class='val'>" +
          String(vps_connected ? "🟢 Online" : "🔴 Offline") + "</span></div>";
  html += "<div class='row'><span>Internet (hhb)</span><span class='val'>" +
          String(hhb_connected ? "🟢 " + current_ip : "🔴 Offline") + "</span></div>";
  html += "<div class='row'><span>Wateja</span><span class='val'>" +
          String(WiFi.softAPgetStationNum()) + "</span></div>";
  html += "</div>";

  if (isOwner) {
    html += R"rawliteral(<div class="card"><h3>Admin Controls</h3>
<a class="btn" href="/admin">🔧 Admin Panel</a>
<a class="btn" href="/wifi-config">📶 WiFi Config</a>
<a class="btn" href="/data-stats">📊 Stats (JSON)</a>
</div>)rawliteral";
  }

  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ====================================
// HTML: Admin Page
// ====================================
void serveAdminPage() {
  String html = "<!DOCTYPE html><html><head><title>Admin</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;background:#111;color:#fff;padding:20px;}"
    ".card{background:#1a1a1a;padding:18px;border-radius:12px;margin:10px 0;}"
    "a{display:block;background:#222;color:#38ef7d;padding:14px;"
    "border-radius:8px;text-decoration:none;margin:6px 0;text-align:center;}"
    ".info{font-size:13px;color:#aaa;line-height:2.2;}</style></head><body>"
    "<h1>🔧 Bushiri Admin</h1><div class='card'><div class='info'>"
    "AP: <b>" + String(AP_SSID) + "</b><br>"
    "MAC: <b>" + WiFi.macAddress() + "</b><br>"
    "Wateja: <b>" + String(WiFi.softAPgetStationNum()) + "</b><br>"
    "VPS: <b>" + String(vps_connected ? "🟢 Online" : "🔴 Offline") + "</b><br>"
    "hhb: <b>" + String(hhb_connected ? "🟢 " + current_ip : "🔴 Offline") + "</b><br>"
    "Uptime: <b>" + String(millis() / 60000) + " min</b>"
    "</div></div><div class='card'>"
    "<a href='/wifi-config'>📶 Configure hhb WiFi</a>"
    "<a href='/data-stats'>📊 Data Stats</a>"
    "</div><p style='color:#444;font-size:12px;margin-top:16px'>OTA: Bushiri-v2.0 | Port 3232</p>"
    "</body></html>";
  server.send(200, "text/html", html);
}

// ====================================
// ROUTE HANDLERS
// ====================================
void handleRoot() {
  String mac = getMacOfClient();
  if (mac == String(OWNER_MAC) || checkSessionVPS(mac)) {
    serveDashboard(mac);
  } else {
    serveLoginPage();
  }
}

void handleCaptive() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void handleLogin() {
  if (!server.hasArg("phone")) { serveLoginPage("❌ Ingiza nambari"); return; }
  String phone = server.arg("phone");
  String mac   = getMacOfClient();
  phone.trim();
  if (phone.startsWith("+255")) phone = "0" + phone.substring(4);
  if (phone.startsWith("255"))  phone = "0" + phone.substring(3);
  Serial.println("🔐 Login: " + phone + " | " + mac);
  int minutes = verifyPaymentVPS(phone, mac);
  if (minutes > 0) {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
    Serial.println("✅ Access: " + String(minutes) + " min");
  } else if (minutes == -1) {
    serveLoginPage("⏰ Hakuna malipo mapya. Lipa kwanza halafu jaribu tena.");
  } else {
    serveLoginPage("❌ Seva haipatikani. Angalia hhb WiFi.");
  }
}

void handleAdmin() {
  if (!server.authenticate("admin", ADMIN_PASS)) return server.requestAuthentication();
  String path = server.uri();
  if (path.indexOf("wifi-config") >= 0) handleWifiConfig();
  else if (path.indexOf("data-stats") >= 0) handleDataStats();
  else serveAdminPage();
}

void handleWifiConfig() {
  if (!server.authenticate("admin", ADMIN_PASS)) return server.requestAuthentication();
  if (server.method() == HTTP_POST && server.hasArg("ssid")) {
    current_ssid = server.arg("ssid");
    prefs.putString("ssid", current_ssid);
    prefs.putString("pass", server.hasArg("pass") ? server.arg("pass") : STA_PASS_ALT);
    server.send(200, "text/html", "<h2>✅ Imehifadhiwa! Rebooting...</h2>"
      "<script>setTimeout(()=>location.href='/admin',4000)</script>");
    delay(3000); ESP.restart();
  } else {
    server.send(200, "text/html",
      "<!DOCTYPE html><html><head><title>WiFi Config</title>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:sans-serif;background:#111;color:#fff;padding:20px;}"
      "input,button{width:100%;padding:14px;margin:8px 0;border-radius:8px;border:none;font-size:16px;}"
      "button{background:#38ef7d;color:#111;font-weight:bold;cursor:pointer;}"
      "a{color:#38ef7d;}</style></head><body>"
      "<h2>📶 WiFi Config</h2>"
      "<form method='POST'>"
      "<input type='text' name='ssid' placeholder='SSID ya hhb' required>"
      "<input type='password' name='pass' placeholder='.kibushi1' value='.kibushi1'>"
      "<button>🔗 Hifadhi & Reboot</button></form>"
      "<a href='/admin'>← Admin</a></body></html>");
  }
}

void handleDataStats() {
  if (!server.authenticate("admin", ADMIN_PASS)) return server.requestAuthentication();
  DynamicJsonDocument doc(512);
  doc["vps_online"] = vps_connected;
  doc["hhb_online"] = hhb_connected;
  doc["hhb_ip"]     = current_ip;
  doc["ap_clients"] = WiFi.softAPgetStationNum();
  doc["uptime_min"] = millis() / 60000;
  String json; serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleTunnel() {
  String mac = getMacOfClient();
  if (mac != String(OWNER_MAC) && !checkSessionVPS(mac)) {
    server.send(403, "text/plain", "❌ Login kwanza!"); return;
  }
  if (!vps_connected) { server.send(503, "text/plain", "❌ VPS offline"); return; }
  String target = server.arg("url");
  if (target.length() == 0) { server.send(400, "text/plain", "Missing url"); return; }
  secureClient.setInsecure();
  HTTPClient http;
  http.begin(secureClient, "https://" + String(VPS_HOST) + "/proxy?url=" + target +
             "&mac=" + mac + "&token=" + String(VPS_TOKEN));
  int code = http.GET();
  server.send(code == 200 ? 200 : 502, "text/html",
              code == 200 ? http.getString() : "VPS Error: " + String(code));
  http.end();
}

// ====================================
// SETUP & LOOP
// ====================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  prefs.begin("bushiri", false);
  current_ssid = prefs.getString("ssid", "");
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("🚀 Bushiri v2.0");
  Serial.println("AP: " + WiFi.softAPIP().toString());
  Serial.println("MAC: " + WiFi.macAddress());

  server.on("/",                    handleRoot);
  server.on("/generate_204",        handleCaptive);
  server.on("/fwlink",              handleCaptive);
  server.on("/hotspot-detect.html", handleCaptive);
  server.on("/connecttest.txt",     handleCaptive);
  server.on("/login",  HTTP_POST,   handleLogin);
  server.on("/admin",               handleAdmin);
  server.on("/wifi-config",         handleWifiConfig);
  server.on("/data-stats",          handleDataStats);
  server.on("/tunnel",              handleTunnel);
  server.begin();
  Serial.println("🌐 Server ready");

  ArduinoOTA.setHostname("Bushiri-v2.0");
  ArduinoOTA.begin();
  Serial.println("🔄 OTA ready");

  if (current_ssid.length() > 0) connectToHHB();
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  if (millis() - last_heartbeat > 30000) {
    heartbeatVPS();
    last_heartbeat = millis();
  }

  // Auto-reconnect: jaribu kila dakika 1 tu
  if (current_ssid.length() > 0 && !hhb_connected) {
    if (millis() - last_reconnect_try > 60000) {
      last_reconnect_try = millis();
      connectToHHB();
    }
  }

  // Detect disconnection
  if (hhb_connected && WiFi.status() != WL_CONNECTED) {
    hhb_connected = false;
    vps_connected = false;
    Serial.println("⚠️ hhb disconnected");
  }
}
