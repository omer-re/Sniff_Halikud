#include <Arduino.h>
#include "detected_devices.h"

DetectedDevices detected_devices;

// Function to simulate inserting devices with random RSSI
void insert_random_devices() {
    std::string mac_addresses[] = {
        "AA:BB:CC:00:00:01",
        "AA:BB:CC:00:00:02",
        "AA:BB:CC:00:00:03",
        "AA:BB:CC:00:00:04",
        "AA:BB:CC:00:00:05",
    };

    for (const auto& mac : mac_addresses) {
        int rssi = random(-100, -40);
        detected_devices.insert(mac, rssi);
    }
}

void setup() {
    Serial.begin(115200);

    xTaskCreatePinnedToCore(
        [] (void* parameter) {
            for (;;) {
                insert_random_devices();
                delay(1000);
            }
        },
        "InsertTask",
        4096,
        nullptr,
        1,
        nullptr,
        0 // Run on core 0
    );

    xTaskCreatePinnedToCore(
        [] (void* parameter) {
            for (;;) {
                Serial.println("Temporary devices:");
                detected_devices.print_temp();
                delay(5000);
            }
        },
        "PrintTempTask",
        4096,
        nullptr,
        1,
        nullptr,
        0 // Run on core 0
    );

xTaskCreatePinnedToCore(
    [] (void* parameter) {
        for (;;) {
            Serial.println("Global devices:");
            detected_devices.print_global();
            delay(10000);
        }
    },
    "PrintGlobalTask",
    4096,
    nullptr,
    1,
    nullptr,
    0 // Run on core 0
);
}

void loop() {
    // The main loop is empty since all tasks are managed by FreeRTOS
}
