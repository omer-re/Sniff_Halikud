#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <vector>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
#define CHANNEL_SWITCH_INTERVAL 200 // ms
#define CHANNEL_COUNT 13

#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "FS.h"
#include "SPIFFS.h"




#include <vector>
#include <algorithm>
#include <string>
#define LED_GPIO_PIN                     2
#define WIFI_CHANNEL_SWITCH_INTERVAL  (500)
#define WIFI_CHANNEL_MAX               (13)


struct ProbeRequest {
  uint8_t mac[6];
  unsigned long timestamp;
  int rssi;
};

std::vector<ProbeRequest> probeRequests;
unsigned long lastSwitchTime = 0;
int currentChannel = 1;
unsigned long lastPrintTime = 0;
unsigned long lastSaveTime = 0;

void setup() {
  Serial.begin(115200);
  SPIFFS.begin(true);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);

  // Set up promiscuous mode
  WiFi.promiscuousEnable(true);
  WiFi.setPromiscuous(true);
  WiFi.setPromiscuousFilter(WIFI_PROMIS_FILTER_MASK_MGMT);
  WiFi.promiscuousSetRxCallback(&onProbeRequest);
}


void loop() {
  unsigned long currentTime = millis();

  // Switch channels
  if (currentTime - lastSwitchTime > CHANNEL_SWITCH_INTERVAL) {
    lastSwitchTime = currentTime;
    currentChannel = (currentChannel % CHANNEL_COUNT) + 1;
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  }

  // Print top 20 probe requests every minute
  if (currentTime - lastPrintTime > 60000) {
    lastPrintTime = currentTime;
    printTopN(20);
  }

  // Save top 50 probe requests every 5 minutes
  if (currentTime - lastSaveTime > 300000) {
    lastSaveTime = currentTime;
    saveTopNToSPIFFS(50);
  }

  // Check for serial input
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input == "plot") {
      printFileContents("probe_timing");
    }
  }
}

void IRAM_ATTR onProbeRequest(void* buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
if (type != WIFI_PKT_MGMT || pkt->payload[0] != 0x40) {
return;
}

int rssi = pkt->rx_ctrl.rssi;
if (rssi >= -90) {
return;
}

ProbeRequest probeRequest;
memcpy(probeRequest.mac, pkt->payload + 10, 6);
probeRequest.timestamp = millis();
probeRequest.rssi = rssi;

// Add probe request to the list
probeRequests.push_back(probeRequest);
}

bool timeDifferenceComparator(const ProbeRequest& a, const ProbeRequest& b) {
return (a.timestamp < b.timestamp);
}

void printTopN(int n) {
std::sort(probeRequests.begin(), probeRequests.end(), timeDifferenceComparator);
for (int i = 0; i < n && i < probeRequests.size(); ++i) {
ProbeRequest probeRequest = probeRequests[i];
Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X Time difference: %lu ms\n",
probeRequest.mac[0], probeRequest.mac[1], probeRequest.mac[2],
probeRequest.mac[3], probeRequest.mac[4], probeRequest.mac[5],
probeRequest.timestamp);
}
}

void printTopN(int n) {
  std::sort(probeRequests.begin(), probeRequests.end(), timeDifferenceComparator);
  for (int i = 0; i < n && i < probeRequests.size(); ++i) {
    ProbeRequest probeRequest = probeRequests[i];
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X Time difference: %lu ms RSSI: %d dBm\n",
                  probeRequest.mac[0], probeRequest.mac[1], probeRequest.mac[2],
                  probeRequest.mac[3], probeRequest.mac[4], probeRequest.mac[5],
                  probeRequest.timestamp, probeRequest.rssi);
  }
}

void saveTopNToSPIFFS(int n) {
  File file = SPIFFS.open("/probe_timing", "a");
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }

  std::sort(probeRequests.begin(), probeRequests.end(), timeDifferenceComparator);
  for (int i = 0; i < n && i < probeRequests.size(); ++i) {
    ProbeRequest probeRequest = probeRequests[i];
    file.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X Time difference: %lu ms RSSI: %d dBm\n",
probeRequest.mac[0], probeRequest.mac[1], probeRequest.mac[2],
probeRequest.mac[3], probeRequest.mac[4], probeRequest.mac[5],
probeRequest.timestamp, probeRequest.rssi);
}
file.close();
Serial.println("Top 50 probe requests appended to SPIFFS");
}


void printFileContents(const char* path) {
File file = SPIFFS.open(path, "r");
if (!file) {
Serial.println("Failed to open file for reading");
return;
}

while (file.available()) {
Serial.write(file.read());
}
file.close();
}
