#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_init(void);

void app_loop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
