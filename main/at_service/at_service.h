/**
 * @file at_service.h
 * @brief Serial AT command interface
 *
 * Command set and wire format follow ESP-AT
 * (https://github.com/espressif/esp-at, README_CN.md):
 *
 *   - Terminate every command with CR+LF
 *   - Success        -> \r\nOK\r\n
 *   - Failure        -> \r\nERROR\r\n
 *   - Query          -> \r\n+CMD:<value>\r\n\r\nOK\r\n
 *   - Test           -> \r\n+CMD:<help>\r\n\r\nOK\r\n
 *
 * ESPX deliberately implements a SUBSET of ESP-AT, chosen so that everything a
 * host needs to commission and drive a node is available. It is not a drop-in
 * replacement for the ESP-AT firmware: ESP-AT is a complete application that
 * owns the device, whereas here AT is one more channel onto the same
 * configuration model (see AGENT.md).
 *
 * Every mutating command goes through config_apply()/device_write()/the MQTT
 * client; the AT layer contains no configuration logic of its own.
 *
 * Default UART is UART1 so the AT port and the log/test console (UART0) do not
 * fight over the same pins. Change it with CONFIG_ESPX_AT_UART_*.
 */

#ifndef AT_SERVICE_H
#define AT_SERVICE_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** AT version reported by AT+GMR */
#define AT_SERVICE_VERSION "ESPX-AT-1.0.0"

/**
 * @brief Start the AT command service
 *
 * Configures the AT UART and starts the command task. Safe to call once.
 */
esp_err_t at_service_start(void);

/**
 * @brief Stop the AT service (releases the UART)
 */
esp_err_t at_service_stop(void);

/**
 * @brief Whether the service is running
 */
bool at_service_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_SERVICE_H */
