#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "remote.h"
#include "usb_network.h"
#include "web_server.h"

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
    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI("app", "XIAO Google TV web remote ready");
}
