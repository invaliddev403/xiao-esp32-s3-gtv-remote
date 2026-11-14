#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "remote.h"
#include "usb_network.h"
#include "web_server.h"

#define STATUS_LED_GPIO GPIO_NUM_21

static void status_led_task(void *argument)
{
    bool on = false;

    while (true) {
        if (remote_is_connected()) {
            on = true;
        } else {
            on = !on;
        }
        ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_GPIO, on ? 0 : 1));
        vTaskDelay(pdMS_TO_TICKS(remote_is_connected() ? 200 : 500));
    }
}

static void status_led_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_GPIO, 1));
    ESP_ERROR_CHECK(xTaskCreate(status_led_task, "status_led", 2048, NULL, 1, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}

void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(usb_network_init());
    ESP_ERROR_CHECK(remote_init());
    status_led_init();
    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI("app", "XIAO Google TV web remote ready");
}
