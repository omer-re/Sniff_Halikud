#include "detected_devices.h"

Device::Device(const std::string& mac, int rssi)
    : mac_address(mac), min_rssi(rssi), ping_counter(1) {}

DetectedDevices::DetectedDevices() : insert_counter(0) {
    if (!SPIFFS.begin()) {
        Serial.println("Failed to initialize SPIFFS");
        return;
    }
}

void DetectedDevices::insert(const std::string& _mac, int _rssi) {
    if (_rssi < -90) {
        return;
    }

    auto it = devices.find(_mac);
    if (it != devices.end()) {
        it->second.ping_counter++;
        if (_rssi > it->second.min_rssi) {
            it->second.min_rssi = _rssi;
        }
    } else {
        devices[_mac] = Device(_mac, _rssi);
    }

    insert_counter++;
    if (insert_counter >= 500) {
        save_to_spiffs();
        insert_counter = 0;
    }
}

void DetectedDevices::print_global() {
    Serial.println("Global Devices (SPIFFS):");
    for (const auto& [key, device] : spiffs_devices) {
        Serial.printf("MAC: %s, min_rssi: %d, counter: %d\n",
                      device.mac_address.c_str(), device.min_rssi, device.ping_counter);
    }
}

void DetectedDevices::print_temp() {
    Serial.println("Temporary Devices (Working memory):");
    for (const auto& [key, device] : devices) {
        Serial.printf("MAC: %s, min_rssi: %d, counter: %d\n",
                      device.mac_address.c_str(), device.min_rssi, device.ping_counter);
    }
}

void DetectedDevices::save_to_spiffs() {
    for (const auto& [key, device] : devices) {
        spiffs_devices[key] = device;
    }

    // Save to SPIFFS
    File file = SPIFFS.open("/devices.dat", "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    for (const auto& [key, device] : spiffs_devices) {
        file.printf("MAC: %s, min_rssi: %d, counter: %d\n",
                    device.mac_address.c_str(), device.min_rssi, device.ping_counter);
    }

    file.close();
    Serial.println("Devices saved to SPIFFS");
}
