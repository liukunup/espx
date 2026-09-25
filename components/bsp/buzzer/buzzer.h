/**
 * @file buzzer.h
 * @brief Buzzer Driver (Passive and Active)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Buzzer type
 */
typedef enum {
    BUZZER_TYPE_ACTIVE = 0,  // Active buzzer (just toggle on/off)
    BUZZER_TYPE_PASSIVE,     // Passive buzzer (PWM tone generation)
} buzzer_type_t;

/**
 * @brief Buzzer configuration
 */
typedef struct {
    int8_t gpio_num;       // GPIO pin number (-1 to disable)
    buzzer_type_t type;    // Buzzer type
    uint32_t default_freq; // Default frequency for passive buzzer (Hz)
} buzzer_config_t;

/**
 * @brief Buzzer handle
 */
typedef struct buzzer_handle_s *buzzer_handle_t;

/**
 * @brief Create buzzer driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
buzzer_handle_t buzzer_create(const buzzer_config_t *config);

/**
 * @brief Delete buzzer driver instance
 * @param handle Driver handle
 */
void buzzer_delete(buzzer_handle_t handle);

/**
 * @brief Initialize buzzer
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int buzzer_init(buzzer_handle_t handle);

/**
 * @brief Turn buzzer on (active) or start playing tone (passive)
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int buzzer_on(buzzer_handle_t handle);

/**
 * @brief Turn buzzer off
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int buzzer_off(buzzer_handle_t handle);

/**
 * @brief Set frequency for passive buzzer (100-10000 Hz)
 * @param handle Driver handle
 * @param freq Frequency in Hz
 * @return 0 on success, negative on error
 */
int buzzer_set_freq(buzzer_handle_t handle, uint32_t freq);

/**
 * @brief Play a tone for specified duration
 * @param handle Driver handle
 * @param freq Frequency in Hz
 * @param duration_ms Duration in milliseconds (0 = play until stopped)
 * @return 0 on success, negative on error
 */
int buzzer_tone(buzzer_handle_t handle, uint32_t freq, uint32_t duration_ms);

/**
 * @brief Play a beep pattern
 * @param handle Driver handle
 * @param on_ms On duration in milliseconds
 * @param off_ms Off duration in milliseconds
 * @param count Number of beeps
 * @return 0 on success, negative on error
 */
int buzzer_beep(buzzer_handle_t handle, uint32_t on_ms, uint32_t off_ms, uint8_t count);

/**
 * @brief Play a melody note
 * @param handle Driver handle
 * @param note Note index (0-87, piano keys)
 * @param duration_ms Duration in milliseconds
 * @return 0 on success, negative on error
 */
int buzzer_play_note(buzzer_handle_t handle, uint8_t note, uint32_t duration_ms);

/**
 * @brief Get buzzer state
 * @param handle Driver handle
 * @param on Output state (true=on, false=off)
 * @return 0 on success, negative on error
 */
int buzzer_get_state(buzzer_handle_t handle, bool *on);

/**
 * @brief Common musical notes (A4 = 69)
 */
#define BUZZER_NOTE_C4  60
#define BUZZER_NOTE_D4  62
#define BUZZER_NOTE_E4  64
#define BUZZER_NOTE_F4  65
#define BUZZER_NOTE_G4  67
#define BUZZER_NOTE_A4  69
#define BUZZER_NOTE_B4  71
#define BUZZER_NOTE_C5  72

/**
 * @brief Calculate frequency from MIDI note number
 * @param note Note number (0-127)
 * @return Frequency in Hz
 */
uint32_t buzzer_note_to_freq(uint8_t note);

#ifdef __cplusplus
}
#endif
