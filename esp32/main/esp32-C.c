#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_sntp.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "freertos/event_groups.h"
#include "wifi_config.h"
#include "cJSON.h"
#include "driver/rtc_io.h"

#include "bme680/bme68x.h"

#define SERVER_URL "https://10.184.34.192:5000/sensor"
#define VERSION_URL "https://10.184.34.192:5000/firmware/version"
#define OTA_URL "https://10.184.34.192:5000/firmware/latest"
#define FIRMWARE_VERSION "1.2.0"

#define DEEP_SLEEP_DURATION_SEC 300 // 5 minutes
#define OTA_CHECK_INTERVAL 24       // Check for OTA updates every 24 wake cycles (2 hours)
#define WIFI_TIMEOUT_MS 30000       // 30 seconds wifi connection timeout

#define I2C_MASTER_SCL_IO 5
#define I2C_MASTER_SDA_IO 4
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t wifi_event_group;

#define NVS_NAMESPACE "t_mon"
#define NVS_WAKE_COUNT_KEY "wake_cnt"
#define NVS_LAST_OTA_KEY "last_ota"

static struct bme68x_dev gas_sensor;
static bool wifi_connected = false;

extern const uint8_t cert_pem_start[] asm("_binary_cert_pem_start");

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        printf("Disconnected from WiFi, retrying...\n");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("Got IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_connected = true;
    }
}

void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode,
                       I2C_MASTER_RX_BUF_DISABLE,
                       I2C_MASTER_TX_BUF_DISABLE, 0);
}

int8_t bme_i2c_read(uint8_t reg_addr, uint8_t *data, uint32_t len, void *intf_ptr)
{
    uint8_t device_addr = *(uint8_t *)intf_ptr;
    (void)intf_ptr;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1)
    {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    int8_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK ? BME68X_OK : BME68X_E_COM_FAIL;
}

int8_t bme_i2c_write(uint8_t reg_addr, const uint8_t *data, uint32_t len, void *intf_ptr)
{
    uint8_t device_addr = *(uint8_t *)intf_ptr;
    (void)intf_ptr;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    int8_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK ? BME68X_OK : BME68X_E_COM_FAIL;
}

void user_delay_us(uint32_t period, void *intf_ptr)
{
    (void)intf_ptr;
    vTaskDelay(pdMS_TO_TICKS(period + 999) / 1000);
}

bool wifi_init_and_connect(void)
{
    wifi_event_group = xEventGroupCreate();
    wifi_connected = false;

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    printf("Connecting to WiFi...\n");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(WIFI_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT)
    {
        printf("Connected to WiFi\n");
        return true;
    }
    else
    {
        printf("Failed to connect to WiFi within timeout\n");
        return false;
    }
}

void wifi_cleanup(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_deinit();
    if (wifi_event_group)
    {
        vEventGroupDelete(wifi_event_group);
    }
}

bool read_sensor_data(float *temp, float *humidity, float *pressure, int *gas_resistance)
{
    struct bme68x_conf conf;
    struct bme68x_heatr_conf heatr_conf;
    struct bme68x_data data;
    uint8_t n_fields;
    int8_t rslt;

    conf.os_temp = BME68X_OS_8X;
    conf.os_hum = BME68X_OS_2X;
    conf.os_pres = BME68X_OS_4X;
    conf.filter = BME68X_FILTER_OFF;
    bme68x_set_conf(&conf, &gas_sensor);

    heatr_conf.enable = BME68X_ENABLE_HEATER;
    heatr_conf.heatr_temp = 320;
    heatr_conf.heatr_dur = 150;
    bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &gas_sensor);

    bme68x_set_op_mode(BME68X_FORCED_MODE, &gas_sensor);
    gas_sensor.delay_us(heatr_conf.heatr_dur * 1000, gas_sensor.intf_ptr);

    rslt = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &gas_sensor);
    if (rslt == BME68X_OK && n_fields > 0)
    {
        *temp = data.temperature;
        *humidity = data.humidity;
        *pressure = data.pressure;
        *gas_resistance = data.gas_resistance;
        return true;
    }

    return false;
}

bool send_sensor_data(float temp, float humidity, float pressure, int gas_resistance)
{
    char post_data[256];
    snprintf(post_data, sizeof(post_data),
             "{\"temperature\": %.2f, \"humidity\": %.2f, \"pressure\": %.2f, \"gas_resistance\": %d}",
             temp, humidity, pressure, gas_resistance);

    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .cert_pem = (const char *)cert_pem_start,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    bool success = false;

    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(client);
        printf("HTTP POST Status = %d\n", status_code);
        success = (status_code >= 200 && status_code < 300);
    }
    else
    {
        printf("HTTP POST failed: %s\n", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return success;
}

bool is_new_firmware_available()
{
    esp_http_client_config_t config = {
        .url = VERSION_URL,
        .timeout_ms = 10000,
        .cert_pem = (const char *)cert_pem_start,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        printf("Failed to open HTTP client: %s\n", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0)
    {
        printf("Failed to fetch headers\n");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200)
    {
        printf("HTTP request failed with status code: %d\n", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    char *buf = malloc(content_length + 1);
    if (!buf)
    {
        printf("Failed to allocate memory\n");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int data_read = esp_http_client_read_response(client, buf, content_length);
    if (data_read < 0)
    {
        printf("Failed to read response\n");
        free(buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    buf[data_read] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        printf("Failed to parse JSON\n");
        free(buf);
        return false;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    bool is_new = false;

    if (cJSON_IsString(version) && (version->valuestring != NULL))
    {
        printf("Server version: %s, Current: %s\n", version->valuestring, FIRMWARE_VERSION);
        is_new = (strcmp(FIRMWARE_VERSION, version->valuestring) != 0);
    }

    free(buf);
    cJSON_Delete(root);
    return is_new;
}

bool perform_ota_update()
{
    printf("Starting OTA update...\n");

    esp_http_client_config_t config = {
        .url = OTA_URL,
        .timeout_ms = 30000,
        .cert_pem = (const char *)cert_pem_start,
        .skip_cert_common_name_check = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK)
    {
        printf("OTA update successful, restarting...\n");
        esp_restart();
        return true;
    }
    else
    {
        printf("OTA update failed: %s\n", esp_err_to_name(ret));
        return false;
    }
}

void enter_deep_sleep(void)
{
    printf("Entering deep sleep for %d seconds...\n", DEEP_SLEEP_DURATION_SEC);

    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_SEC * 1000000ULL);

    // rtc_gpio_isolate(GPIO_NUM_12);
    // rtc_gpio_isolate(GPIO_NUM_15);

    esp_deep_sleep_start();
}

uint32_t get_wake_count(void)
{
    nvs_handle_t nvs_handle;
    uint32_t wake_count = 0;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        printf("Failed to open NVS: %s\n", esp_err_to_name(err));
        return wake_count;
    }

    err = nvs_get_u32(nvs_handle, NVS_WAKE_COUNT_KEY, &wake_count);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        printf("Wake count not found, initializing to 0\n");
        wake_count = 0;
    }
    else if (err != ESP_OK)
    {
        printf("Error reading wake count: %s\n", esp_err_to_name(err));
        wake_count = 0;
    }

    printf("Previous wake count: %lu\n", wake_count);

    wake_count++;
    err = nvs_set_u32(nvs_handle, NVS_WAKE_COUNT_KEY, wake_count);
    if (err != ESP_OK)
    {
        printf("Error setting wake count: %s\n", esp_err_to_name(err));
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        printf("Error committing NVS changes: %s\n", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    printf("Updated wake count: %lu\n", wake_count);

    return wake_count;
}

void app_main(void)
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_TIMER:
        printf("Wakeup from timer\n");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
        printf("Cold boot\n");
        break;
    }

    printf("Firmware v%s starting...\n", FIRMWARE_VERSION);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    uint32_t wake_count = get_wake_count();
    printf("Wake count: %lu\n", wake_count);

    i2c_master_init();

    static uint8_t dev_addr = BME68X_I2C_ADDR_HIGH;
    gas_sensor.intf = BME68X_I2C_INTF;
    gas_sensor.intf_ptr = &dev_addr;
    gas_sensor.read = bme_i2c_read;
    gas_sensor.write = bme_i2c_write;
    gas_sensor.delay_us = user_delay_us;
    gas_sensor.amb_temp = 25;

    int8_t rslt = bme68x_init(&gas_sensor);
    if (rslt != BME68X_OK)
    {
        printf("BME68x initialization failed: %d\n", rslt);
        enter_deep_sleep();
        return;
    }

    float temp, humidity, pressure;
    int gas_resistance;

    if (!read_sensor_data(&temp, &humidity, &pressure, &gas_resistance))
    {
        printf("Failed to read sensor data\n");
        enter_deep_sleep();
        return;
    }

    printf("T: %.2f°C, H: %.2f%%, P: %.2fhPa, G: %dΩ\n",
           temp, humidity, pressure, gas_resistance);

    if (!wifi_init_and_connect())
    {
        printf("WiFi connection failed, going to sleep\n");
        wifi_cleanup();
        enter_deep_sleep();
        return;
    }

    bool data_sent = send_sensor_data(temp, humidity, pressure, gas_resistance);
    if (data_sent)
    {
        printf("Data sent successfully\n");
    }
    else
    {
        printf("Failed to send data\n");
    }

    if (wake_count % OTA_CHECK_INTERVAL == 0)
    {
        printf("Checking for OTA update...\n");
        if (is_new_firmware_available())
        {
            perform_ota_update();
        }
        else
        {
            printf("No firmware update available\n");
        }
    }

    wifi_cleanup();
    i2c_driver_delete(I2C_MASTER_NUM);

    enter_deep_sleep();
}