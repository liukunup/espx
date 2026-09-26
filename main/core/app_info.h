/**
 * @file app_info.h
 * @brief Build identity of the running application
 *
 * The version has exactly ONE source: version.txt at the project root (falling
 * back to `git describe`, ESP-IDF's own precedence). It is compiled into the
 * image's application descriptor, so it is the version the bootloader, an OTA
 * server and the device itself all agree on.
 *
 * Before this existed, node identity reported CONFIG_FIRMWARE_VERSION while OTA
 * reported esp_app_desc's version (git describe). One firmware claiming two
 * different versions is an operational hazard: an OTA decision made on one
 * figure is wrong when compared against the other.
 */

#ifndef APP_INFO_H
#define APP_INFO_H

#include <esp_app_desc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Firmware version of the running image (never NULL)
 */
static inline const char* app_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return (d && d->version[0]) ? d->version : "unknown";
}

/**
 * @brief Build date and time of the running image
 */
static inline const char* app_build_date(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d ? d->date : "";
}

static inline const char* app_build_time(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d ? d->time : "";
}

/**
 * @brief IDF version the image was built with
 */
static inline const char* app_idf_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d ? d->idf_ver : "";
}

#ifdef __cplusplus
}
#endif

#endif /* APP_INFO_H */
