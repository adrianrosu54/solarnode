#ifndef WIFI_H
#define WIFI_H

#include "esp_event_base.h"

#include "solarnode.h"

extern esp_event_handler_instance_t s_instanceAnyId;

SolarNode_Error configureWifi();

void captureWifiRssi(SolarNode_Data *data);

#endif
