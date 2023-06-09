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
#include <map>

#define LED_GPIO_PIN                     5
#define WIFI_CHANNEL_SWITCH_INTERVAL  (500)
#define WIFI_CHANNEL_MAX               (13)
#define RSSI_TH (-80)
#define BATCH_SIZE 200
#define FIRST_CSV_WAYPOINT 15000
#define CSV_DEDUP_INTERVAL 1000

#define LED_BUILTIN 2


char *FILENAME = "/crowd_count.txt";
std::vector <String> strVector;


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

esp_err_t event_handler(void *ctx, system_event_t *event) {
    return ESP_OK;
}

void wifi_sniffer_init(void) {
    nvs_flash_init();
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_country(&wifi_country)); /* set country for channel range [1, 13] */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
}

// Create a vector of strings
int dup_remover_calls = 0;

int deduplication_counter = 0;

void check_if_deduplication_needed() {
    int row_count = count_rows(FILENAME);
    if ((row_count >= FIRST_CSV_WAYPOINT && deduplication_counter == 0) ||
        (row_count >= (FIRST_CSV_WAYPOINT + deduplication_counter * CSV_DEDUP_INTERVAL))) {
        deduplicate_file_by_rows();
        deduplication_counter++;
        Serial.printf("Deduplicated counter %d\n", deduplication_counter);

    }
}

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
    for (auto i: strVector) {
        Serial.println(i);
    }
    Serial.println(strVector.size());

}

void process_strings(String dest_mac, String src_mac, int _rssi) {
    if (_rssi < (RSSI_TH)) return;
    const String empty_mac = "000";
    const String ignored_mac = "ffffffffffff";
    if (dest_mac != empty_mac) return; // ignore non broadcast


    if (dest_mac == ignored_mac) dest_mac = empty_mac;
    if (src_mac == ignored_mac) src_mac = empty_mac;

    if (dest_mac == src_mac) src_mac = empty_mac;

    String macs_row = dest_mac + "," + src_mac + " :" + _rssi;

    strVector.push_back(macs_row);

}


String vector_to_paragraph() {
    String paragraph;
    for (auto str: strVector) {
        paragraph += str + "\n";
    }
    return paragraph;
    // Print the paragraph
    //  std::cout << paragraph;

}

void wifi_sniffer_set_channel(uint8_t channel) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

const char *wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type) {
    switch (type) {
        case WIFI_PKT_MGMT:
            return "MGMT";
        case WIFI_PKT_DATA:
            return "DATA";
        default:
        case WIFI_PKT_MISC:
            return "MISC";
    }
}

void wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT)
        return;

    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *) buff;
    const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *) ppkt->payload;
    const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;
    //Serial.println(ipkt->hdr);
    //  Serial.print(hdr->addr1[0]);Serial.print(hdr->addr1[1]);Serial.print(hdr->addr1[2]);Serial.print(hdr->addr1[3]);Serial.print(hdr->addr1[4]);Serial.println(hdr->addr1[5]);
    //  Serial.println(String(hdr->addr1[0]+hdr->addr1[1]+hdr->addr1[2]+hdr->addr1[3]+hdr->addr1[4]+hdr->addr1[5]));
    //  Serial.println(String(hdr->addr1[0]+hdr->addr1[1]+hdr->addr1[2]+hdr->addr1[3]+hdr->addr1[4]+hdr->addr1[5]));

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


    process_strings(String(macs[0]), String(macs[1]), _rssi);

    remove_duplicates();
    if (strVector.size() > BATCH_SIZE) {
        //  print_vector();
        String parag = vector_to_paragraph();
        Serial.println("parag   ");

        Serial.println(parag);
        int res = appendFile(SPIFFS, FILENAME, parag.c_str());
        if (res == 0) {
            // writing succeeded, clear temp vector
            strVector.clear();
            Serial.println("strVector cleared");

        }
        check_if_deduplication_needed();
    }
}

int forced_writing() {
    remove_duplicates();

    //  print_vector();
    String parag = vector_to_paragraph();
    Serial.println("parag   ");

    Serial.println(parag);
    int res = appendFile(SPIFFS, FILENAME, parag.c_str());
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

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
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

void readFile(fs::FS &fs, const char *path) {
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

void writeFile(fs::FS &fs, const char *path, const char *message) {
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

int appendFile(fs::FS &fs, const char *path, const char *message) {
    Serial.printf("Appending to file: %s\r\n", path);
    counter++;

    File file = fs.open(path, FILE_APPEND);
    if (!file) {
        Serial.println("- failed to open file for appending");
        return -1;
    }
    if (file.print(message)) {
        file.print("\n");
        Serial.print("Appended: ");

        Serial.print(counter);
        //    update_counter();

        Serial.println(" insertions completed  - message appended");
        return 0;
    } else {
        Serial.println("- append failed");
    }
    file.close();
    return -1;
}

int count_rows(const char *filename) {
    File file = SPIFFS.open(filename, "r");
    int count = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() > 0) count++;
    }
    file.close();
    return count;
}

void renameFile(fs::FS &fs, const char *path1, const char *path2) {
    Serial.printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2)) {
        Serial.println("- file renamed");
    } else {
        Serial.println("- rename failed");
    }
}

void deleteFile(fs::FS &fs, const char *path) {
    Serial.printf("Deleting file: %s\r\n", path);
    if (fs.remove(path)) {
        Serial.println("- file deleted");
    } else {
        Serial.println("- delete failed");
    }
}

void testFileIO(fs::FS &fs, const char *path) {
    Serial.printf("Testing file I/O with %s\r\n", path);

    static uint8_t buf[512];
    size_t len = 0;
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("- failed to open file for writing");
        return;
    }

    size_t i;
    Serial.print("- writing");
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
        Serial.print("- reading");
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

bool is_file_exist(fs::FS &fs, const char *path) {

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


void read_counter(fs::FS &fs, const char *path) {

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


void deduplicate_file_by_rows() {
    // Open the file for reading
    File file = SPIFFS.open(FILENAME, "r");

    // Create a map to store the key-value pairs
    std::map<std::string, int> keyValuePairs;

    // Read the lines from the file and parse the key-value pairs
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() > 0) {
            std::string delimiter = ":";
            int pos = line.indexOf(delimiter.c_str());

            // If the delimiter is found, split the line into key and value
            if (pos != -1) {
                std::string key = line.substring(0, pos).c_str();
                std::string value = line.substring(pos + delimiter.length()).c_str();
                int intValue = std::stoi(value);

                // If the key is already in the map, update its value to the highest number found
                if (keyValuePairs.find(key) != keyValuePairs.end()) {
                    keyValuePairs[key] = std::max(keyValuePairs[key], intValue);
                } else {
                    keyValuePairs[key] = intValue;
                }
            }
        }
    }

    // Close the file
    file.close();

    // Open the file for writing
    File outFile = SPIFFS.open(FILENAME, "w");

    // Write the key-value pairs to the file
    for (const auto& [key, value] : keyValuePairs) {
        outFile.print(key.c_str());
        outFile.print(":");
        outFile.println(value);
    }

    // Close the file
    outFile.close();
}


void old_deduplicate_file_by_rows() {
    if (!SPIFFS.exists(FILENAME)) {
        Serial.printf("File %s does not exist\n", FILENAME);
        return;
    }

    // Read the text file and store the lines in a vector
    File file = SPIFFS.open(FILENAME, "r");
    std::vector <String> lines;
    while (file.available()) {
        lines.push_back(file.readStringUntil('\n'));
    }
    file.close();

    // Process the lines and create a map to store the unique rows and their associated highest numbers.
    std::map<String, int> dedupMap;
    for (const String &line: lines) {
        int delimiterIndex = line.indexOf(':'); // break line into key and value by delimiter
        String key = line.substring(0, delimiterIndex); // key is mac_dest,mac_src
        int num = line.substring(delimiterIndex + 1).toInt(); // value is the rssi

        auto iter = dedupMap.find(key);
        if (iter == dedupMap.end()) {
            dedupMap[key] = num;
        } else {
            if (num > iter->second) {
                dedupMap[key] = num;
            }
        }
    }
    // Write the deduplicated rows back to the SPIFFS file.
    File outFile = SPIFFS.open(FILENAME, "w");
    if (!outFile) {
        Serial.println("Failed to open the output file.");
        return;
    }

    for (const auto &entry: dedupMap) {
        String line = entry.first + String(entry.second).substring(1);
        outFile.println(line);
    }

    outFile.close();
    Serial.println("Deduplication complete");
}



//void deduplicate_file_by_rows() {
//    if (!SPIFFS.exists(FILENAME)) {
//        Serial.printf("File %s does not exist\n", FILENAME);
//        return;
//    }
//
//    // Read the text file and store the lines in a vector
//    File file = SPIFFS.open(FILENAME, "r");
//    std::vector<String> lines;
//    while (file.available()) {
//        String line = file.readStringUntil('\n');
//        line.trim();
//        lines.push_back(line);
//    }
//    file.close();
//
//    // Remove duplicates
//    sort(lines.begin(), lines.end());
//    lines.erase(unique(lines.begin(), lines.end()), lines.end());
//
//    // Overwrite the text file with deduplicated lines
//    File new_file = SPIFFS.open(FILENAME, "w");
//    for (const auto &line : lines) {
//        new_file.println(line);
//    }
//    new_file.close();
//
//    Serial.printf("Deduplicated file %s\n", FILENAME);
//}


int percent_full() {
    float totalBytes = float(SPIFFS.totalBytes());
    float usedBytes = float(SPIFFS.usedBytes());
    float freeBytes = totalBytes - usedBytes;
    float freePercentage = (freeBytes / totalBytes) * 100;
    return freePercentage;

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
    if (is_file_exist(SPIFFS, FILENAME) == false) {
        Serial.println("file doesn't exist, create one.");

        writeFile(SPIFFS, FILENAME, "crowd_count ");

    }


    delay(1000);

    Serial.println("Test complete");
    appendFile(SPIFFS, FILENAME, "new run\n");
    readFile(SPIFFS, FILENAME);
    read_counter(SPIFFS, FILENAME);

    wifi_sniffer_init();

}


String input;

void loop() {

    if (Serial.available() > 0) {
        input = Serial.readStringUntil('\n');

        if (input == "size") {

            read_counter(SPIFFS, FILENAME);
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
            Serial.print("current temp vector's size:  ");
            Serial.println(strVector.size());
            delay(3000);

        }
        if (input == "plot") {
            // Print all entries
            readFile(SPIFFS, FILENAME);
            read_counter(SPIFFS, FILENAME);

            delay(5000);

        }

        if (input == "perc") {
            Serial.print("Percent free:  ");
            Serial.println(percent_full());

            delay(3000);

        }
        if (input == "stat") {
            Serial.print("current temp vector's size:  ");
            Serial.println(strVector.size());
            read_counter(SPIFFS, FILENAME);
            Serial.print("Percent free:  ");
            Serial.println(percent_full());

            delay(3000);

        }
        if (input == "dedup") {
            forced_writing();
            deduplicate_file_by_rows();
            delay(3000);

        }

    }
    //  uint64_t mac = generateRandomMac();
    //
    //  appendFile(SPIFFS, FILENAME, String(mac).c_str());
}
