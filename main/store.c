#include "store.h"
#include "nvs_flash.h"
#include "nvs.h"

static nvs_handle_t s_nvs;

void store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nvs_open("arcade", NVS_READWRITE, &s_nvs));
}

uint32_t store_get(const char *key, uint32_t def)
{
    uint32_t v;
    return nvs_get_u32(s_nvs, key, &v) == ESP_OK ? v : def;
}

void store_set(const char *key, uint32_t value)
{
    nvs_set_u32(s_nvs, key, value);
    nvs_commit(s_nvs);
}
