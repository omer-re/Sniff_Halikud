#include <Arduino.h>
#include <SPIFFS.h>
#include <vector>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <string>
#include <FS.h>
#include <Ticker.h>

#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"


#define BATCH_SIZE 200
#define FIRST_CSV_WAYPOINT 15000
#define CSV_DEDUP_INTERVAL 1000
#define WIFI_CHANNEL_SWITCH_INTERVAL  (500)
#define WIFI_CHANNEL_MAX               (13)
#define LED_BUILTIN 2


char* FILENAME = "/crowd.csv";
std::vector<String> row;

std::vector<std::vector<String>> temp_matrix;
unsigned int deduplication_counter = 0;
int dup_remover_calls = 0;
int counter=0;
Ticker led_ticker;

static esp_err_t event_handler(void *ctx, system_event_t *event);

static void wifi_sniffer_init(void);

static void wifi_sniffer_set_channel(uint8_t channel);

static const char *wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type);

static void wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type);
//void receive_mac_addresses() {
//    String input = Serial.readStringUntil('\n');
//    input.trim();
//
//    char mac1[18], mac2[18], mac3[18];
//    int count = sscanf(input.c_str(), "%17[^,],%17[^,],%17s", mac1, mac2, mac3);
//
//    if (count == 3) {
//        add_mac_addresses(mac1, mac2, mac3);
//    }
//}

//void add_mac_addresses(const String &mac1, const String &mac2, const String &mac3) {
void add_mac_addresses() {
    temp_matrix.push_back(row);
}

//void receive_mac_addresses() {
//    String input = Serial.readStringUntil('\n');
//    input.trim();
//
//    String mac1, mac2, mac3;
//    int pos1 = input.indexOf(",");
//    int pos2 = input.indexOf(",", pos1 + 1);
//
//    if (pos1 != -1 && pos2 != -1) {
//        mac1 = input.substring(0, pos1);
//        mac2 = input.substring(pos1 + 1, pos2);
//        mac3 = input.substring(pos2 + 1);
//
//        std::vector<String> row = {mac1, mac2, mac3};
//        temp_list.push_back(row);
//    }
//}

int count_rows(const char* filename) {
    File file = SPIFFS.open(filename, "r");
    int count = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() > 0) count++;
    }
    file.close();
    return count;
}

void remove_duplicates(std::vector<std::vector<String>> &matrix) {
    sort(matrix.begin(), matrix.end());
    matrix.erase(unique(matrix.begin(), matrix.end()), matrix.end());
}

void append_to_csv(const std::vector<std::vector<String>> &matrix, const char* filename) {
    File file = SPIFFS.open(filename, "a");
    for (const std::vector<String> &row : matrix) {
        for (size_t i = 0; i < row.size(); ++i) {
            file.print(row[i]);
            if (i < row.size() - 1) {
                file.print(",");
            }
        }
        file.println();
    }
    file.close();
    temp_matrix.clear();

    check_if_deduplication_needed();
}

std::vector<String> parse_csv_line(const String &line) {
    String field;
    for (char c : line) {
        if (c == ',') {
            row.push_back(field);
            field = "";
        } else {
            field += c;
        }
    }
    row.push_back(field);
    return row;
}

String join_csv_line(const std::vector<String> &row) {
    String line = "";
    for (size_t i = 0; i < row.size(); i++) {
        line += row[i];
        if (i < row.size() - 1) {
            line += ",";
        }
    }
    return line;
}

void deduplicate_csv(const char* filename) {
    std::vector<std::vector<String>> matrix;

    // Read the CSV file and populate the matrix
    File file = SPIFFS.open(filename, "r");
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        row = parse_csv_line(line);
        matrix.push_back(row);
    }
    file.close();

    // Remove duplicates from the matrix
    remove_duplicates(matrix);

    // Overwrite the CSV file with the deduplicated matrix
    File new_file = SPIFFS.open(filename, "w");
    for (const auto &row : matrix) {
        String line = join_csv_line(row);
        new_file.println(line);
    }
    new_file.close();
}


void check_if_deduplication_needed() {
    int row_count = count_rows(FILENAME);
    if ((row_count >= FIRST_CSV_WAYPOINT && deduplication_counter == 0) ||
        (row_count >= (FIRST_CSV_WAYPOINT + deduplication_counter * CSV_DEDUP_INTERVAL))) {
        deduplicate_csv(FILENAME);
        deduplication_counter++;
    }
}

///////////////// wifi sniffer /////////////////////



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

//    char arr1[32];
//    char arr2[32];
//    char arr3[32];
        char macs[3][32];
        snprintf(macs[0], sizeof(macs[0]), "%02x%02x%02x%02x%02x%02x", hdr->addr1[0], hdr->addr1[1], hdr->addr1[2], hdr->addr1[3], hdr->addr1[4], hdr->addr1[5]);
        snprintf(macs[1], sizeof(macs[1]), "%02x%02x%02x%02x%02x%02x", hdr->addr2[0], hdr->addr2[1], hdr->addr2[2], hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
        snprintf(macs[2], sizeof(macs[2]), "%02x%02x%02x%02x%02x%02x", hdr->addr3[0], hdr->addr3[1], hdr->addr3[2], hdr->addr3[3], hdr->addr3[4], hdr->addr3[5]);
//        if ((macs[0]) == "ffffffffffff") {
//            //    Serial.println(String(macs[0]));
//            macs[0]=NULL;
////            strVector.push_back(String(macs[0]));
//        }
//        if ((macs[1]) == "ffffffffffff") {
//            //    Serial.println(String(macs[1]));
//            macs[1]=NULL;
////            strVector.push_back(String(macs[1]));
//        }
//        if ((macs[2]) != "ffffffffffff") {
//            macs[2]=NULL;
//            //    Serial.println(String(macs[2]));
//        }
        row = {(macs[0]), (macs[1]), (macs[2])};
        add_mac_addresses();
    }


// filesystem realted functions

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


    int forced_writing() {
        digitalWrite(LED_BUILTIN, HIGH);
        led_ticker.detach();

        remove_duplicates(temp_matrix);
        if (temp_matrix.size() >= BATCH_SIZE) {
            append_to_csv(temp_matrix, FILENAME);
        }

        led_ticker.attach(1, [](){ digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); });

    }


    /////////////////////   SETUP & LOOP


    void setup() {
        Serial.begin(115200);
        pinMode(LED_BUILTIN, OUTPUT);

        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS Mount Failed");
            led_ticker.attach_ms(100, [](){ digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); });
            return;
        }

        if (SPIFFS.exists(FILENAME)) {
            int num_rows = count_rows(FILENAME);
            Serial.printf("crowd.csv exists with %d rows\n", num_rows);
        } else {
            File crowd_file = SPIFFS.open(FILENAME, "w");
            crowd_file.close();
            Serial.println("crowd.csv created");
        }

        led_ticker.attach(1, [](){ digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); });
    }

    void loop() {

        if (Serial.available() > 0) {
            String input = Serial.readStringUntil('\n');

            if (input == "size") {

                read_counter(SPIFFS, FILENAME);
                delay(1000);

            }

            if (input == "dir") {
                // Print all entries
                listdir();
                delay(5000);

            }


            if (input == "fw") {
                forced_writing();
                delay(3000);

            }

            if (input == "vecs") {
                // Print current temp vector's size
                Serial.print("current temp vector's size:  "); Serial.println(temp_matrix.size());
                delay(3000);

            }

            if (input == "mat") {
                // Print current temp vector's size
                for


                Serial.print("current temp vector's size:  "); Serial.println(temp_matrix.size());
                delay(3000);

            }

            if (input == "plot") {
                // Print all entries
                readFile(SPIFFS, FILENAME);
                read_counter(SPIFFS, FILENAME);

                delay(5000);

            }
        }


        if (temp_matrix.size() >= BATCH_SIZE) {
            digitalWrite(LED_BUILTIN, HIGH);
            led_ticker.detach();

            remove_duplicates(temp_matrix);
            if (temp_matrix.size() >= BATCH_SIZE) {
                append_to_csv(temp_matrix, FILENAME);
            }

            led_ticker.attach(1, [](){ digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); });
        }



        delay(1000);
    }
