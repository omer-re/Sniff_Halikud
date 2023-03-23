/* prompt:

esp32 cpp spiffs, if there is less than 20% free memory read file name "source.txt", remove all duplicated rows in it, save to a new file "source2.txt", if writing new file succeeded then remove "source.txt", check if there is less than 20% free memory, if so- set variable uniquely_full=true;
**/

#include <SPIFFS.h>

bool uniquely_full = false;

void setup() {
  // Mount SPIFFS file system
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount SPIFFS file system");
    return;
  }

  // Check if there is less than 20% free memory in SPIFFS
  float totalBytes = float(SPIFFS.totalBytes());
  float usedBytes = float(SPIFFS.usedBytes());
  float freeBytes = totalBytes - usedBytes;
  float freePercentage = (freeBytes / totalBytes) * 100;
  if (freePercentage < 20.0) {
    Serial.println("Less than 20% free memory in SPIFFS");
    // Set the flag to true if there is less than 20% free memory
    uniquely_full = true;
  } else {
    Serial.println("More than 20% free memory in SPIFFS");
  }

  if (!uniquely_full) {
    // Open the input file "source.txt" for reading
    File inputFile = SPIFFS.open("/source.txt", FILE_READ);
    if (!inputFile) {
      Serial.println("Failed to open input file");
      return;
    }

    // Open the output file "source2.txt" for writing
    File outputFile = SPIFFS.open("/source2.txt", FILE_WRITE);
    if (!outputFile) {
      Serial.println("Failed to open output file");
      inputFile.close();
      return;
    }

    // Create a set to store unique rows
    std::set<String> uniqueRows;

    // Read input file and remove duplicates
    while (inputFile.available()) {
      String row = inputFile.readStringUntil('\n');
      if (row.length() > 0) {
        uniqueRows.insert(row);
      }
    }

    // Write unique rows to output file
    for (const auto& row : uniqueRows) {
      outputFile.println(row);
    }

    // Close input and output files
    inputFile.close();
    outputFile.close();

    // Remove the input file if the write to the output file was successful
    if (outputFile) {
      SPIFFS.remove("/source.txt");
    }

    // Check if there is less than 20% free memory in SPIFFS again
    totalBytes = float(SPIFFS.totalBytes());
    usedBytes = float(SPIFFS.usedBytes());
    freeBytes = totalBytes - usedBytes;
    freePercentage = (freeBytes / totalBytes) * 100;
    if (freePercentage < 20.0) {
      Serial.println("Less than 20% free memory in SPIFFS");
      // Set the flag to true if there is less than 20% free memory
      uniquely_full = true;
    } else {
      Serial.println("More than 20% free memory in SPIFFS");
    }
  }
}

void loop() {
  // Do nothing in the loop
}



