#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"


#include <WiFi.h>
#include <vector>
#include <HTTPClient.h>
#include <array>

const char* ssid = "AAMCAR";
const char* password = "goodkarma";
const char* formUrl = "https://docs.google.com/forms/d/e/1FAIpQLScrYCyObHtTmms5ptSFftnXoKAOv4jsaYdjt4vKf4CKikyVFQ/formResponse";
const char* _device = "entry.1587348386";
const char* _packet_type = "entry.270531172";
const char* _channel = "entry.274816694";
const char* _rssi = "entry.1966244937";
const char* _dest_mac = "entry.1509951218";
const char* _src_mac = "entry.1035355794";
const char* _opt_mac = "entry.1805723102";


#define LED_GPIO_PIN                     5
#define WIFI_CHANNEL_SWITCH_INTERVAL  (500)
#define WIFI_CHANNEL_MAX               (13)

uint8_t level = 0, channel = 1;

static wifi_country_t wifi_country = {.cc="CN", .schan = 1, .nchan = 13}; //Most recent esp32 library struct
uint8_t esp_mac[6];

typedef struct {
  unsigned frame_ctrl:16;
  unsigned duration_id:16;
  uint8_t addr1[6]; /* receiver address */
  uint8_t addr2[6]; /* sender address */
  uint8_t addr3[6]; /* filtering address */
  unsigned sequence_ctrl:16;
  uint8_t addr4[6]; /* optional */
} wifi_ieee80211_mac_hdr_t;

typedef struct {
  wifi_ieee80211_mac_hdr_t hdr;
  uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;

static esp_err_t event_handler(void *ctx, system_event_t *event);
static void wifi_sniffer_init(void);
static void wifi_sniffer_set_channel(uint8_t channel);
static const char *wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type);
static void wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type);

esp_err_t event_handler(void *ctx, system_event_t *event)
{
  return ESP_OK;
}

void wifi_sniffer_init(void)
{
  nvs_flash_init();
  tcpip_adapter_init();
  ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
  ESP_ERROR_CHECK( esp_wifi_set_country(&wifi_country) ); /* set country for channel range [1, 13] */
  ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
  ESP_ERROR_CHECK( esp_wifi_start() );
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
}

void wifi_sniffer_set_channel(uint8_t channel)
{
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

const char * wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type)
{
  switch(type) {
  case WIFI_PKT_MGMT: return "MGMT";
  case WIFI_PKT_DATA: return "DATA";
  default:
  case WIFI_PKT_MISC: return "MISC";
  }
}

void wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT)
    return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
  const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
  const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;
  uint32_t crc = ppkt->rx_ctrl.sig_mode ? ppkt->rx_ctrl.crc : 0;

  printf("PACKET TYPE=%s, CHAN=%02d, CRC=%d RSSI=%02d,"
    " ADDR1=%02x:%02x:%02x:%02x:%02x:%02x,"
    " ADDR2=%02x:%02x:%02x:%02x:%02x:%02x,"
    " ADDR3=%02x:%02x:%02x:%02x:%02x:%02x\n",
    wifi_sniffer_packet_type2str(type),
    ppkt->rx_ctrl.channel,
    crc,
    ppkt->rx_ctrl.rssi,
    /* ADDR1 */
    hdr->addr1[0],hdr->addr1[1],hdr->addr1[2],
    hdr->addr1[3],hdr->addr1[4],hdr->addr1[5],
    /* ADDR2 */
    hdr->addr2[0],hdr->addr2[1],hdr->addr2[2],
    hdr->addr2[3],hdr->addr2[4],hdr->addr2[5],
    /* ADDR3 */
    hdr->addr3[0],hdr->addr3[1],hdr->addr3[2],
    hdr->addr3[3],hdr->addr3[4],hdr->addr3[5]
  );
    String packet_type=wifi_sniffer_packet_type2str(type);
    String channel=ppkt->rx_ctrl.channel;
    int _rssi;
    char arr1[32];/* receiver address */
    char arr2[32];/* sender address */
    char macs[2][32];
    _rssi = ppkt->rx_ctrl.rssi;
    // ignore packets with rssi lower than threshold
    if (_rssi < (RSSI_TH)) return;
    snprintf(macs[0], sizeof(macs[0]), "%02x%02x%02x%02x%02x%02x", hdr->addr1[0], hdr->addr1[1], hdr->addr1[2],
             hdr->addr1[3], hdr->addr1[4], hdr->addr1[5]);
    snprintf(macs[1], sizeof(macs[1]), "%02x%02x%02x%02x%02x%02x", hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
             hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);


    sendDataToGoogleForm(packet_type, channel, rssi, macs[0], macs[1]);

}

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin 5 as an output.
  Serial.begin(115200);
    // Connect to Wi-Fi network
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }

    Serial.println("Connected to WiFi");
    WiFi.macAddress(esp_mac);

    //

  delay(10);
  wifi_sniffer_init();
  pinMode(LED_GPIO_PIN, OUTPUT);
}

// the loop function runs over and over again forever
void loop() {
  //Serial.print("inside loop");
  delay(1000); // wait for a second

  if (digitalRead(LED_GPIO_PIN) == LOW)
    digitalWrite(LED_GPIO_PIN, HIGH);
  else
    digitalWrite(LED_GPIO_PIN, LOW);
  vTaskDelay(WIFI_CHANNEL_SWITCH_INTERVAL / portTICK_PERIOD_MS);
  wifi_sniffer_set_channel(channel);
  channel = (channel % WIFI_CHANNEL_MAX) + 1;
}


bool sendDataToGoogleForm(String packet_type, String channel, String rssi, String dest_mac, String src_mac){


// Create an HTTPClient object
HTTPClient http;
// Convert the MAC address to a string
String macStr = String(esp_mac[0], HEX) + ":" + String(esp_mac[1], HEX) + ":" + String(esp_mac[2], HEX) + ":" + String(esp_mac[3], HEX) + ":" + String(esp_mac[4], HEX) + ":" + String(esp_mac[5], HEX);

// Set the URL for the Google Form
http.begin(formUrl);

// Set the headers for the request
http.addHeader("Content-Type", "application/x-www-form-urlencoded");

// Construct the payload from the values vector
String payload = "";
payload += String(_packet_type) + "=" + packet_type + "&";
payload += String(_channel) + "=" + channel + "&";
payload += String(_rssi) + "=" + rssi + "&";
payload += String(_dest_mac) + "=" + dest_mac + "&";
payload += String(_src_mac) + "=" + src_mac + "&";
payload += String(_device) + "=" + esp_mac + "&";
//  payload += String(_opt_mac) + "=" + String(values[6]);
Serial.println(payload);
Serial.println(http.POST(payload));

// Send the POST request
int httpResponseCode = http.POST(payload);

// Check for errors and return the result
if (httpResponseCode == HTTP_CODE_OK) {
String response = http.getString();
Serial.println(response);
return true;
} else {
Serial.println("Error sending data to Google Form: " + String(httpResponseCode));
return false;
}

// Clean up
http.end();
}
