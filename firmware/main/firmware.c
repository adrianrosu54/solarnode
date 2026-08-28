#include <stdio.h>
#include <string.h>

#include "bmp280.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "i2cdev.h"

#define I2C_MASTER_SDA 21
#define I2C_MaSTER_SCL 22

static const char *TAG = "SolarNode main";

typedef struct {
    float temperature;
    float pressure;
    float humidity;
} SolarNode_Data;

static void sensorRead_bmp280(SolarNode_Data *restrict data) {
    ESP_ERROR_CHECK(i2cdev_init());

    bmp280_params_t params;
    bmp280_init_default_params(&params);

    bmp280_t dev;
    memset(&dev, 0, sizeof(bmp280_t));

    ESP_ERROR_CHECK(bmp280_init_desc(&dev, BMP280_I2C_ADDRESS_0, 0,
                                     I2C_MASTER_SDA, I2C_MaSTER_SCL));
    ESP_ERROR_CHECK(bmp280_init(&dev, &params));

    ESP_LOGI(TAG, "BME280: found");

    vTaskDelay(pdMS_TO_TICKS(90));
    if (bmp280_read_float(&dev, &data->temperature, &data->pressure,
                          &data->humidity) != ESP_OK) {
        ESP_LOGW(TAG, "Temperature/pressure reading failed\n");
        return;
    }

    printf("Pressure: %.2f Pa, Temperature: %.2f C, Humidity: %.2f\n",
           data->pressure, data->temperature, data->humidity);

    ESP_ERROR_CHECK(bmp280_free_desc(&dev));
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

    sensorRead_bmp280(&data);

    esp_sleep_enable_timer_wakeup(WAKEUP_TIME_SEC * 1000000ULL);

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", WAKEUP_TIME_SEC);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_deep_sleep_start();
}
