#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"

#include "bme680/bme68x.h"

#define WIFI_SSID "Privat-Megleren"
#define WIFI_PASS "Albretsen-666"
#define SERVER_URL "http://192.168.1.11:5000/sensor"

#define I2C_MASTER_SCL_IO 5
#define I2C_MASTER_SDA_IO 4
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

static struct bme68x_dev gas_sensor;

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
    printf("Connecting to %s...\n", WIFI_SSID);
}

void send_temperature_task(void *pvParameters)
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

    while (1)
    {
        bme68x_set_op_mode(BME68X_FORCED_MODE, &gas_sensor);
        gas_sensor.delay_us(heatr_conf.heatr_dur * 1000, gas_sensor.intf_ptr);

        rslt = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &gas_sensor);
        if (rslt == BME68X_OK && n_fields > 0)
        {
            float temp = data.temperature;
            printf("Temperature: %.2f °C\n", temp);

            char post_data[64];
            snprintf(post_data, sizeof(post_data), "{\"temperature\": %.2f}", temp);

            esp_http_client_config_t config = {
                .url = SERVER_URL,
                .method = HTTP_METHOD_POST,
            };
            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_post_field(client, post_data, strlen(post_data));
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_err_t err = esp_http_client_perform(client);
            if (err == ESP_OK)
            {
                printf("HTTP POST Status = %d, content_length = %lld\n",
                       esp_http_client_get_status_code(client),
                       esp_http_client_get_content_length(client));
            }
            else
            {
                printf("HTTP POST failed: %s\n", esp_err_to_name(err));
            }
            esp_http_client_cleanup(client);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
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
        return;
    }

    wifi_init_sta();
    xTaskCreate(send_temperature_task, "send_temperature_task", 8192, NULL, 5, NULL);
}
