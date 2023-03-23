#ifndef DETECTED_DEVICES_H
#define DETECTED_DEVICES_H

#include <string>
#include <map>
#include <Arduino.h>
#include <SPIFFS.h>

class Device {
public:
    std::string mac_address;
    int min_rssi;
    int ping_counter;

    Device(const std::string& mac, int rssi);
};

class DetectedDevices {
public:
    DetectedDevices();
    void insert(const std::string& _mac, int _rssi);
    void print_global();
    void print_temp();
    void save_to_spiffs();

private:
    std::map<std::string, Device> devices;
    std::map<std::string, Device> spiffs_devices;
    int insert_counter;
};

#endif
