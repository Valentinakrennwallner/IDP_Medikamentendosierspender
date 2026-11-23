#ifndef SERIAL_LINE_H
#define SERIAL_LINE_H

/*
 * Line input for the serial commissioning dialogs (calibration, pre-shared
 * key): reads one line byte by byte with backspace, accepts CR, LF and
 * CR LF as line end, and echoes either the typed characters or one '*' per
 * character (masked, for the pre-shared key). All I/O goes through two
 * function pointers, so the logic is host-testable.
 * Design Document, Chapter 2, Section 2.1 (weight_calibrate()), and
 * Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2, Requirement 4.7.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    /* Echo every typed character (calibration). */
    SERIAL_LINE_ECHO_PLAIN = 0,
    /* Echo '*' for every typed character, never the character itself. */
    SERIAL_LINE_ECHO_MASKED = 1
} serial_line_echo_t;

typedef struct
{
    /* Wait for one received byte; false on a read error. */
    bool (*read_byte)(uint8_t *byte);
    /* Send a zero-terminated text; false on a write error. */
    bool (*write_text)(const char *text);
    /* Caller buffer with room for capacity characters plus '\0'. */
    char *text;
    uint32_t capacity;
    serial_line_echo_t echo;
    /* Characters in text after serial_line_read(). */
    uint32_t length;
    /* More than capacity characters were typed; the line must be rejected. */
    bool too_long;
    /* The last line ended with CR; a directly following LF is skipped. */
    bool skip_line_feed;
    /* A read or write failed; nothing more is read or sent. */
    bool io_failed;
} serial_line_t;

/*
 * Prepare a line reader with the I/O functions, the caller's buffer
 * (capacity characters plus '\0') and the echo mode. Design Document,
 * Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2, Requirement 4.7.
 */
void serial_line_init(serial_line_t *line,
                      bool (*read_byte)(uint8_t *byte),
                      bool (*write_text)(const char *text),
                      char *buffer,
                      uint32_t capacity,
                      serial_line_echo_t echo);

/*
 * Send a text; after the first I/O error nothing more is sent.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
void serial_line_say(serial_line_t *line, const char *text);

/*
 * Read one line with echo and backspace. CR, LF and CR LF all end a line;
 * other control characters are ignored. Lines longer than the capacity are
 * marked too_long. Returns false on an I/O error (also after an earlier one).
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2,
 * Requirement 4.7.
 */
bool serial_line_read(serial_line_t *line);

/*
 * True if the complete line (not too long) equals word.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
bool serial_line_is(const serial_line_t *line, const char *word);

#endif
