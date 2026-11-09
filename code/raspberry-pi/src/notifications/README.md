# Notifications module

`telegram_send_message()` posts a text reminder or alert to the Telegram Bot
API over HTTPS. It verifies the peer and hostname, pins certificate validity
checking to the supplied trusted epoch, restricts the URL to HTTPS, uses a
bounded request timeout, and requires a successful Telegram API response.
`telegram_send_message_with_time()` obtains certificate time from the
time-sync service: current NTP time when synchronized, otherwise the saved
last-valid epoch (Design Document, Chapter 2, Section 2.1, and Chapter 6,
Section 6.2, Requirement 4.10).
`telegram_heartbeat_timeout_alert()` sends the minimal “Dispenser not
reachable” notification and can be passed directly as the gateway heartbeat
monitor's alert callback (Chapter 6, Section 6.1, Requirement 3.5).

Example call:

```c
telegram_result_t result = telegram_send_message_with_time(
    bot_token, chat_id, reminder_text, &time_sync);
if (result != TELEGRAM_RESULT_OK) {
    /* Handle/report the failure without logging message contents or secrets. */
}
```

Provide the bot token and chat ID from the protected device configuration.
Never log these values or the message body. Build only the minimum reminder
or warning text required (Chapter 6, Sections 6.1–6.2, Requirements 1.2,
1.10–1.11, 2.6, 2.10, 2.13, 3.5, and 4.5).

This implementation requires libcurl built with the OpenSSL TLS backend so
the certificate verification epoch can be set. It returns an explicit error
when the backend is unavailable. The application must load the saved time
before its first HTTPS request and surface any non-success result.
