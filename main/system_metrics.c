
#include "system_metrics.h"

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

static system_metrics_t metrics;
static const char *TAG = "metrics";

static void update_metrics(void)
{
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t free_heap  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    metrics.used_percent = 100.0f * (1.0f - (float)free_heap / (float)total_heap);

    metrics.free_heap = free_heap;
    metrics.total_heap = total_heap;
    metrics.min_free_heap = esp_get_minimum_free_heap_size();
    metrics.uptime_ms = esp_timer_get_time() / 1000000;
}

system_metrics_t *system_metrics_get(void)
{
    update_metrics();
    return &metrics;
}
