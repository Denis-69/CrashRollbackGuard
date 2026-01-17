# Configuration Reference

This file consolidates all runtime options (set through `crg::Options`) and compile-time flags available in CrashRollbackGuard. Use it as a checklist before integrating the guard into production firmware.

---

## Runtime Options (`crg::Options`)

| Field | Default | Description |
| --- | --- | --- |
| `nvsNamespace` | `CRG_NAMESPACE` (`"crg"`) | Namespace used for guard metadata in NVS. Maximum length is `CRG_NAMESPACE_MAX_LEN` characters. |
| `failLimit` | `CRG_FAIL_LIMIT` (3) | Suspicious reset threshold. When the counter reaches or exceeds this value, rollback logic is triggered. `0` disables crash-based rollbacks. |
| `stableTimeMs` | `CRG_STABLE_TIME_MS` (60000) | Automatic health window for `loopTick()`. `0` disables the auto mark. |
| `autoSavePrevSlot` | `CRG_AUTOSAVE_PREV_SLOT` (`false`) | When true, `beginEarly()` stores the running slot label if no previous slot is present. |
| `logLevel` | `CRG_LOG_ENABLED ? LogLevel::Info : LogLevel::None` | Controls verbosity (`None`, `Error`, `Info`, `Debug`). |
| `logOutput` | `&Serial` | `Print*` target for logs. Set to `nullptr` to silence logging. |
| `fallbackToFactory` | `false` | Enable fallback to a factory partition when rollback to the previous OTA slot is impossible. |
| `factoryLabel` | `"factory"` | Partition label used for factory fallback (`Options::fallbackToFactory` must be `true`). |
| `maxRollbackAttempts` | `1` | Caps consecutive rollbacks without a successful `markHealthyNow()`. `0` removes the guard. |
| `swResetCountsAsCrash` | `false` | Treat `ESP_RST_SW` as suspicious when `true`. |
| `brownoutCountsAsCrash` | `false` | Treat `ESP_RST_BROWNOUT` as suspicious when `true`. |

### Helper Methods
- `setOptions(const Options&)`: Apply the structure above before calling `beginEarly()`.
- `setSuspiciousResetPredicate(ResetReasonPredicate)`: Override reset classification entirely when necessary.

---

## Compile-Time Flags
Add overrides via `platformio.ini` `build_flags` or Arduino IDE `-D` definitions.

| Flag | Default | Purpose |
| --- | --- | --- |
| `CRG_NAMESPACE` | `"crg"` | Default namespace used when `Options::nvsNamespace` is untouched. |
| `CRG_FAIL_LIMIT` | `3` | Default `failLimit`. |
| `CRG_STABLE_TIME_MS` | `60000UL` | Default `stableTimeMs`. |
| `CRG_AUTOSAVE_PREV_SLOT` | `0` | Default value for `autoSavePrevSlot`. |
| `CRG_LOG_ENABLED` | `1` | Compile-time logging toggle. Set to `0` to strip all logging logic. |
| `CRG_FEATURE_FACTORY_FALLBACK` | `1` | Remove factory fallback support when `0`. |
| `CRG_FEATURE_STABLE_TICK` | `1` | Remove `loopTick()` auto-health logic when `0`. |
| `CRG_FEATURE_PENDING_VERIFY_FIX` | `1` | Disable OTA state inspection/repair when `0`. |
| `CRG_LABEL_BUFFER_SIZE` | `ESP_PARTITION_LABEL_MAX_LEN + 1` | Buffer size for partition labels stored in NVS. |
| `CRG_LOG_BUFFER_SIZE` | `192` | Size of the temporary log buffer used by `log()`. |
| `CRG_NAMESPACE_MAX_LEN` | `15` | Maximum namespace length for the internal fixed buffer. |

---

## Integration Checklist
1. Decide on your `nvsNamespace` and ensure it fits within `CRG_NAMESPACE_MAX_LEN`.
2. Configure `failLimit`, `stableTimeMs`, and `maxRollbackAttempts` based on your crash tolerance.
3. If you rely on factory fallback, confirm that the `factoryLabel` exists in your `partitions.csv` and that `CRG_FEATURE_FACTORY_FALLBACK` remains enabled.
4. Select a logging destination (`logOutput`). For silent builds set it to `nullptr` or disable logging with `CRG_LOG_ENABLED=0`.
5. Review compile-time flags when optimizing for flash/RAM or when removing unused features.

With these knobs tuned correctly, CrashRollbackGuard can be transplanted between projects without re-auditing the source each time.
