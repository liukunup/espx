/**
 * @file defaults.h
 * @brief Seed the factory-default configuration exactly once
 *
 * A fresh node with nothing bound is not useful: you cannot tell whether the
 * firmware works without binding something first. This seeds the on-board
 * WS2812 as a device so a new unit has a visible, controllable output from the
 * first boot.
 *
 * "Exactly once" matters. The seed is guarded by a flag in NVS, not by "is the
 * device list empty": if it were the latter, deleting the LED would bring it
 * back on the next reboot and the user could never get rid of it.
 *
 * Factory data wins: if the provisioning payload already supplied devices, or
 * if the node has been configured before, nothing is added.
 */

#ifndef DEFAULTS_H
#define DEFAULTS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add the default devices if this node has never been seeded
 *
 * Call after device_manager_load() and after factory provisioning.
 *
 * @return ESP_OK (including when nothing was added)
 */
esp_err_t defaults_seed_once(void);

/**
 * @brief Whether the defaults have already been seeded on this unit
 */
bool defaults_seeded(void);

#ifdef __cplusplus
}
#endif

#endif /* DEFAULTS_H */
