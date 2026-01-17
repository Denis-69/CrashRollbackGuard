---
name: Bug report
about: Help us squash regressions, crash loops, and OTA surprises
labels: bug
---

## Summary
A clear and concise description of the issue.

## Hardware / Firmware
- Board: (e.g., ESP32-S3 DevKitC-1)
- Framework / version: (Arduino core, PlatformIO env, IDF release)
- CrashRollbackGuard commit / tag:

## Steps to Reproduce
1. …
2. …
3. …

## Expected Behavior
What you thought would happen.

## Actual Behavior / Logs
- Serial/monitor output (enable `CRG_LOG_ENABLED=1` if possible)
- Reset reason from `guard.lastResetReason()` if available
- Any panic/backtrace data

## OTA / Slot Details
- Current running partition label:
- Stored previous slot label:
- Factory fallback enabled?:

## Additional Context
Configuration snippets (`Options`, compile-time flags), wiring setup, or any other info that helps us reproduce the issue.
