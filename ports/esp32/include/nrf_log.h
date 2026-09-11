#pragma once
#include <esp_log.h>
#define NRF_LOG_INFO(...)    ESP_LOGI("InfiniTime", __VA_ARGS__)
#define NRF_LOG_WARNING(...) ESP_LOGW("InfiniTime", __VA_ARGS__)
#define NRF_LOG_ERROR(...)   ESP_LOGE("InfiniTime", __VA_ARGS__)
