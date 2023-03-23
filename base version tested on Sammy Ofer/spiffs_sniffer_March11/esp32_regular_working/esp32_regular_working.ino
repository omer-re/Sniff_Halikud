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
#define LED_GPIO_PIN                     5
#define WIFI_CHANNEL_SWITCH_INTERVAL  (500)
#define WIFI_CHANNEL_MAX               (13)

#define BATCH_SIZE 100

// WIFI SNIFFER FUNCTIONS //
uint8_t level = 0, channel = 1;

static wifi_country_t wifi_country = {.cc = "CN", .schan = 1, .nchan = 13}; //Most recent esp32 library struct

typedef struct {
  unsigned frame_ctrl: 16;
  unsigned duration_id: 16;
  uint8_t addr1[6]; /* receiver address */
  uint8_t addr2[6]; /* sender address */
  uint8_t addr3[6]; /* filtering address */
  unsigned sequence_ctrl: 16;
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

// Create a vector of strings
int dup_remover_calls = 0;

std::vector<String> strVector;
void remove_duplicates() {

  // Sort the vector to make duplicates adjacent
  std::sort(strVector.begin(), strVector.end());

  // Use std::unique() to remove duplicates
  auto last = std::unique(strVector.begin(), strVector.end());
  strVector.erase(last, strVector.end());
  dup_remover_calls++;
  //  // Print the unique strings
  //  for (auto str : strVector) {
  //    Serial.println(str);
  //  }
}

void print_vector() {
  Serial.println("Vector contains: ");
  // Print the vector
  for (auto i : strVector) {
    Serial.println(i);
  }
  Serial.println(strVector.size());

}


String vector_to_paragraph() {
  String paragraph;
  for (auto str : strVector) {
    paragraph += str + "\n";
  }
  return paragraph;
  // Print the paragraph
  //  std::cout << paragraph;

}

void wifi_sniffer_set_channel(uint8_t channel)
{
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

const char * wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type)
{
  switch (type) {
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
  //Serial.println(ipkt->hdr);
  //  Serial.print(hdr->addr1[0]);Serial.print(hdr->addr1[1]);Serial.print(hdr->addr1[2]);Serial.print(hdr->addr1[3]);Serial.print(hdr->addr1[4]);Serial.println(hdr->addr1[5]);
  //  Serial.println(String(hdr->addr1[0]+hdr->addr1[1]+hdr->addr1[2]+hdr->addr1[3]+hdr->addr1[4]+hdr->addr1[5]));
  //  Serial.println(String(hdr->addr1[0]+hdr->addr1[1]+hdr->addr1[2]+hdr->addr1[3]+hdr->addr1[4]+hdr->addr1[5]));

  char arr1[32];
  char arr2[32];
  char arr3[32];
  char macs[3][32];

  snprintf(macs[0], sizeof(macs[0]), "%02x%02x%02x%02x%02x%02x", hdr->addr1[0], hdr->addr1[1], hdr->addr1[2], hdr->addr1[3], hdr->addr1[4], hdr->addr1[5]);
  snprintf(macs[1], sizeof(macs[1]), "%02x%02x%02x%02x%02x%02x", hdr->addr2[0], hdr->addr2[1], hdr->addr2[2], hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
  snprintf(macs[2], sizeof(macs[2]), "%02x%02x%02x%02x%02x%02x", hdr->addr3[0], hdr->addr3[1], hdr->addr3[2], hdr->addr3[3], hdr->addr3[4], hdr->addr3[5]);
  if (String(macs[0]) != "ffffffffffff") {
    //    Serial.println(String(macs[0]));
    strVector.push_back(String(macs[0]));
  }
  if (String(macs[1]) != "ffffffffffff") {
    //    Serial.println(String(macs[1]));
    strVector.push_back(String(macs[1]));
  }
  if (String(macs[2]) != "ffffffffffff") {
    //    Serial.println(String(macs[2]));
    strVector.push_back(String(macs[2]));
  }


  remove_duplicates();
  if (strVector.size() > BATCH_SIZE) {
    //  print_vector();
    String parag = vector_to_paragraph();
    Serial.println("parag   ");

    Serial.println(parag);
    int res = appendFile(SPIFFS, "/crowd_count.txt", parag.c_str());
    if (res == 0) {
      // writing succeeded, clear temp vector
      strVector.clear();
      Serial.println("strVector cleared");

    }
  }
}

int forced_writing() {
  remove_duplicates();

  //  print_vector();
  String parag = vector_to_paragraph();
  Serial.println("parag   ");

  Serial.println(parag);
  int res = appendFile(SPIFFS, "/crowd_count.txt", parag.c_str());
  if (res == 0) {
    // writing succeeded, clear temp vector
    strVector.clear();
    Serial.println("strVector cleared");
    return 0;
  }
  return -1;

}


// SPIFFS FUNCTIONS //
int counter;

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return;
  }
  Serial.println("- read from file:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
  file.close();
}

int appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\r\n", path);
  counter++;

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("- failed to open file for appending");
    return -1;
  }
  if (file.print(message)) {
    file.print("\n");
    Serial.print(counter);
    //    update_counter();

    Serial.println("  - message appended");
    return 0;
  } else {
    Serial.println("- append failed");
  }
  file.close();
  return -1;
}

void renameFile(fs::FS &fs, const char * path1, const char * path2) {
  Serial.printf("Renaming file %s to %s\r\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("- file renamed");
  } else {
    Serial.println("- rename failed");
  }
}

void deleteFile(fs::FS &fs, const char * path) {
  Serial.printf("Deleting file: %s\r\n", path);
  if (fs.remove(path)) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}

void testFileIO(fs::FS &fs, const char * path) {
  Serial.printf("Testing file I/O with %s\r\n", path);

  static uint8_t buf[512];
  size_t len = 0;
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }

  size_t i;
  Serial.print("- writing" );
  uint32_t start = millis();
  for (i = 0; i < 2048; i++) {
    if ((i & 0x001F) == 0x001F) {
      Serial.print(".");
    }
    file.write(buf, 512);
  }
  Serial.println("");
  uint32_t end = millis() - start;
  Serial.printf(" - %u bytes written in %u ms\r\n", 2048 * 512, end);
  file.close();

  file = fs.open(path);
  start = millis();
  end = start;
  i = 0;
  if (file && !file.isDirectory()) {
    len = file.size();
    size_t flen = len;
    start = millis();
    Serial.print("- reading" );
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      if ((i++ & 0x001F) == 0x001F) {
        Serial.print(".");
      }
      len -= toRead;
    }
    Serial.println("");
    end = millis() - start;
    Serial.printf("- %u bytes read in %u ms\r\n", flen, end);
    file.close();
  } else {
    Serial.println("- failed to open file for reading");
  }
}

bool is_file_exist(fs::FS &fs, const char * path) {

  File file = fs.open(path);
  if (!file) {
    Serial.println("- file doesn't exist");
    return false;
  }
  Serial.println("- file exist");

  return true;

}
//
//uint64_t generateRandomMac() {
//  uint8_t mac[6];
//  for (int i = 0; i < 6; i++) {
//    mac[i] = esp_random() & 0xff;
//  }
//  return ((uint64_t)mac[0] << 40) | ((uint64_t)mac[1] << 32) | ((uint64_t)mac[2] << 24) | ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | ((uint64_t)mac[5]);
//}

int tries = 0;


void read_counter(fs::FS &fs, const char * path) {

  File file = fs.open(path);
  // Initialize row count to zero
  int rowCount = 0;

  // Loop through each line in the file and increment the row count
  while (file.available()) {
    char c = file.read();
    if (c == '\n') {
      rowCount++;
    }
  }

  // Close the file
  file.close();

  // Print the number of rows
  Serial.println("Number of rows: " + String(rowCount));
  delay(3000);

}

void listdir() {
  listDir(SPIFFS, "/", 0);


}


// ARDUINO FUNCTIONS
void setup() {
  Serial.begin(115200);
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS Mount Failed");
    Serial.println(tries++);
    return;
  }

  listDir(SPIFFS, "/", 0);
  if (is_file_exist(SPIFFS, "/crowd_count.txt") == false) {
    Serial.println("file doesn't exist, create one.");

    writeFile(SPIFFS, "/crowd_count.txt", "crowd_count ");

  }


  delay(1000);

  Serial.println( "Test complete" );
  appendFile(SPIFFS, "/crowd_count.txt", "new run\n");
  readFile(SPIFFS, "/crowd_count.txt");
  read_counter(SPIFFS, "/crowd_count.txt");

  wifi_sniffer_init();

}




String input;

void loop() {
  if (Serial.available() > 0) {
    input = Serial.readStringUntil('\n');

    if (input == "size") {

      read_counter(SPIFFS, "/crowd_count.txt");
      delay(1000);

    }

    if (input == "dir") {
      // Print all entries
      listdir();
      delay(5000);

    }
    if (input == "dupr") {

      Serial.print("dup_remover_calls:  ");

      Serial.println(dup_remover_calls);
      delay(3000);

    }

    if (input == "fw") {
      forced_writing();
      delay(3000);

    }
    if (input == "vecs") {
      // Print current temp vector's size
      Serial.print("current temp vector's size:  "); Serial.println(strVector.size());
      delay(3000);

    }
    if (input == "plot") {
      // Print all entries
      readFile(SPIFFS, "/crowd_count.txt");
      read_counter(SPIFFS, "/crowd_count.txt");

      delay(5000);

    }
  }
  //  uint64_t mac = generateRandomMac();
  //
  //  appendFile(SPIFFS, "/crowd_count.txt", String(mac).c_str());
}
