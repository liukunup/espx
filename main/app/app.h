


#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t app_init(void);

esp_err_t app_loop(void);


#ifdef __cplusplus
}
#endif

#endif /* APP_H */
