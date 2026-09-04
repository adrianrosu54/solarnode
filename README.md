# SolarNode

SolarNode is a solar-powered weather station project. It runs using an ESP32
board and it integrates with an external server providing a **Prometheus**
metrics endpoint. It is designed to be _energy efficient_ and _resilient_.

## Build

### ESP32 Firmware

Requires the official
[ESP-IDF SDK](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started)
from Espressif.

Make sure to activate the ESP-IDF environment variables and tools
by running the repository's export.sh script.

Connect the ESP32 via USB to your computer and run:

```bash
idf.py set-target esp32

# set all variables from SolarNode configuration settings
idf.py menuconfig

idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Go Server

Requires the standard Go build tools installed. Check out
[the official guide](https://go.dev/dl/)

```bash
cd server
go build
```

## License

Copyright (c) 2026 Adrian Laurențiu Roșu. All Rights Reserved.

This repository is available for viewing purposes only. See [LICENSE](./LICENSE)
for details.
