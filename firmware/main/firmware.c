#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "portmacro.h"

void app_main(void) {
    char secs = 0;

    while (1) {
        printf("Hello! It's been %d seconds\n", secs);
        secs++;

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
