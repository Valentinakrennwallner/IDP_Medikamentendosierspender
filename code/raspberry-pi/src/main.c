#include <stdlib.h>

#include "logging/logging.h"

/*
 * Start the gateway process: start logging and print the start-up message
 * with the software version; device services are wired in by the
 * application. If logging cannot be started, the gateway continues without
 * it. Design Document, Chapter 2, Section 2.1 (System start and logging)
 * and Chapter 6, Section 6.3, Requirements 5.3 and 5.4.
 */
int main(void)
{
    /* log_init() prints the start-up message; failure is not fatal. */
    (void)log_init(NULL);

    log_shutdown();
    return EXIT_SUCCESS;
}
