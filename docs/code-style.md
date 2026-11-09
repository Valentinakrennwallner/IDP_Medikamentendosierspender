# Code style

These rules apply to C source and headers in `code/`. Use the repository
`.clang-format` file for mechanical formatting. ESP-IDF-required conventions
may differ; keep any deviation local and consistent with the relevant SDK API.

## Formatting and naming

- Use four spaces for indentation; do not use tabs.
- Put opening braces for functions and control-flow blocks on their own line.
- Keep lines at or below 100 columns where practical. Break long parameter
  lists and conditions across readable lines.
- Use `snake_case` for functions, variables, and files. Use `UPPER_SNAKE_CASE`
  for macros and compile-time constants.
- Use descriptive names that communicate units and state (for example,
  `weight_grams`, `sequence_number`, and `intake_window_active`).
- Keep module interfaces in headers and implementation details private to the
  source file. Include only the headers a file directly uses.

## C and safety

- Write portable, explicit C. Avoid compiler extensions unless required by the
  target SDK and documented at the use site.
- Follow MISRA C as the project safety baseline. Document and justify any
  deviation; a clean build or formatter run is not proof of MISRA compliance.
- Use fixed-width integer types for protocol fields and hardware values where
  their width matters. Check conversions, bounds, and arithmetic overflow.
- Validate all external input, sensor values, configuration, and decoded
  messages before using them.
- Propagate errors explicitly. Do not ignore return values from hardware,
  operating-system, cryptographic, or I/O APIs.
- Keep interrupt handlers short; notify or queue work for the appropriate
  task/thread instead of doing lengthy work in the handler.
- Avoid dynamic allocation in safety-critical runtime paths unless a documented
  design requires it and allocation failure is handled.
- Keep dose evaluation and user-visible safety feedback on the ESP32. Do not
  allow communication or logging failures to silently change dosing decisions.

## Security, logging, and tests

- Never log medication names, intake times, doses, weights, keys, tokens, or
  decrypted protocol payloads.
- Do not commit device secrets or real user schedules. Keep configuration
  parsing strict and fail explicitly on missing or invalid required values.
- Keep cryptography behind the secure-channel module; do not invent custom
  primitives or bypass authentication and sequence-number checks.
- Add or update tests alongside behavior changes. Name tests or test cases with
  the relevant specification requirement IDs where practical.
- Test boundary conditions, invalid input, fault recovery, and communication
  failure paths, not only successful operation.
- Run `ctest --test-dir <build-directory> -R misra_c_screening
  --output-on-failure` after configuring either the ESP32 or Raspberry Pi
  CMake project. The test requires cppcheck, including its standard
  configuration and MISRA addon, to be available on `PATH`.
  It uses cppcheck's MISRA addon as a preliminary static-analysis screen.
  The addon checks only a subset of MISRA C rules and does not establish
  project compliance; formally claiming compliance requires a suitable
  qualified toolchain, documented deviations, and review of its results.
