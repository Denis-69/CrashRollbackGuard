# CrashRollbackGuard

Fail-safe OTA rollback helper for ESP32 / ESP32-S3 projects (Arduino core or ESP-IDF via PlatformIO). The guard tracks suspicious resets, manages previous OTA slots, and automatically rolls devices back to a known-good firmware without corrupting NVS or getting stuck in ping-pong loops.

## Highlights
- **Production-grade crash detection**: mirrored fail counters, CRC-protected labels, and guarded pending actions survive brownouts and mid-write resets.
- **Ping-pong protection**: configurable rollback guard prevents endless toggling between slots and optionally falls back to a factory partition.
- **Controlled restarts**: mark intentional `ESP.restart()` calls so they never inflate crash counters.
- **Safe OTA verification**: integrates with `ESP_OTA_IMG_PENDING_VERIFY` so passing health checks automatically marks the image as valid.
- **Allocation-free rollback logic**: crash detection, NVS access, and rollback decisions avoid dynamic allocation; use buffer helpers when you need zero-heap label access.

## Requirements
- ESP32 or ESP32-S3 target
- PlatformIO or Arduino core (IDF 4.x+)
- `Preferences`, `esp_ota_ops`, and `esp_partition` available in the framework

## Installation
Add the directory to your PlatformIO project as a library dependency (already present in this repo). When using it in another project, copy the folder into `lib/CrashRollbackGuard` or publish it to a private registry/Git submodule and reference it in `platformio.ini`.

## Quick Start
```cpp
#include <CrashRollbackGuard.h>

// Global instance recommended to ensure early boot lifetime
crg::CrashRollbackGuard guard;

static bool myResetFilter(esp_reset_reason_t reason) {
  // Example: ignore software resets triggered by your firmware
  return (reason != ESP_RST_SW);
}

void setup() {
  Serial.begin(115200);

  crg::Options opt;
  opt.failLimit             = 3;
  opt.stableTimeMs          = 60'000;
  opt.autoSavePrevSlot      = true;
  opt.fallbackToFactory     = true;
  opt.factoryLabel          = "factory";  // partition label from partitions.csv
  opt.maxRollbackAttempts   = 1;           // prevent ping-pong
  opt.swResetCountsAsCrash  = false;
  opt.brownoutCountsAsCrash = true;
  opt.logLevel              = crg::LogLevel::Info;
  opt.logOutput             = &Serial;     // Point logs to Serial, Serial1, etc.

  guard.setOptions(opt);
  guard.setSuspiciousResetPredicate(myResetFilter);

  guard.beginEarly();

  // start Wi-Fi, MQTT, etc.
}

void loop() {
  guard.loopTick();  // Automatically marks image healthy after stableTimeMs
}

void onAllServicesReady() {
  guard.markHealthyNow();
}

void beforeSwitchingToNewOTA() {
  guard.saveCurrentAsPreviousSlot();
  guard.armControlledRestart();
  ESP.restart(); // esp_restart() is used internally by the guard
}
```

### Reading Slot Labels Without Heap Usage
If you need to log or persist slot labels without constructing `String` objects, use the buffer-based helpers:

```cpp
char prevLabel[CRG_LABEL_BUFFER_SIZE];
if (guard.getPreviousSlot(prevLabel, sizeof(prevLabel))) {
  Serial.printf("Prev slot: %s\n", prevLabel);
}

char runningLabel[CRG_LABEL_BUFFER_SIZE];
if (crg::CrashRollbackGuard::getRunningLabel(runningLabel, sizeof(runningLabel))) {
  Serial.printf("Running slot: %s\n", runningLabel);
}
```

## Options Reference
| Field | Description |
| --- | --- |
| `nvsNamespace` | Namespace used to store guard metadata (defaults to `"crg"`). |
| `failLimit` | Number of suspicious resets before rollback logic engages. `0` disables crash-based rollback logic entirely (use with caution). |
| `stableTimeMs` | Milliseconds of uptime considered stable; `loopTick()` calls `markHealthyNow()` once this duration elapses. `0` disables the auto mark. |
| `autoSavePrevSlot` | Automatically remember the running slot as the previous slot when none is stored. Best used when you do not manage slots manually. |
| `logLevel` | `None`, `Error`, `Info`, or `Debug`. |
| `logOutput` | `Print*` destination for logs (defaults to `&Serial`). Set to `nullptr` to silence logs. |
| `fallbackToFactory` | Attempt to boot the factory partition when rollback to the previous OTA slot fails or does not exist. |
| `factoryLabel` | Partition label used for factory fallback. Only checked when `fallbackToFactory` is true. |
| `maxRollbackAttempts` | Caps consecutive rollbacks between slots. `0` removes the guard (not recommended). |
| `swResetCountsAsCrash` | Treat `ESP_RST_SW` as suspicious (default `false`). |
| `brownoutCountsAsCrash` | Treat `ESP_RST_BROWNOUT` as suspicious (default `false`). |

## Compile-Time Flags
Override via `platformio.ini` `build_flags`:

| Flag | Default | Purpose |
| --- | --- | --- |
| `CRG_NAMESPACE` | `"crg"` | Default NVS namespace. |
| `CRG_FAIL_LIMIT` | `3` | Default fail limit when `Options::failLimit` is untouched. |
| `CRG_STABLE_TIME_MS` | `60000UL` | Default stability window for `loopTick()`. |
| `CRG_AUTOSAVE_PREV_SLOT` | `0` | Auto-save current slot flag (`1` = enabled). |
| `CRG_LOG_ENABLED` | `1` | Compile-time logging toggle. |
| `CRG_FEATURE_FACTORY_FALLBACK` | `1` | Strip factory fallback logic entirely when set to `0`. |
| `CRG_FEATURE_STABLE_TICK` | `1` | Remove `loopTick()` auto-mark logic when `0`. |
| `CRG_FEATURE_PENDING_VERIFY_FIX` | `1` | Disable OTA image state inspection when `0`. |
| `CRG_LABEL_BUFFER_SIZE` | `ESP_PARTITION_LABEL_MAX_LEN + 1` | Override label buffer size. |
| `CRG_LOG_BUFFER_SIZE` | `192` | Size of the temporary log buffer. |
| `CRG_NAMESPACE_MAX_LEN` | `15` | Max namespace length (excluding null terminator). |

## Recommended Workflow for OTA Updates
1. **Before flashing a new image**: Call `guard.saveCurrentAsPreviousSlot()` while still running the known-good firmware.
2. **Just before `ESP.restart()`**: Call `guard.armControlledRestart()` so the following boot is considered intentional.
3. **After the new image boots**: Run `guard.beginEarly()` as early as possible in `setup()`—before Wi-Fi, MQTT, or other subsystems—so reset reasons and OTA states are evaluated before any user logic executes.
4. **After services are stable**: Call `guard.markHealthyNow()` to zero the fail counters and mark the OTA image as valid (if it was `PENDING_VERIFY`). Alternatively, let `loopTick()` handle it after `stableTimeMs` milliseconds of uptime.
5. **On reboot storms**: The guard increments fail counters only until `failLimit`. Once exceeded, it attempts to revert to the previous slot; if that fails and factory fallback is enabled, it boots the factory image instead.

## Safety Notes
- `Preferences` writes are minimized: fail counters and roll counts are mirrored with XOR values to detect corruption, and the guard writes only when necessary.
- All partition labels saved in NVS include CRC32 checksums to detect torn writes or flash wear. Corrupted entries are cleared automatically.
- Pending actions (rollback, factory fallback, controlled restarts) create a commit record before changing boot partitions. After the next boot, `beginEarly()` validates and clears the record so unexpected resets don’t cause double rollbacks.
- The guard never uses dynamic allocation along critical paths, making it safe to run during brownout/WDT recovery windows.

## Troubleshooting
| Symptom | Possible Cause | Mitigation |
| --- | --- | --- |
| Guard logs "Stored prev slot label corrupted" | CRC mismatch, likely due to flash wear or power loss. | The guard already clears the entry; re-save the slot using `saveCurrentAsPreviousSlot()`. |
| Rollback never triggers | `failLimit` too high or resets not classified as suspicious. | Lower `failLimit`, enable `swResetCountsAsCrash` / `brownoutCountsAsCrash`, or supply a custom predicate via `setSuspiciousResetPredicate()`. |
| Device ping-pongs between two slots | `maxRollbackAttempts` set to `0`. | Set to `1` or more to stop repeated rollbacks without a successful health mark. |
| Factory fallback ignored | `fallbackToFactory` disabled or `factoryLabel` missing in the partition table. | Ensure the label exists in `partitions.csv` and `Options::factoryLabel` matches exactly. |

## Non-Goals
- The guard does **not** download OTA images for you.
- It does not handle firmware encryption or signature verification.
- It does not replace or modify the bootloader.

## State Flow (High-Level)
```
Boot
 ├─ Pending verify image? → wait for health mark → mark VALID
 ├─ Suspicious reset? → increment fail counter
 │    └─ limit exceeded? → rollback to prev slot → factory fallback if needed
 └─ Healthy boot → clear counters and pending actions
```

## License
MIT (see `LICENSE`).
