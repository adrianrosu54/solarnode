#ifndef SOLARNODE_H
#define SOLARNODE_H

typedef struct {
    float temperature;
    float pressure;
    float humidity;
    float batteryVoltage;
    int wifiRssi;
} SolarNode_Data;

typedef enum {
    SOLARNODE_SUCCCES,
    SOLARNODE_READ_FAILURE,
    SOLARNODE_STARTUP_FAILURE,
    SOLARNODE_NETWORK_FAILURE
} SolarNode_Error;

static const char *TAG = "SolarNode main";

#endif
