#include <stdint.h>

#include "esp_attr.h"
#include "esp_bit_defs.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portmacro.h"

#include "solarnode.h"
#include "wifi.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define MAX_WIFI_RETRIES 5

static EventGroupHandle_t s_wifiEventGroup;
static int s_retryCount = 0;

RTC_DATA_ATTR static uint8_t s_wifiCachedBssid[6];
RTC_DATA_ATTR static uint8_t s_wifiCachedChannel;
RTC_DATA_ATTR static bool s_wifiHasCache = false;

static void wifiEventHandler(void *arg, esp_event_base_t eventBase,
                             int32_t eventId, void *eventData) {
    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
        // connection loop start
        esp_wifi_connect();
    } else if (eventBase == WIFI_EVENT &&
               eventId == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retryCount < MAX_WIFI_RETRIES) {
            esp_wifi_connect();
            s_retryCount++;
            wifi_event_sta_disconnected_t *event =
                (wifi_event_sta_disconnected_t *)eventData;
            ESP_LOGI(TAG, "Disconnected, reason=%d. Retrying... (%d/%d)",
                     event->reason, s_retryCount, MAX_WIFI_RETRIES);
        } else {
            xEventGroupSetBits(s_wifiEventGroup, WIFI_FAIL_BIT);
        }
    } else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)eventData;
        ESP_LOGI(TAG, "Received IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retryCount = 0;
        xEventGroupSetBits(s_wifiEventGroup, WIFI_CONNECTED_BIT);

        // renew cache
        wifi_ap_record_t apInfo;
        esp_wifi_sta_get_ap_info(&apInfo);
        memcpy(s_wifiCachedBssid, apInfo.bssid, 6);
        s_wifiCachedChannel = apInfo.primary;
        s_wifiHasCache = true;
    }
}

SolarNode_Error configureWifi() {
    s_wifiEventGroup = xEventGroupCreate();

    // initialise NVS for Wi-Fi

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) {
            return SOLARNODE_STARTUP_FAILURE;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return SOLARNODE_STARTUP_FAILURE;
    }

    // initialise TCP/IP network stack and Wi-Fi

    if (esp_netif_init() != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;
    if (esp_event_loop_create_default() != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    esp_netif_t *staNetif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&initConfig) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    // event handler setup

    // (instanceAnyId made global)
    esp_event_handler_instance_t instanceGotIp;
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &wifiEventHandler, NULL,
                                            &s_instanceAnyId) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &wifiEventHandler, NULL,
                                            &instanceGotIp) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    // Wi-Fi credentials and mode configuration

    wifi_config_t wifiConfig = {
        .sta = {.ssid = CONFIG_SOLARNODE_WIFI_SSID,
                .password = CONFIG_SOLARNODE_WIFI_PASS,
                .scan_method = WIFI_FAST_SCAN,
                .threshold.authmode = WIFI_AUTH_WPA2_PSK}};

    if (s_wifiHasCache) {
        memcpy(wifiConfig.sta.bssid, s_wifiCachedBssid, 6);
        wifiConfig.sta.bssid_set = true;
        wifiConfig.sta.channel = s_wifiCachedChannel;
        ESP_LOGI(TAG, "Wi-Fi info cache hit");
    }

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;
    if (esp_wifi_set_config(WIFI_IF_STA, &wifiConfig) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    // disable power saving (short lived connection)
    esp_wifi_set_ps(WIFI_PS_NONE);

    // DHCP

    esp_netif_dhcpc_stop(staNetif);

    esp_netif_ip_info_t ipInfo = {
        .ip.addr = esp_ip4addr_aton(CONFIG_SOLARNODE_IP_ADDR),
        .gw.addr = esp_ip4addr_aton(CONFIG_SOLARNODE_GATEWAY),
        .netmask.addr = esp_ip4addr_aton(CONFIG_SOLARNODE_NETMASK)};
    esp_netif_set_ip_info(staNetif, &ipInfo);

    // start Wi-Fi

    if (esp_wifi_start() != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    ESP_LOGI(TAG, "Connecting to %s...", CONFIG_SOLARNODE_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifiEventGroup,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Wi-Fi successfully");
    } else {
        ESP_LOGE(TAG, "Failed to connect to the Wi-Fi network");
        return SOLARNODE_NETWORK_FAILURE;
    }

    return SOLARNODE_SUCCCES;
}

void captureWifiRssi(SolarNode_Data *data) {
    wifi_ap_record_t apInfo;
    if (esp_wifi_sta_get_ap_info(&apInfo) != ESP_OK)
        return;

    data->wifiRssi = apInfo.rssi;
}
