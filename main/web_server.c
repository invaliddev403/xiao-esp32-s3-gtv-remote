#include "web_server.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "remote.h"

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static const char *tag = "web_server";

static esp_err_t send_error(httpd_req_t *request, const char *status, const char *message)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(body, "error", message);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t result = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return result;
}

static esp_err_t send_ok(httpd_req_t *request)
{
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static esp_err_t index_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, (const char *)index_html_start,
                           index_html_end - index_html_start);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    remote_refresh_status();
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return send_error(request, "500 Internal Server Error", "Out of memory");
    }
    cJSON_AddBoolToObject(body, "connected", remote_is_connected());
    cJSON_AddBoolToObject(body, "pairing_failed", remote_pairing_failed());
    cJSON_AddStringToObject(body, "peer_name", remote_peer_name());
    cJSON_AddNumberToObject(body, "rssi", remote_peer_rssi());
    cJSON_AddNumberToObject(body, "bonded_devices", remote_saved_device_count());

    char address[18] = "Unavailable";
    uint8_t bytes[6];
    if (remote_peer_address(bytes)) {
        snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                 bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    }
    cJSON_AddStringToObject(body, "peer_address", address);

    cJSON *device_list = cJSON_AddArrayToObject(body, "devices");
    remote_device_info_t devices[REMOTE_MAX_DEVICES];
    int device_count = remote_get_devices(devices, REMOTE_MAX_DEVICES);
    for (int i = 0; device_list && i < device_count; i++) {
        cJSON *device = cJSON_CreateObject();
        if (!device) {
            break;
        }
        char device_address[18];
        snprintf(device_address, sizeof(device_address), "%02X:%02X:%02X:%02X:%02X:%02X",
                 devices[i].address[0], devices[i].address[1], devices[i].address[2],
                 devices[i].address[3], devices[i].address[4], devices[i].address[5]);
        cJSON_AddStringToObject(device, "address", device_address);
        cJSON_AddStringToObject(device, "name", devices[i].name[0] ? devices[i].name : "Saved Google TV");
        cJSON_AddBoolToObject(device, "active", devices[i].active);
        cJSON_AddItemToArray(device_list, device);
    }

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) {
        return send_error(request, "500 Internal Server Error", "Out of memory");
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return result;
}

static bool lookup_key(const char *name, remote_key_t *key)
{
    static const struct {
        const char *name;
        remote_key_t key;
    } keys[] = {
        {"up", REMOTE_KEY_UP},
        {"down", REMOTE_KEY_DOWN},
        {"left", REMOTE_KEY_LEFT},
        {"right", REMOTE_KEY_RIGHT},
        {"ok", REMOTE_KEY_OK},
        {"back", REMOTE_KEY_BACK},
        {"home", REMOTE_KEY_HOME},
        {"power", REMOTE_KEY_POWER},
        {"volume_up", REMOTE_KEY_VOLUME_UP},
        {"volume_down", REMOTE_KEY_VOLUME_DOWN},
        {"mute", REMOTE_KEY_MUTE},
        {"play_pause", REMOTE_KEY_PLAY_PAUSE},
        {"rewind", REMOTE_KEY_REWIND},
        {"fast_forward", REMOTE_KEY_FAST_FORWARD},
        {"previous", REMOTE_KEY_PREVIOUS},
        {"next", REMOTE_KEY_NEXT},
        {"stop", REMOTE_KEY_STOP},
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (strcmp(name, keys[i].name) == 0) {
            *key = keys[i].key;
            return true;
        }
    }
    return false;
}

static esp_err_t key_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > 96) {
        return send_error(request, "400 Bad Request", "Invalid request size");
    }
    char body[97];
    size_t received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, body + received, request->content_len - received);
        if (result <= 0) {
            return send_error(request, "400 Bad Request", "Could not read request");
        }
        received += result;
    }
    body[received] = '\0';

    cJSON *json = cJSON_Parse(body);
    const cJSON *name = json ? cJSON_GetObjectItemCaseSensitive(json, "key") : NULL;
    remote_key_t key;
    bool valid = cJSON_IsString(name) && lookup_key(name->valuestring, &key);
    cJSON_Delete(json);
    if (!valid) {
        return send_error(request, "400 Bad Request", "Unknown key");
    }
    if (!remote_is_connected()) {
        return send_error(request, "409 Conflict", "Google TV is not connected");
    }
    esp_err_t result = remote_send_key(key);
    if (result != ESP_OK) {
        return send_error(request, "503 Service Unavailable", "Could not queue key");
    }
    return send_ok(request);
}

static bool parse_address(const char *text, uint8_t *address)
{
    if (!text || strlen(text) != 17) {
        return false;
    }
    unsigned int values[6];
    if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x", &values[0], &values[1],
               &values[2], &values[3], &values[4], &values[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        address[i] = (uint8_t)values[i];
    }
    return true;
}

static esp_err_t connect_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > 96) {
        return send_error(request, "400 Bad Request", "Invalid request size");
    }
    char body[97];
    size_t received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, body + received, request->content_len - received);
        if (result <= 0) {
            return send_error(request, "400 Bad Request", "Could not read request");
        }
        received += result;
    }
    body[received] = '\0';

    cJSON *json = cJSON_Parse(body);
    const cJSON *address_text = json ? cJSON_GetObjectItemCaseSensitive(json, "address") : NULL;
    uint8_t address[6];
    bool valid = cJSON_IsString(address_text) && parse_address(address_text->valuestring, address);
    cJSON_Delete(json);
    if (!valid) {
        return send_error(request, "400 Bad Request", "Invalid device address");
    }
    esp_err_t result = remote_connect_device(address);
    if (result == ESP_ERR_NOT_FOUND) {
        return send_error(request, "404 Not Found", "Device is not paired with this remote");
    }
    return result == ESP_OK ? send_ok(request)
                            : send_error(request, "500 Internal Server Error", esp_err_to_name(result));
}

static esp_err_t rename_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > 192) {
        return send_error(request, "400 Bad Request", "Invalid request size");
    }
    char body[193];
    size_t received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, body + received, request->content_len - received);
        if (result <= 0) {
            return send_error(request, "400 Bad Request", "Could not read request");
        }
        received += result;
    }
    body[received] = '\0';

    cJSON *json = cJSON_Parse(body);
    const cJSON *address_text = json ? cJSON_GetObjectItemCaseSensitive(json, "address") : NULL;
    const cJSON *name = json ? cJSON_GetObjectItemCaseSensitive(json, "name") : NULL;
    uint8_t address[6];
    bool valid = cJSON_IsString(address_text) && cJSON_IsString(name) &&
                 parse_address(address_text->valuestring, address);
    esp_err_t result = valid ? remote_rename_device(address, name->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(json);
    if (result == ESP_ERR_INVALID_ARG) {
        return send_error(request, "400 Bad Request", "Enter a name between 1 and 63 bytes");
    }
    if (result == ESP_ERR_NOT_FOUND) {
        return send_error(request, "404 Not Found", "Device is not paired with this remote");
    }
    return result == ESP_OK ? send_ok(request)
                            : send_error(request, "500 Internal Server Error", esp_err_to_name(result));
}

static esp_err_t forget_device_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > 96) {
        return send_error(request, "400 Bad Request", "Invalid request size");
    }
    char body[97];
    size_t received = 0;
    while (received < request->content_len) {
        int result = httpd_req_recv(request, body + received, request->content_len - received);
        if (result <= 0) {
            return send_error(request, "400 Bad Request", "Could not read request");
        }
        received += result;
    }
    body[received] = '\0';

    cJSON *json = cJSON_Parse(body);
    const cJSON *address_text = json ? cJSON_GetObjectItemCaseSensitive(json, "address") : NULL;
    uint8_t address[6];
    bool valid = cJSON_IsString(address_text) && parse_address(address_text->valuestring, address);
    cJSON_Delete(json);
    if (!valid) {
        return send_error(request, "400 Bad Request", "Invalid device address");
    }
    esp_err_t result = remote_forget_device(address);
    if (result == ESP_ERR_NOT_FOUND) {
        return send_error(request, "404 Not Found", "Device is not paired with this remote");
    }
    return result == ESP_OK ? send_ok(request)
                            : send_error(request, "500 Internal Server Error", esp_err_to_name(result));
}

static esp_err_t disconnect_handler(httpd_req_t *request)
{
    esp_err_t result = remote_disconnect_device();
    return result == ESP_OK ? send_ok(request)
                            : send_error(request, "500 Internal Server Error", esp_err_to_name(result));
}

static esp_err_t retry_handler(httpd_req_t *request)
{
    esp_err_t result = remote_retry_pairing();
    return result == ESP_OK ? send_ok(request)
                            : send_error(request, "500 Internal Server Error", esp_err_to_name(result));
}

static esp_err_t forget_handler(httpd_req_t *request)
{
    esp_err_t result = remote_clear_pairing();
    return result == ESP_OK ? send_ok(request)
                            : send_error(request, "500 Internal Server Error", esp_err_to_name(result));
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    httpd_handle_t server;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), tag, "HTTP server start failed");
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/key", .method = HTTP_POST, .handler = key_handler},
        {.uri = "/api/connect", .method = HTTP_POST, .handler = connect_handler},
        {.uri = "/api/disconnect", .method = HTTP_POST, .handler = disconnect_handler},
        {.uri = "/api/rename", .method = HTTP_POST, .handler = rename_handler},
        {.uri = "/api/forget-device", .method = HTTP_POST, .handler = forget_device_handler},
        {.uri = "/api/retry", .method = HTTP_POST, .handler = retry_handler},
        {.uri = "/api/forget", .method = HTTP_POST, .handler = forget_handler},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        esp_err_t result = httpd_register_uri_handler(server, &handlers[i]);
        if (result != ESP_OK) {
            httpd_stop(server);
            ESP_RETURN_ON_ERROR(result, tag, "HTTP handler registration failed");
        }
    }
    ESP_LOGI(tag, "Web remote started");
    return ESP_OK;
}
