# CrashRollbackGuard — Design Rationale

## Goals
CrashRollbackGuard is designed to solve one specific problem:
**prevent unrecoverable reboot loops and unsafe OTA rollbacks on ESP32-class devices.**

The library prioritizes:
- deterministic behavior during early boot,
- survival across brownouts and mid-write resets,
- explicit user intent over heuristic guesses.

## Non-Goals
This library does NOT:
- download OTA images,
- manage firmware signing or encryption,
- replace the ESP32 bootloader.

## Core Principles

### 1. Early Decision Making
All critical decisions are made in `beginEarly()` before:
- Wi-Fi,
- MQTT,
- user tasks.

This ensures reset reasons and OTA states are evaluated before side effects occur.

### 2. Explicit Intent Beats Heuristics
Operations that look like crashes but are intentional (OTA reboot, software restart)
must be explicitly marked via `armControlledRestart()`.

Unmarked resets are treated conservatively.

### 3. NVS Is Treated as Unreliable
All persistent metadata is protected using:
- mirrored counters,
- CRC32-protected labels,
- automatic repair or clearing on mismatch.

No single NVS value is trusted blindly.

### 4. Rollback Is a Transaction
Partition switches are guarded by pending-action records.
After reboot, the guard validates and clears the action
to prevent double rollbacks or ping-pong loops.

### 5. Zero Heap Usage in Critical Paths
All persistent strings and buffers use fixed storage.
No dynamic allocation occurs during early boot or crash handling.

## Supported Recovery Strategies
- Rollback to previous OTA slot
- Factory partition fallback (optional)
- Controlled restart without crash accounting

## Expected Environment
CrashRollbackGuard is designed for:
- ESP32 / ESP32-S3
- Arduino core or ESP-IDF (via PlatformIO)
- Devices deployed unattended in the field
