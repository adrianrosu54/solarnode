#include <stdio.h>
#include <string.h>

#include "bmp280.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "i2cdev.h"
#include "sdkconfig.h"
#include "soc/soc.h"

#define I2C_MASTER_SDA 21
#define I2C_MaSTER_SCL 22

void bmp280_test(void *pvParameters) {
    bmp280_params_t params;
    bmp280_init_default_params(&params);

    bmp280_t dev;
    memset(&dev, 0, sizeof(bmp280_t));

    ESP_ERROR_CHECK(bmp280_init_desc(&dev, BMP280_I2C_ADDRESS_0, 0,
                                     I2C_MASTER_SDA, I2C_MaSTER_SCL));
    ESP_ERROR_CHECK(bmp280_init(&dev, &params));

    bool bme280p = dev.id == BME280_CHIP_ID;
    printf("BME280: found %s\n", bme280p ? "BME280" : "BMP280");

    float temperature, pressure, humidity;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (bmp280_read_float(&dev, &temperature, &pressure, &humidity) !=
            ESP_OK) {
            printf("Temperature/pressure reading failed\n");
            continue;
        }

        printf("Pressure: %.2f Pa, Temperature: %.2f C", pressure, temperature);
        if (bme280p) {
            printf(", Humidity: %.2f\n", humidity);
        } else {
            printf("\n");
        }
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(i2cdev_init());
    xTaskCreatePinnedToCore(bmp280_test, "bmp280_test",
                            CONFIG_ESP_MAIN_TASK_STACK_SIZE, NULL, 5, NULL,
                            APP_CPU_NUM);
}
