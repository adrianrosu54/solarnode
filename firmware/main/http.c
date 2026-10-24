#include "esp_http_client.h"
#include "esp_log.h"

#include "solarnode.h"

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

void httpPostRequest(const char *jsonPayload) {
    esp_http_client_config_t config = {.url = CONFIG_SOLARNODE_SERVER_URL,
                                       .event_handler = httpEventHandler,
                                       .method = HTTP_METHOD_POST,
                                       .timeout_ms = 5000,
                                       .transport_type =
                                           HTTP_TRANSPORT_OVER_TCP};

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, jsonPayload, strlen(jsonPayload));

    esp_err_t error = esp_http_client_perform(client);

    if (error == ESP_OK) {
        ESP_LOGI(TAG, "POST request status: %d, Content Length: %" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG,
                 "POST request to " CONFIG_SOLARNODE_SERVER_URL " failed: %s",
                 esp_err_to_name(error));
    }

    esp_http_client_cleanup(client);
}
