# OTA Workflow with CrashRollbackGuard

## Recommended OTA Sequence

1. While running known-good firmware:
   - Call `saveCurrentAsPreviousSlot()`.

2. Before rebooting into the new image:
   - Call `armControlledRestart()`.

3. Restart the device.

4. In `setup()` of the new firmware:
   - Call `beginEarly()` as soon as possible.

5. After all services are stable:
   - Call `markHealthyNow()`.

## Pending Verify Handling
If the new image boots in `ESP_OTA_IMG_PENDING_VERIFY`:
- the guard defers marking it valid,
- once healthy, it calls `esp_ota_mark_app_valid_cancel_rollback()`.

## Failure Scenarios Covered
- Power loss during OTA
- Crash before health mark
- Brownout loops
- Partial NVS writes

## Anti-Patterns
❌ Calling `markHealthyNow()` immediately on boot  
❌ Rebooting without `armControlledRestart()`  
❌ Using OTA rollback without saving the previous slot