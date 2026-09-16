# SolarNode

SolarNode is a solar-powered weather station project. It runs using an ESP32
board and it integrates with an external server providing a **Prometheus**
metrics endpoint. In addition, the data may be visualised using **Grafana**.

It is designed to be _energy efficient_ and _resilient_ using a fully _3D printed_
frame and enclosure.

## Building from source

### ESP32 Station Firmware

Requires the official
[ESP-IDF SDK](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started)
from Espressif.

Make sure to activate the ESP-IDF environment variables and tools
by running the repository's export.sh script.

Connect the ESP32 via USB to your computer and run:

```bash
cd firmware
idf.py set-target esp32

# set all variables from `SolarNode configuration settings`
idf.py menuconfig

idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Go Server

Requires the standard Go build tools installed. Check out
[the official guide](https://go.dev/dl/)

```bash
cd server
go build -o solarnode-server .
```

## Deployment

Once the firmware and the station have been installed and set up, the server
requires a host device on the LAN, such as a Raspberry Pi or a Mini PC.

The project's [systemd service](./server/solarnode.service) file provides a
default configuration to run the server. You may send the server binary to the device
(via `scp` for example) and enable the service.

In order to collect and visualise metrics, configure Grafana and Prometheus.
I most definitely recommend using Docker.
Minimal examples for the [prometheus.yml](./prometheus.yml) and
[docker-compose.yml](./docker-compose.yml) are provided.

## License

Copyright (c) 2026 Adrian Laurențiu Roșu. All Rights Reserved.

This repository is available for viewing purposes only. See [LICENSE](./LICENSE)
for details.
