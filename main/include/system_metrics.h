#ifndef system_metrics_H
#define system_metrics_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t total_heap;
    size_t free_heap;
    size_t min_free_heap;
    float used_percent;
    uint64_t uptime_ms;
} system_metrics_t;
/**
 * @brief  metrics initialization 
 */
void system_metrics_init(void);
system_metrics_t *system_metrics_get(void);
#endif // system_metrics_H