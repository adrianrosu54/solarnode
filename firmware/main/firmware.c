#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bmp280.h"
#include "cJSON.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_bit_defs.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/adc_types.h"
#include "i2cdev.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portmacro.h"

#define I2C_MASTER_SDA 21
#define I2C_MaSTER_SCL 22

#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_0

#define WAKEUP_TIME_SEC 10

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define MAX_WIFI_RETRIES 5

typedef struct {
    float temperature;
    float pressure;
    float humidity;
    float battery_voltage;
} SolarNode_Data;

typedef enum {
    SOLARNODE_SUCCCES,
    SOLARNODE_READ_FAILURE,
    SOLARNODE_STARTUP_FAILURE,
    SOLARNODE_NETWORK_FAILURE
} SolarNode_Error;

static const char *TAG = "SolarNode main";

static EventGroupHandle_t s_wifiEventGroup;
static int s_retryCount = 0;

static SolarNode_Error readBmp280(SolarNode_Data *data) {
    if (i2cdev_init() != ESP_OK) {
        ESP_LOGW(TAG, "Couldn't start I2C comms");
        return SOLARNODE_STARTUP_FAILURE;
    }

    bmp280_params_t params;
    bmp280_init_default_params(&params);

    bmp280_t dev;
    memset(&dev, 0, sizeof(bmp280_t));

    if (bmp280_init_desc(&dev, BMP280_I2C_ADDRESS_0, 0, I2C_MASTER_SDA,
                         I2C_MaSTER_SCL) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;
    if (bmp280_init(&dev, &params) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;
    // ESP_LOGI(TAG, "BME280: found");

    vTaskDelay(pdMS_TO_TICKS(90));

    if (bmp280_read_float(&dev, &data->temperature, &data->pressure,
                          &data->humidity) != ESP_OK) {
        ESP_LOGW(TAG, "Temp and pressure reading failed");
        return SOLARNODE_READ_FAILURE;
    }

    ESP_LOGI(TAG, "Pressure: %.2f, Temp: %.2f, Humidity: %.2f", data->pressure,
             data->temperature, data->humidity);

    bmp280_free_desc(&dev);

    return SOLARNODE_SUCCCES;
}

static SolarNode_Error readBattery(SolarNode_Data *data) {
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {.unit_id = ADC_UNIT};

    if (adc_oneshot_new_unit(&init_config, &adc_handle) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    adc_oneshot_chan_cfg_t config = {.bitwidth = ADC_BITWIDTH_DEFAULT,
                                     .atten = ADC_ATTEN_DB_12};

    if (adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    adc_cali_handle_t cali_handle = NULL;
    adc_cali_line_fitting_config_t cali_config = {.unit_id = ADC_UNIT,
                                                  .bitwidth =
                                                      ADC_BITWIDTH_DEFAULT,
                                                  .atten = ADC_ATTEN_DB_12};

    if (adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle) !=
        ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    int adcValue = 0;
    int voltageMv = 0;

    if (adc_oneshot_read(adc_handle, ADC_CHANNEL, &adcValue) != ESP_OK) {
        ESP_LOGW(TAG, "ADC battery voltage reading failed\n");
        return SOLARNODE_READ_FAILURE;
    }

    adc_cali_raw_to_voltage(cali_handle, adcValue, &voltageMv);

    // double to calculate read voltage using the voltage divider
    voltageMv *= 2;

    ESP_LOGI(TAG, "ADC raw value: %d, Voltage value: %d mV", adcValue,
             voltageMv);

    data->battery_voltage = (float)voltageMv / 1000;

    return SOLARNODE_SUCCCES;
}

static void wifiEventHandler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // connection loop start
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retryCount < MAX_WIFI_RETRIES) {
            esp_wifi_connect();
            s_retryCount++;
            ESP_LOGI(TAG, "Disconnected. Retrying... (%d/%d)", s_retryCount,
                     MAX_WIFI_RETRIES);
        } else {
            xEventGroupSetBits(s_wifiEventGroup, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Received IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retryCount = 0;
        xEventGroupSetBits(s_wifiEventGroup, WIFI_CONNECTED_BIT);
    }
}

static SolarNode_Error configureWifi() {
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

    // esp_netif_t *sta_netif =
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    // event handler setup

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &wifiEventHandler, NULL,
                                            &instance_any_id) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &wifiEventHandler, NULL,
                                            &instance_got_ip) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    // Wi-Fi credentials and mode configuration

    wifi_config_t wifi_config = {
        .sta = {.ssid = SOLARNODE_WIFI_SSID,
                .password = SOLARNODE_WIFI_PASS,
                .threshold.authmode = WIFI_AUTH_WPA2_PSK}};

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    if (esp_wifi_start() != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    ESP_LOGI(TAG, "Connecting to %s...", SOLARNODE_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifiEventGroup,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected successfully");
    } else {
        ESP_LOGE(TAG, "Failed to connect to the Wi-Fi network");
        return SOLARNODE_NETWORK_FAILURE;
    }

    return SOLARNODE_SUCCCES;
}

static esp_err_t httpEventHandler(esp_http_client_event_t *event) {
    switch (event->event_id) {
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "Response chunk: %.*s", event->data_len,
                 (char *)event->data);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void httpPostRequest(const char *jsonPayload) {
    esp_http_client_config_t config = {.url = SOLARNODE_SERVER_DATA_URL,
                                       .event_handler = httpEventHandler,
                                       .method = HTTP_METHOD_POST,
                                       .timeout_ms = 5000};

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, jsonPayload, strlen(jsonPayload));

    esp_err_t error = esp_http_client_perform(client);

    if (error == ESP_OK) {
        ESP_LOGI(TAG, "POST status: %d, Content Length: %" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "POST request failed: %s", esp_err_to_name(error));
    }

    esp_http_client_cleanup(client);
}

static SolarNode_Error networkSendReading(SolarNode_Data *data) {
    ESP_LOGD(TAG,
             "Final Readings:\t"
             "Temperature: %.2f C, Pressure: %.2f Pa, Humidity: %.2f %%RH, "
             "Battery: %.2f mV",
             data->temperature, data->pressure, data->humidity,
             data->battery_voltage);

    // establish Wi-Fi connection

    SolarNode_Error errorWifi = configureWifi();
    if (errorWifi != SOLARNODE_SUCCCES) {
        ESP_LOGE(TAG, "Wi-Fi setup has failed");
        return SOLARNODE_NETWORK_FAILURE;
    }

    // create and send JSON post request

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temperature", data->temperature);
    cJSON_AddNumberToObject(root, "pressure", data->pressure);
    cJSON_AddNumberToObject(root, "humidity", data->humidity);
    cJSON_AddNumberToObject(root, "battery_voltage", data->battery_voltage);

    char *json = cJSON_PrintUnformatted(root);

    ESP_LOGD(TAG, "JSON payload: %.*s", strlen(json), json);

    httpPostRequest(json);

    free(json);
    cJSON_Delete(root);

    return SOLARNODE_SUCCCES;
}

void app_main(void) {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    SolarNode_Data data;

    switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_TIMER:
        ESP_LOGI(TAG, "Timer wakeup");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        ESP_LOGI(TAG, "First boot or power-on reset wakeup");
        break;
    }

    SolarNode_Error errorTemp = readBmp280(&data);
    SolarNode_Error errorBattery = readBattery(&data);

    if (errorTemp == SOLARNODE_SUCCCES && errorBattery == SOLARNODE_SUCCCES) {
        networkSendReading(&data);
    } else {
        ESP_LOGW(TAG, "Sensors failed, data won't be transmitted.");
    }

    esp_sleep_enable_timer_wakeup(WAKEUP_TIME_SEC * 1000000ULL);

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", WAKEUP_TIME_SEC);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_deep_sleep_start();
}
