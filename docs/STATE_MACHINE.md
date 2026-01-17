# CrashRollbackGuard — State Machine

## High-Level Flow

```text
Boot
 ├─ Pending Action?
 │   ├─ ControlledRestart → clear + trust
 │   ├─ RollbackPrev       → validate + switch
 │   └─ RollbackFactory    → validate + switch
 │
 ├─ OTA Image State?
 │   ├─ PENDING_VERIFY → wait for health
 │   ├─ INVALID        → immediate rollback
 │   └─ VALID          → continue
 │
 ├─ Reset Reason Analysis
 │   ├─ Suspicious → increment fail counter
 │   │     └─ limit exceeded → rollback
 │   └─ Benign → clear counters
 │
 └─ Healthy Runtime
       └─ markHealthyNow()