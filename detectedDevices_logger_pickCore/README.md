# Basic requirements:
implement the requirements as an .h file and show an example of using it from esp32's main.
the program should run on core 0 of the esp32.
esp32 data structure name "detected_devices" that has items of "device".
device class should include:
string mac_adddress,
int min_rssi,
int ping_counter.
detected_devices holds a unique item for each MAC address.
it should have an insert function that takes string _mac, and int _rssi, if this _mac already exists- increment counter by 1, if the _rssi>min_rssi then min_rssi=_rssi.
if such MAC doesn't exist- create it.
if rssi is lower than (-90) ignore that device.


detected_devices should run on the esp32's work memory, and every 500 insertions it should update a copy of it to the non volatile memory using spiffs.
make a function called "print_global" that prints for all items from the spiffs, their MAC, min_rssi and counter to the serial terminal.
a function called "print_temp" that prints for all items from the the working memory, their MAC, min_rssi and counter to the serial terminal.

add an ability to determine whether the program runs on the first or second core of the esp32 processor