# Prompts

The LLM prompts below were used with auto-selected Github Copilot standard models (Claude Haiku 5.5, Claude Sonnet 5.5, Gemini 3.8 Flash, GPT-6 Luna, GPT-6.1 Terra) unless noted otherwise to create, modify and validate code and documentation.

- Read the specification in the docs folder (start.tex) and recommend folder structure for implementation
- Combine framework, gateway and protocol into a single code folder
- Eliminate the firmware and gateway folders, move their subfolders up one level
- Add code style rules
- Add tests for MISRA C compliance
- MISRA C check is required for both raspberry and esp32
- Document development software requirements in the README.md
- Implement time_sync according to specification
- Add code to pass schedule and current time as per flow diagram
- Add code to pass schedule information to the esp32
- Add comments to each function explaining the purpose, and if applicable reference the documentation as "Design Document" which chapter and section numbering as per the structure in start.pdf
- Add functions for setting LED states as required by the specification
- Add Telegram communication using the Telegram HTTPS API as per the specification
- Implement heartbeat logic
- Implement ble communication
- Validate that there are comments to each function explaining the purpose, and if applicable reference the documentation as "Design Document" which chapter and section numbering as per the structure in start.pdf
- Remember instructions where to find the speciication and how to reference the specification.
- Remember instructions that code must follow MISRA C rules
- Remember instructions about commenting functions
- Implement key exchange and encryption
- Implement time critical weighing function
- You are an experienced developer of safety critical software. review the functionality that has been implemented already against the specification, eliminate redundant code and reduce complexity where possible (Claude Sonnet 5.5)
- Follow docs/code-style.md (MISRA C, a purpose comment on every function with "Design Document, Chapter N, Section N.M" references per start.pdf). Add host tests, keep ctest and the WSL host build green, and update the affected README. Don't change unrelated code.
- Triage the misra_c_screening findings. Write docs/misra-deviations.md with justified deviations for the advisory rules (15.1 goto cleanup, 15.5 early returns, 2.5, 8.9). Make the screening command skip only those rules. List the remaining required-rule findings. (Claude Sonnet 5.5)
- Follow docs/code-style.md (MISRA C, a purpose comment on every function with "Design Document, Chapter N, Section N.M" references per start.pdf). Add host tests, keep ctest and the WSL host build green, and update the affected README. Change one thing at a time, explain what was changed and advise if further changes are needed. Don't change unrelated code.
- Fix MISRA screening script after merge b4ad4aa, keep per-device version of check.cmake (Claude Code, Claude Opus 5.5)
- Add host interop ctest for the OpenSSL and mbedTLS secure channel backends (Claude Code, Claude Opus 5.5)
- Replace calloc/free in both secure_channel backends with a static session pool (MISRA 21.3) (Claude Code, Claude Opus 5.5)
