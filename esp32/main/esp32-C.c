#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"

#define WIFI_SSID "Privat-Megleren"
#define WIFI_PASS "Albretsen-666"
#define SERVER_URL "http://192.168.1.11:5000/hello"

// static const char *TAG = "HELLO_SENDER";

void wifi_init_sta(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
    // ESP_LOGI(TAG, "Connecting to %s...", WIFI_SSID);
    printf("Connecting to %s...\n", WIFI_SSID);
}

void send_hello_task(void *pvParameters)
{
    while (1)
    {
        esp_http_client_config_t config = {
            .url = SERVER_URL,
            .method = HTTP_METHOD_POST,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);

        const char *post_data = "Hello, World from ESP32!";
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
        esp_http_client_set_header(client, "Content-Type", "text/plain");

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK)
        {
            printf("HTTP POST Status = %d, content_length = %lld\n",
                   esp_http_client_get_status_code(client),
                   esp_http_client_get_content_length(client));
            // ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
            //  esp_http_client_get_status_code(client),
            //  esp_http_client_get_content_length(client));
        }
        else
        {
            printf("HTTP POST failed: %s\n", esp_err_to_name(err));
            // ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_sta();
    xTaskCreate(&send_hello_task, "send_hello_task", 8192, NULL, 5, NULL);
}
