#include <string.h>

#include "bmp280.h"
#include "cJSON.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/adc_types.h"
#include "i2cdev.h"
#include "sdkconfig.h"

#include "http.h"
#include "solarnode.h"
#include "wifi.h"

#define I2C_PORT 0
#define I2C_MASTER_SDA 21
#define I2C_MaSTER_SCL 22

#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_0

esp_event_handler_instance_t s_instanceAnyId;

static SolarNode_Error readBmp280(SolarNode_Data *data) {
    if (i2cdev_init() != ESP_OK) {
        ESP_LOGE(TAG, "Couldn't start I2C comms");
        return SOLARNODE_STARTUP_FAILURE;
    }

    bmp280_params_t params;
    bmp280_init_default_params(&params);

    bmp280_t dev;
    memset(&dev, 0, sizeof(bmp280_t));

    if (bmp280_init_desc(&dev, BMP280_I2C_ADDRESS_0, I2C_PORT, I2C_MASTER_SDA,
                         I2C_MaSTER_SCL) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;
    if (bmp280_init(&dev, &params) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;
    // ESP_LOGI(TAG, "BME280: found");

    vTaskDelay(pdMS_TO_TICKS(90));

    if (bmp280_read_float(&dev, &data->temperature, &data->pressure,
                          &data->humidity) != ESP_OK) {
        ESP_LOGE(TAG, "Temp and pressure reading failed");
        return SOLARNODE_READ_FAILURE;
    }

    ESP_LOGD(TAG, "Pressure: %.2f, Temp: %.2f, Humidity: %.2f", data->pressure,
             data->temperature, data->humidity);

    bmp280_free_desc(&dev);

    return SOLARNODE_SUCCCES;
}

static SolarNode_Error readBattery(SolarNode_Data *data) {
    adc_oneshot_unit_handle_t adcHandle;
    adc_oneshot_unit_init_cfg_t initConfig = {.unit_id = ADC_UNIT};

    if (adc_oneshot_new_unit(&initConfig, &adcHandle) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    adc_oneshot_chan_cfg_t config = {.bitwidth = ADC_BITWIDTH_DEFAULT,
                                     .atten = ADC_ATTEN_DB_12};

    if (adc_oneshot_config_channel(adcHandle, ADC_CHANNEL, &config) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    adc_cali_handle_t caliHandle = NULL;
    adc_cali_line_fitting_config_t caliConfig = {.unit_id = ADC_UNIT,
                                                 .bitwidth =
                                                     ADC_BITWIDTH_DEFAULT,
                                                 .atten = ADC_ATTEN_DB_12};

    if (adc_cali_create_scheme_line_fitting(&caliConfig, &caliHandle) != ESP_OK)
        return SOLARNODE_STARTUP_FAILURE;

    int adcValue = 0;
    int voltageMv = 0;

    if (adc_oneshot_read(adcHandle, ADC_CHANNEL, &adcValue) != ESP_OK) {
        ESP_LOGE(TAG, "ADC battery voltage reading failed");
        return SOLARNODE_READ_FAILURE;
    }

    adc_cali_raw_to_voltage(caliHandle, adcValue, &voltageMv);

    // double to calculate read voltage using the voltage divider
    voltageMv *= 2;

    ESP_LOGD(TAG, "ADC raw value: %d, Voltage value: %d mV", adcValue,
             voltageMv);

    data->batteryVoltage = (float)voltageMv / 1000.0f;

    return SOLARNODE_SUCCCES;
}

static SolarNode_Error networkSendReading(SolarNode_Data *data) {
    ESP_LOGD(TAG,
             "Final Readings:\t"
             "Temperature: %.2f C, Pressure: %.2f Pa, Humidity: %.2f %%RH, "
             "Battery: %.2f mV",
             data->temperature, data->pressure, data->humidity,
             data->batteryVoltage);

    // establish Wi-Fi connection

    SolarNode_Error errorWifi = configureWifi();
    if (errorWifi != SOLARNODE_SUCCCES) {
        ESP_LOGE(TAG, "Wi-Fi setup has failed");
        return SOLARNODE_NETWORK_FAILURE;
    }

    captureWifiRssi(data);

    // create and send JSON post request

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temperature", data->temperature);
    cJSON_AddNumberToObject(root, "pressure", data->pressure);
    cJSON_AddNumberToObject(root, "humidity", data->humidity);
    cJSON_AddNumberToObject(root, "battery_voltage", data->batteryVoltage);
    cJSON_AddNumberToObject(root, "wifi_rssi", data->wifiRssi);

    char *json = cJSON_PrintUnformatted(root);

    ESP_LOGD(TAG, "JSON payload: %.*s", strlen(json), json);

    httpPostRequest(json);

    free(json);
    cJSON_Delete(root);

    // cleanup Wi-Fi

    esp_event_handler_instance_unregister(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        s_instanceAnyId); // remove unwanted disconnect retries
    esp_wifi_disconnect();
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));

    return SOLARNODE_SUCCCES;
}

void app_main(void) {
    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
    SolarNode_Data data;

    switch (wakeupReason) {
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

    esp_sleep_enable_timer_wakeup(CONFIG_SOLARNODE_WAKEUP_TIME_SEC *
                                  1000000ULL);

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds",
             CONFIG_SOLARNODE_WAKEUP_TIME_SEC);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_deep_sleep_start();
}
