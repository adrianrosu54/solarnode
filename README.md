# SolarNode

SolarNode is a **solar-powered weather station** project. It runs using an _ESP32_
board and integrates with an external hosted server providing a _Prometheus_
metrics endpoint. The resulting data can be visualised using a
custom-made _Grafana dashboard_.

The project is designed to be **energy efficient** and **resilient**, using a fully
_3D printed_ solar panel frame and enclosure assembly.
The core station spends most of its time inside **deep sleep**,
waking up and sending data at regular intervals (every minute, by default).

## Building

The project software has been written on Linux, but all frameworks and libraries
used are cross-platform capable.

### ESP32 Station Firmware

Requires the official
[ESP-IDF SDK](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started)
from _Espressif_.

Make sure to activate the ESP-IDF environment variables and tools
by running the repository's export.sh script.

Connect the ESP32 via USB to your computer and run:

```bash
cd firmware
idf.py set-target esp32

# set all variables from <SolarNode configuration settings>
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
(via `scp`, for example) and enable the service.

### Metrics

In order to collect and visualise metrics, configure Grafana and Prometheus.
I most definitely recommend using Docker.

Minimal examples for the [prometheus.yml](./prometheus.yml) and
[docker-compose.yml](./docker-compose.yml) are provided.

Once set up, import the project's custom [Grafana dashboard](./grafana-dashboard.json)
to display relevant station data.

## License

Copyright (c) 2026 Adrian Laurențiu Roșu. All Rights Reserved.

This repository is available for viewing purposes only. See [LICENSE](./LICENSE)
for details.
