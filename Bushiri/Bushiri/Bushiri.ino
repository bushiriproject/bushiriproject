#include "esp_wifi.h"
#include "esp_netif.h"

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

// MAC yako - unapata internet bure
String ownerMAC = "bc:90:63:a2:32:83";

// ======================================================

#define PROJECT_NAME "BUSHIRI"
#define VERSION "3.0.0"
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
      else sessions[i].active = false;
    }
  }
  return false;
}

bool addSession(String mac) {
  mac.toUpperCase();
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].mac == mac) {
      sessions[i].active = true;
      sessions[i].expiry = millis() + ((unsigned long)SESSION_HOURS * 3600000);
      return true;
    }
  }
  if (sessionCount < MAX_CLIENTS) {
    sessions[sessionCount].mac = mac;
    sessions[sessionCount].active = true;
    sessions[sessionCount].expiry = millis() + ((unsigned long)SESSION_HOURS * 3600000);
    sessionCount++;
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
      sessions[i].active = false;
      sessionCount++;
    }
  }
}

// ==================== GET CLIENT MAC ====================
String getClientMAC() {
  IPAddress clientAddr = server.client().remoteIP();
  return getMACFromIP(clientAddr.toString());
}

// ✅ FIXED (IP ↔ MAC working)
String getMACFromIP(String ip) {
  wifi_sta_list_t wifi_sta_list;
  esp_wifi_ap_get_sta_list(&wifi_sta_list);

  esp_netif_sta_list_t netif_sta_list;
  esp_netif_get_sta_list(&wifi_sta_list, &netif_sta_list);

  for (int i = 0; i < netif_sta_list.num; i++) {
    esp_netif_sta_info_t station = netif_sta_list.sta[i];

    char ip_str[16];
    sprintf(ip_str, IPSTR, IP2STR(&station.ip));

    if (String(ip_str) == ip) {
      char macStr[18];
      uint8_t *mac = station.mac;
      sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
              mac[0], mac[1], mac[2],
              mac[3], mac[4], mac[5]);
      return String(macStr);
    }
  }

  return ip;
}