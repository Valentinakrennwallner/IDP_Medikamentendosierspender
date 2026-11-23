/*
 * Line input for the serial commissioning dialogs. Taken out of the
 * calibration dialog (task 9) so that the pre-shared key dialog (task 17)
 * uses the same input handling, with a masked echo.
 */
#include "serial_line.h"

#include <stddef.h>
#include <string.h>

#define ASCII_BACKSPACE 0x08U
#define ASCII_DELETE 0x7FU
#define ASCII_LINE_FEED 0x0AU
#define ASCII_CARRIAGE_RETURN 0x0DU
#define ASCII_FIRST_PRINTABLE 0x20U
#define ASCII_LAST_PRINTABLE 0x7EU

/*
 * Prepare a line reader with the I/O functions, the caller's buffer and the
 * echo mode. Design Document, Chapter 6, Section 6.1, Requirement 2.8, and
 * Section 6.2, Requirement 4.7.
 */
void serial_line_init(serial_line_t *line,
                      bool (*read_byte)(uint8_t *byte),
                      bool (*write_text)(const char *text),
                      char *buffer,
                      uint32_t capacity,
                      serial_line_echo_t echo)
{
    if (line != NULL)
    {
        line->read_byte = read_byte;
        line->write_text = write_text;
        line->text = buffer;
        line->capacity = capacity;
        line->echo = echo;
        line->length = 0U;
        line->too_long = false;
        line->skip_line_feed = false;
        line->io_failed = (read_byte == NULL) || (write_text == NULL) || (buffer == NULL);
        if (buffer != NULL)
        {
            buffer[0] = '\0';
        }
    }
}

/*
 * Send a text; after the first I/O error nothing more is sent.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
void serial_line_say(serial_line_t *line, const char *text)
{
    if (!line->io_failed && !line->write_text(text))
    {
        line->io_failed = true;
    }
}

/*
 * Echo one accepted character: the character itself, or '*' in masked
 * mode so that no part of a key is shown.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2,
 * Requirement 4.7.
 */
static void serial_line_echo(serial_line_t *line, char character)
{
    char text[2];

    text[0] = (line->echo == SERIAL_LINE_ECHO_MASKED) ? '*' : character;
    text[1] = '\0';
    serial_line_say(line, text);
}

/*
 * Read one line with echo and backspace. CR, LF and CR LF all end a line;
 * other control characters are ignored. Lines longer than the capacity are
 * marked too_long. Returns false on an I/O error.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8, and Section 6.2,
 * Requirement 4.7.
 */
bool serial_line_read(serial_line_t *line)
{
    bool line_complete = false;
    uint8_t byte = 0U;

    line->length = 0U;
    line->too_long = false;

    while (!line_complete && !line->io_failed)
    {
        if (!line->read_byte(&byte))
        {
            line->io_failed = true;
        }
        else if ((byte == ASCII_LINE_FEED) && line->skip_line_feed)
        {
            line->skip_line_feed = false;
        }
        else
        {
            line->skip_line_feed = false;
            if ((byte == ASCII_CARRIAGE_RETURN) || (byte == ASCII_LINE_FEED))
            {
                line->skip_line_feed = (byte == ASCII_CARRIAGE_RETURN);
                serial_line_say(line, "\r\n");
                line_complete = true;
            }
            else if ((byte == ASCII_BACKSPACE) || (byte == ASCII_DELETE))
            {
                if (line->length > 0U)
                {
                    line->length--;
                    serial_line_say(line, "\b \b");
                }
            }
            else if ((byte >= ASCII_FIRST_PRINTABLE) && (byte <= ASCII_LAST_PRINTABLE))
            {
                if (line->length < line->capacity)
                {
                    line->text[line->length] = (char)byte;
                    line->length++;
                    serial_line_echo(line, (char)byte);
                }
                else
                {
                    line->too_long = true;
                }
            }
            else
            {
                /* Other control characters are ignored. */
            }
        }
    }

    if (line->text != NULL)
    {
        line->text[line->length] = '\0';
    }
    return line_complete;
}

/*
 * True if the complete line (not too long) equals word.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
bool serial_line_is(const serial_line_t *line, const char *word)
{
    return !line->too_long && (strcmp(line->text, word) == 0);
}
