#include <stdio.h>
#include <string.h>

#include "bmp280.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/adc_types.h"
#include "i2cdev.h"

#define I2C_MASTER_SDA 21
#define I2C_MaSTER_SCL 22

#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_0

#define WAKEUP_TIME_SEC 10

static const char *TAG = "SolarNode main";

typedef struct {
    float temperature;
    float pressure;
    float humidity;
    int battery_voltage;
} SolarNode_Data;

typedef enum {
    SOLARNODE_SUCCCES,
    SOLARNODE_READ_FAILURE,
    SOLARNODE_STARTUP_FAILURE
} SolarNode_Error;

static SolarNode_Error SolarNode_transmit(SolarNode_Data *data) {
    printf("Final Readings:\n"
           "Temperature: %.2f C, Pressure: %.2f Pa, Humidity: %.2f %%RH, "
           "Battery: %d mV\n",
           data->temperature, data->pressure, data->humidity,
           data->battery_voltage);

    return SOLARNODE_SUCCCES;
}

static SolarNode_Error sensorRead_battery(SolarNode_Data *data) {
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

    data->battery_voltage = voltageMv;

    return SOLARNODE_SUCCCES;
}

static SolarNode_Error sensorRead_bmp280(SolarNode_Data *data) {
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

    SolarNode_Error errTemp = sensorRead_bmp280(&data);
    SolarNode_Error errBat = sensorRead_battery(&data);

    if (errTemp == SOLARNODE_SUCCCES && errBat == SOLARNODE_SUCCCES) {
        SolarNode_transmit(&data);
    } else {
        ESP_LOGW(TAG, "Sensors failed, data won't be transmitted.");
    }

    esp_sleep_enable_timer_wakeup(WAKEUP_TIME_SEC * 1000000ULL);

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", WAKEUP_TIME_SEC);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_deep_sleep_start();
}
