#include "cloud_upload.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_AI_SCALE_CLOUD_UPLOAD_ENABLED

static const char *TAG = "cloud_upload";
static const EventBits_t WIFI_CONNECTED_BIT = BIT0;
static EventGroupHandle_t s_wifi_events;
static QueueHandle_t s_upload_queue;
static bool s_started;

typedef struct {
    char data[1024];
    size_t length;
} response_buffer_t;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u; reconnecting", (unsigned)event->reason);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi connected, ip=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t init_wifi(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "failed to erase NVS");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "failed to initialize NVS");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "failed to initialize esp-netif");
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL &&
        esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "failed to initialize remote Wi-Fi");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
        TAG, "failed to register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL),
        TAG, "failed to register IP event handler");

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, CONFIG_AI_SCALE_CLOUD_WIFI_SSID,
            sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, CONFIG_AI_SCALE_CLOUD_WIFI_PASSWORD,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "connecting to Wi-Fi SSID: %s", CONFIG_AI_SCALE_CLOUD_WIFI_SSID);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "failed to set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "failed to set Wi-Fi config");
    return esp_wifi_start();
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    response_buffer_t *response = (response_buffer_t *)event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0 && response != NULL) {
        const size_t remaining = sizeof(response->data) - response->length - 1;
        const size_t copy_len = (size_t)event->data_len < remaining
                                    ? (size_t)event->data_len
                                    : remaining;
        memcpy(response->data + response->length, event->data, copy_len);
        response->length += copy_len;
        response->data[response->length] = '\0';
    }
    return ESP_OK;
}

static char *build_json(const cloud_meal_record_t *record)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON *ingredients = cJSON_AddArrayToObject(root, "ingredients");
    cJSON *weights = cJSON_AddArrayToObject(root, "raw_weights_g");
    if (ingredients == NULL || weights == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (size_t i = 0; i < record->ingredient_count; ++i) {
        cJSON_AddItemToArray(ingredients, cJSON_CreateString(record->ingredients[i]));
        cJSON_AddItemToArray(weights, cJSON_CreateNumber(record->raw_weights_g[i]));
    }
    cJSON_AddStringToObject(root, "cooking_method", record->cooking_method);
    cJSON_AddNumberToObject(root, "cooked_weight_g", record->cooked_weight_g);
    cJSON_AddNumberToObject(root, "cooked_energy_kcal", record->cooked_energy_kcal);
    cJSON_AddNumberToObject(root, "cooked_protein_g", record->cooked_protein_g);
    cJSON_AddNumberToObject(root, "cooked_fat_g", record->cooked_fat_g);
    cJSON_AddNumberToObject(root, "cooked_carbohydrate_g", record->cooked_carbohydrate_g);
    cJSON_AddNumberToObject(root, "cooked_sodium_mg", record->cooked_sodium_mg);
    cJSON_AddNumberToObject(root, "cooked_cholesterol_mg", record->cooked_cholesterol_mg);
    cJSON_AddNumberToObject(root, "cooked_vitamin_c_mg", record->cooked_vitamin_c_mg);
    cJSON_AddNumberToObject(root, "cooked_calcium_mg", record->cooked_calcium_mg);
    cJSON_AddNumberToObject(root, "cooked_iron_mg", record->cooked_iron_mg);
    cJSON_AddNumberToObject(root, "cooked_potassium_mg", record->cooked_potassium_mg);
    cJSON_AddNumberToObject(root, "cooking_time_minutes", record->cooking_time_minutes);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static esp_err_t upload_record(const cloud_meal_record_t *record)
{
    char url[256];
    const int url_len = snprintf(url, sizeof(url), "%s?user_id=%d",
                                 CONFIG_AI_SCALE_CLOUD_RECORD_URL,
                                 CONFIG_AI_SCALE_CLOUD_USER_ID);
    if (url_len < 0 || url_len >= (int)sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *json = build_json(record);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    response_buffer_t response = {0};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        cJSON_free(json);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));
    ESP_LOGI(TAG, "uploading meal: foods=%u, method=%s, energy=%.1f kcal",
             (unsigned)record->ingredient_count, record->cooking_method,
             (double)record->cooked_energy_kcal);

    const esp_err_t perform_err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    if (perform_err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP status=%d response=%s", status,
                 response.length > 0 ? response.data : "<empty>");
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(perform_err));
    }

    esp_http_client_cleanup(client);
    cJSON_free(json);
    if (perform_err != ESP_OK) {
        return perform_err;
    }
    return status >= 200 && status < 300 ? ESP_OK : ESP_FAIL;
}

static void cloud_upload_task(void *arg)
{
    (void)arg;
    esp_err_t err = init_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cloud uploader stopped: Wi-Fi init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    cloud_meal_record_t record;
    while (xQueueReceive(s_upload_queue, &record, portMAX_DELAY) == pdTRUE) {
        while (true) {
            xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
            err = upload_record(&record);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "MEAL_UPLOAD_SUCCEEDED");
                break;
            }
            ESP_LOGW(TAG, "meal upload pending; retrying in %d seconds",
                     CONFIG_AI_SCALE_CLOUD_RETRY_SECONDS);
            vTaskDelay(pdMS_TO_TICKS(CONFIG_AI_SCALE_CLOUD_RETRY_SECONDS * 1000));
        }
    }
    vTaskDelete(NULL);
}

esp_err_t cloud_upload_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    if (strlen(CONFIG_AI_SCALE_CLOUD_WIFI_SSID) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_events = xEventGroupCreate();
    s_upload_queue = xQueueCreate(5, sizeof(cloud_meal_record_t));
    if (s_wifi_events == NULL || s_upload_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(cloud_upload_task, "cloud_upload", 8192, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

bool cloud_upload_submit(const cloud_meal_record_t *record)
{
    if (!s_started || record == NULL || record->ingredient_count == 0 ||
        record->ingredient_count > CLOUD_UPLOAD_MAX_INGREDIENTS) {
        return false;
    }
    cloud_meal_record_t queued_record = *record;
    queued_record.cooking_time_minutes = CONFIG_AI_SCALE_CLOUD_COOKING_TIME_MINUTES;
    if (xQueueSend(s_upload_queue, &queued_record, 0) != pdTRUE) {
        ESP_LOGE(TAG, "upload queue full; meal was not queued");
        return false;
    }
    ESP_LOGI(TAG, "meal queued for cloud upload");
    return true;
}

#else

esp_err_t cloud_upload_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool cloud_upload_submit(const cloud_meal_record_t *record)
{
    (void)record;
    return false;
}

#endif
