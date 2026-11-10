#ifndef MOCK_FREERTOS_PORTMACRO_H
#define MOCK_FREERTOS_PORTMACRO_H

/* The host test is single threaded, so the critical section is a no-op. */
typedef int portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))

#endif
