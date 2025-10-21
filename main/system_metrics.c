
#include "system_metrics.h"

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

#define UPDATE_INTERVAL_MS 1000  // 1 second refresh

static system_metrics_t metrics;
static const char *TAG = "metrics";

static void update_metrics(void *arg)
{
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t free_heap  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    metrics.used_percent = 100.0f * (1.0f - (float)free_heap / (float)total_heap);

    metrics.free_heap = free_heap;
    metrics.total_heap = total_heap;
    metrics.min_free_heap = esp_get_minimum_free_heap_size();
    metrics.uptime_ms = esp_timer_get_time() / 1000;
}

void system_metrics_init(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = &update_metrics,
        .name = "metrics_timer"
    };
    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_periodic(timer, UPDATE_INTERVAL_MS * 1000);
}

system_metrics_t *system_metrics_get(void)
{
    return &metrics;
}
