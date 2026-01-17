# Failure Modes and Recovery

| Scenario | Outcome |
|--------|--------|
| Power loss during OTA | Previous slot preserved |
| Crash before health mark | Fail counter increments |
| Reboot storm | Rollback after limit |
| Ping-pong rollback | Stopped by rollback guard |
| NVS corruption | Auto-repair or clear |
| Factory missing | Safe fallback disabled |
| Brownout loop | Optional crash classification |

## What Cannot Be Fixed
- Corrupted bootloader
- Invalid partition table
- Flash hardware failure

CrashRollbackGuard assumes the ESP32 ROM bootloader is intact.
