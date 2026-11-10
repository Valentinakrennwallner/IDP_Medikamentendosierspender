# Prompts

The LLM prompts below were used with Github Copilot standard models (Claude Haiku 5.5, Claude Sonnet 5.5, Gemini 3.8 Flash, GPT-6 Luna, GPT-6.1 Terra) to create, modify and validate code and documentation.

- read the specification in the docs folder (start.tex) and recommend folder structure for implementation
- combine framework, gateway and protocol into a single code folder
- eliminate the firmware and gateway folders, move their subfolders up one level
- add code style rules
- add tests for misra c compliance
- misra c check is required for both raspberry and esp32
- document development software requirements in the README.md
- implement time_sync according to specification
- add code to pass schedule and current time as per flow diagram
- add code to pass schedule information to the esp32
- add comments to each function explaining the purpose, and if applicable reference the documentation as "Design Document" which chapter and section numbering as per the structure in start.pdf
- add functions for setting LED states as required by the specification
- add Telegram communication using the Telegram HTTPS API as per the specification
- implement heartbeat logic
- implement ble communication
- validate that there are comments to each function explaining the purpose, and if applicable reference the documentation as "Design Document" which chapter and section numbering as per the structure in start.pdf
- remember instructions where to find the speciication and how to reference the specification.
- remember instructions that code must follow MISRA C rules
- remember instructions about commenting functions
- implement key exchange and encryption
- implement time critical weighing function
- you are an experienced developer of safety critical software. review the functionality that has been implemented already against the specification, eliminate redundant code and reduce complexity where possible (Claude Sonnet 5.5)
