#include "usb_network.h"

#include <stdlib.h>
#include <string.h>

#include "dhcpserver/dhcpserver_options.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "lwip/esp_netif_net_stack.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"

static const char *tag = "usb_network";
static esp_netif_t *usb_netif;
static esp_netif_ip_info_t usb_ip_info = {
    .ip = {.addr = ESP_IP4TOADDR(192, 168, 240, 1)},
    .gw = {.addr = ESP_IP4TOADDR(192, 168, 240, 1)},
    .netmask = {.addr = ESP_IP4TOADDR(255, 255, 255, 0)},
};

static esp_err_t usb_receive(void *buffer, uint16_t length, void *context)
{
    if (!usb_netif) {
        return ESP_ERR_INVALID_STATE;
    }
    void *copy = malloc(length);
    if (!copy) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, buffer, length);
    return esp_netif_receive(usb_netif, copy, length, NULL);
}

static void free_receive_buffer(void *handle, void *buffer)
{
    free(buffer);
}

static esp_err_t usb_transmit(void *handle, void *buffer, size_t length)
{
    return tinyusb_net_send_sync(buffer, length, NULL, pdMS_TO_TICKS(100));
}

esp_err_t usb_network_init(void)
{
    const tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&usb_config), tag, "USB driver initialization failed");

    tinyusb_net_config_t network_config = {
        .on_recv_callback = usb_receive,
    };
    ESP_RETURN_ON_ERROR(esp_read_mac(network_config.mac_addr, ESP_MAC_WIFI_STA), tag,
                        "Could not read device MAC address");
    ESP_RETURN_ON_ERROR(tinyusb_net_init(&network_config), tag, "USB NCM initialization failed");

    esp_netif_inherent_config_t base_config = {
        .flags = ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP,
        .ip_info = &usb_ip_info,
        .if_key = "usb_ncm",
        .if_desc = "USB web remote",
        .route_prio = 10,
    };
    esp_netif_driver_ifconfig_t driver_config = {
        .handle = (void *)1,
        .transmit = usb_transmit,
        .driver_free_rx_buffer = free_receive_buffer,
    };
    struct esp_netif_netstack_config stack_config = {
        .lwip = {
            .init_fn = ethernetif_init,
            .input_fn = ethernetif_input,
        },
    };
    esp_netif_config_t config = {
        .base = &base_config,
        .driver = &driver_config,
        .stack = &stack_config,
    };

    usb_netif = esp_netif_new(&config);
    ESP_RETURN_ON_FALSE(usb_netif != NULL, ESP_FAIL, tag, "USB network interface creation failed");

    uint8_t netif_mac[6];
    memcpy(netif_mac, network_config.mac_addr, sizeof(netif_mac));
    netif_mac[5] ^= 0x01;
    ESP_RETURN_ON_ERROR(esp_netif_set_mac(usb_netif, netif_mac), tag, "USB MAC configuration failed");

    uint32_t lease_minutes = 60;
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_option(usb_netif, ESP_NETIF_OP_SET,
                                               IP_ADDRESS_LEASE_TIME, &lease_minutes,
                                               sizeof(lease_minutes)), tag,
                        "DHCP lease configuration failed");
    esp_netif_action_start(usb_netif, 0, 0, 0);
    ESP_LOGI(tag, "USB NCM ready; web remote at http://192.168.240.1");
    return ESP_OK;
}
