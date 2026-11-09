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