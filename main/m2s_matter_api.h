#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int started;
    int start_requested;
    int ready;
    int start_error;
    int pair_busy;
    int pair_result;
} m2s_matter_status_t;

int m2s_matter_request_start(void);
int m2s_matter_request_pair(uint64_t node, uint32_t pin, uint16_t discriminator);
void m2s_matter_get_status(m2s_matter_status_t *status);

#ifdef __cplusplus
}
#endif
