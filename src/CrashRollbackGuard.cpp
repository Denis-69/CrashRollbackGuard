#include "CrashRollbackGuard.h"
#include <cstdarg>
#include <cstring>

namespace crg {

CrashRollbackGuard::CrashRollbackGuard() {
  setOptions(Options{});
}

void CrashRollbackGuard::setOptions(const Options& opt) {
  opt_ = opt;

  if (opt.nvsNamespace && opt.nvsNamespace[0]) {
    copyLabel_(ownedNamespace_, sizeof(ownedNamespace_), opt.nvsNamespace);
  } else {
    copyLabel_(ownedNamespace_, sizeof(ownedNamespace_), CRG_NAMESPACE);
  }
  opt_.nvsNamespace = ownedNamespace_;

  if (opt.factoryLabel && opt.factoryLabel[0]) {
    copyLabel_(ownedFactoryLabel_, sizeof(ownedFactoryLabel_), opt.factoryLabel);
  } else if (opt_.fallbackToFactory) {
    copyLabel_(ownedFactoryLabel_, sizeof(ownedFactoryLabel_), "factory");
  } else {
    ownedFactoryLabel_[0] = '\0';
  }
  opt_.factoryLabel = (ownedFactoryLabel_[0] == '\0') ? nullptr : ownedFactoryLabel_;

  if (!opt_.logOutput) {
    opt_.logOutput = &Serial;
  }

#if CRG_FEATURE_FACTORY_FALLBACK
  if (opt_.fallbackToFactory) {
    if (!opt_.factoryLabel || !findAppPartitionByLabel_(opt_.factoryLabel)) {
      log(LogLevel::Error,
          "[CRG] factory fallback disabled: partition '%s' not found.\n",
          opt_.factoryLabel ? opt_.factoryLabel : "<unset>");
      opt_.fallbackToFactory = false;
    }
  }
#endif
}
const Options& CrashRollbackGuard::options() const { return opt_; }

void CrashRollbackGuard::setSuspiciousResetPredicate(ResetReasonPredicate pred) {
  suspiciousPred_ = pred;
}

esp_reset_reason_t CrashRollbackGuard::lastResetReason() const { return resetReason_; }

uint32_t CrashRollbackGuard::failCount() const {
  // prefs_ может быть не открыт до beginEarly(), поэтому читаем безопасно
  Preferences tmp;
  if (!tmp.begin(opt_.nvsNamespace, true)) return 0;
  const uint32_t v = readFailCounter_(tmp, false);
  tmp.end();
  return v;
}

bool CrashRollbackGuard::isSuspicious(esp_reset_reason_t r) const {
  if (suspiciousPred_) return suspiciousPred_(r);

  // Default policy: "нормальные" не считаем фейлом, остальное считаем подозрительным
  switch (r) {
    case ESP_RST_POWERON:
    case ESP_RST_EXT:
      return false;
    case ESP_RST_SW:
      return opt_.swResetCountsAsCrash;
    case ESP_RST_BROWNOUT:
      return opt_.brownoutCountsAsCrash;
    default:
      return true;
  }
}

void CrashRollbackGuard::log(LogLevel lvl, const char* fmt, ...) const {
  if ((uint8_t)opt_.logLevel < (uint8_t)lvl || lvl == LogLevel::None) return;

  if (!opt_.logOutput) return;
  char buf[CRG_LOG_BUFFER_SIZE];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  opt_.logOutput->print(buf);
}

bool CrashRollbackGuard::getRunningLabel(char* out, size_t len) {
  return readRunningLabel_(out, len);
}

String CrashRollbackGuard::getRunningLabel() {
  char label[CRG_LABEL_BUFFER_SIZE];
  if (!getRunningLabel(label, sizeof(label))) {
    return String();
  }
  return String(label);
}

const esp_partition_t* CrashRollbackGuard::findAppPartitionByLabel_(const char* label) {
  if (!label || !label[0]) return nullptr;
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
}

bool CrashRollbackGuard::switchBootPartitionByLabel_(const char* label) {
  const esp_partition_t* p = findAppPartitionByLabel_(label);
  if (!p) return false;
  return esp_ota_set_boot_partition(p) == ESP_OK;
}

void CrashRollbackGuard::copyLabel_(char* dst, size_t len, const char* src) {
  if (!dst || len == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  const size_t maxCopy = (len > 0) ? len - 1 : 0;
  if (maxCopy == 0) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, maxCopy);
  dst[maxCopy] = '\0';
}

bool CrashRollbackGuard::readRunningLabel_(char* out, size_t len) {
  if (!out || len == 0) return false;
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    out[0] = '\0';
    return false;
  }
  copyLabel_(out, len, running->label);
  return true;
}

bool CrashRollbackGuard::storeLabelPref_(Preferences& store, const char* key, const char* value) {
  if (!key || !value) return false;
  return store.putString(key, value) > 0;
}
uint32_t CrashRollbackGuard::crc32_(const void* data, size_t len) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  while (len--) {
    crc ^= *ptr++;
    for (uint8_t i = 0; i < 8; ++i) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

bool CrashRollbackGuard::storeLabelWithCrc_(Preferences& store,
                                            const char* labelKey,
                                            const char* crcKey,
                                            const char* value) const {
  if (!labelKey || !crcKey || !value) return false;
  if (!storeLabelPref_(store, labelKey, value)) {
    log(LogLevel::Error, "[CRG] Failed to write label '%s'.\n", labelKey);
    store.remove(labelKey);
    store.remove(crcKey);
    return false;
  }
  const uint32_t crc = crc32_(value, strlen(value));
  if (store.putUInt(crcKey, crc) == 0) {
    store.remove(labelKey);
    store.remove(crcKey);
    log(LogLevel::Error, "[CRG] Failed to write CRC for '%s'.\n", labelKey);
    return false;
  }
  return true;
}

CrashRollbackGuard::LabelStatus CrashRollbackGuard::loadLabelWithCrc_(Preferences& store,
                                                                      const char* labelKey,
                                                                      const char* crcKey,
                                                                      char* out,
                                                                      size_t len) {
  if (!labelKey || !crcKey || !out || len == 0) return LabelStatus::Missing;
  out[0] = '\0';
  const size_t got = store.getString(labelKey, out, len);
  if (got == 0) {
    return LabelStatus::Missing;
  }

  const uint32_t storedCrc = store.getUInt(crcKey, 0);
  if (!store.isKey(crcKey)) {
    out[0] = '\0';
    return LabelStatus::Corrupted;
  }
  const uint32_t calcCrc   = crc32_(out, strlen(out));
  if (storedCrc != calcCrc) {
    out[0] = '\0';
    return LabelStatus::Corrupted;
  }

  return LabelStatus::Ok;
}

uint32_t CrashRollbackGuard::readFailCounter_(Preferences& store, bool allowRepair) const {
  const uint32_t primary = store.getUInt(K_FAILS, 0);
  const uint32_t mirror  = store.getUInt(K_FAILS_INV, primary ^ 0xFFFFFFFFu);
  if ((primary ^ mirror) != 0xFFFFFFFFu) {
    log(LogLevel::Error, "[CRG] fail counter corrupted (0x%08x vs 0x%08x).\n", primary, mirror);
    if (allowRepair) {
      writeFailCounter_(store, 0);
    }
    return 0;
  }
  return primary;
}

void CrashRollbackGuard::writeFailCounter_(Preferences& store, uint32_t value) const {
  store.putUInt(K_FAILS, value);
  store.putUInt(K_FAILS_INV, value ^ 0xFFFFFFFFu);
}

void CrashRollbackGuard::resetFailCounter_(Preferences& store) const {
  writeFailCounter_(store, 0);
}

uint8_t CrashRollbackGuard::readRollbackCount_(Preferences& store, bool allowRepair) const {
  const uint8_t primary = store.getUChar(K_ROLL_COUNT, 0);
  const uint8_t mirror  = store.getUChar(K_ROLL_COUNT_INV, primary ^ 0xFFu);
  if ((uint8_t)(primary ^ mirror) != 0xFFu) {
    log(LogLevel::Error, "[CRG] rollback counter corrupted (%u/%u).\n", primary, mirror);
    if (allowRepair) {
      writeRollbackCount_(store, 0);
    }
    return 0;
  }
  return primary;
}

void CrashRollbackGuard::writeRollbackCount_(Preferences& store, uint8_t value) const {
  store.putUChar(K_ROLL_COUNT, value);
  store.putUChar(K_ROLL_COUNT_INV, value ^ 0xFFu);
}

void CrashRollbackGuard::resetRollbackCount_(Preferences& store) const {
  writeRollbackCount_(store, 0);
}

void CrashRollbackGuard::bumpRollbackCount_(Preferences& store) const {
  const uint8_t current = readRollbackCount_(store);
  if (current != 0xFFu) {
    writeRollbackCount_(store, current + 1);
  }
}

void CrashRollbackGuard::storePendingAction_(Preferences& store, PendingAction action, const char* label) const {
  // Ensure action is cleared before writing label data so partially written labels
  // never pair with a stale PendingAction value.
  if (store.putUChar(K_PENDING_ACT, static_cast<uint8_t>(PendingAction::None)) == 0) {
    log(LogLevel::Error, "[CRG] Failed to clear pending action flag.\n");
    return;
  }

  if (label && label[0]) {
    if (!storeLabelWithCrc_(store, K_PENDING_LABEL, K_PENDING_CRC, label)) {
      store.remove(K_PENDING_LABEL);
      store.remove(K_PENDING_CRC);
      return;
    }
  } else {
    store.remove(K_PENDING_LABEL);
    store.remove(K_PENDING_CRC);
  }

  if (store.putUChar(K_PENDING_ACT, static_cast<uint8_t>(action)) == 0) {
    log(LogLevel::Error, "[CRG] Failed to write pending action flag.\n");
    store.remove(K_PENDING_LABEL);
    store.remove(K_PENDING_CRC);
  }
}

CrashRollbackGuard::PendingAction CrashRollbackGuard::readPendingAction_(Preferences& store,
                                                                         char* labelBuf,
                                                                         size_t bufLen) const {
  if (labelBuf && bufLen > 0) {
    labelBuf[0] = '\0';
  }
  const uint8_t raw = store.getUChar(K_PENDING_ACT, 0);
  if (raw > static_cast<uint8_t>(PendingAction::ControlledRestart)) {
    log(LogLevel::Error, "[CRG] Pending action value invalid (%u).\n", raw);
    clearPendingAction_(store);
    return PendingAction::None;
  }

  const PendingAction action = static_cast<PendingAction>(raw);
  if (action == PendingAction::None) {
    if (store.isKey(K_PENDING_LABEL) || store.isKey(K_PENDING_CRC)) {
      store.remove(K_PENDING_LABEL);
      store.remove(K_PENDING_CRC);
    }
    return PendingAction::None;
  }

  if ((!labelBuf || bufLen == 0) && action != PendingAction::ControlledRestart) {
    log(LogLevel::Error, "[CRG] Pending action %u requires label buffer. Clearing.\n", raw);
    clearPendingAction_(store);
    return PendingAction::None;
  }

  if (labelBuf && bufLen > 0) {
    const LabelStatus status = loadLabelWithCrc_(store, K_PENDING_LABEL, K_PENDING_CRC, labelBuf, bufLen);
    if (status != LabelStatus::Ok) {
      log(LogLevel::Error,
          "[CRG] Pending action label invalid (status=%u, act=%u).\n",
          static_cast<unsigned>(status),
          static_cast<unsigned>(action));
      if (action == PendingAction::ControlledRestart) {
        labelBuf[0] = '\0';
        return action; // treat as valid controlled restart without label
      }
      clearPendingAction_(store);
      return PendingAction::None;
    }
  }

  return action;
}

void CrashRollbackGuard::clearPendingAction_(Preferences& store) const {
  store.putUChar(K_PENDING_ACT, static_cast<uint8_t>(PendingAction::None));
  store.remove(K_PENDING_LABEL);
  store.remove(K_PENDING_CRC);
}

bool CrashRollbackGuard::saveCurrentAsPreviousSlot() {
  Preferences writer;
  if (!writer.begin(opt_.nvsNamespace, false)) return false;

  char label[CRG_LABEL_BUFFER_SIZE];
  if (!readRunningLabel_(label, sizeof(label))) {
    writer.end();
    return false;
  }

  const bool ok = storeLabelWithCrc_(writer, K_PREV_LABEL, K_PREV_CRC, label);
  if (ok) {
    resetRollbackCount_(writer);
    log(LogLevel::Info, "[CRG] Saved prev slot: %s\n", label);
  }

  writer.end();
  return ok;
}

bool CrashRollbackGuard::getPreviousSlot(char* out, size_t len) const {
  if (!out || len == 0) return false;
  out[0] = '\0';

  Preferences reader;
  if (!reader.begin(opt_.nvsNamespace, true)) return false;

  const LabelStatus status = loadLabelWithCrc_(reader, K_PREV_LABEL, K_PREV_CRC, out, len);
  reader.end();

  if (status == LabelStatus::Corrupted) {
    log(LogLevel::Error, "[CRG] Stored prev slot label corrupted. Clearing.\n");
    Preferences writer;
    if (writer.begin(opt_.nvsNamespace, false)) {
      writer.remove(K_PREV_LABEL);
      writer.remove(K_PREV_CRC);
      writer.end();
    }
    out[0] = '\0';
    return false;
  }

  if (status != LabelStatus::Ok) {
    out[0] = '\0';
    return false;
  }

  return true;
}

String CrashRollbackGuard::getPreviousSlot() const {
  char label[CRG_LABEL_BUFFER_SIZE];
  if (!getPreviousSlot(label, sizeof(label))) {
    return String();
  }
  return String(label);
}

void CrashRollbackGuard::clearPreviousSlot() {
  Preferences writer;
  if (!writer.begin(opt_.nvsNamespace, false)) return;
  writer.remove(K_PREV_LABEL);
  writer.remove(K_PREV_CRC);
  resetRollbackCount_(writer);
  writer.end();
}

void CrashRollbackGuard::markHealthyNow() {
  if (healthyMarked_) return;
  if (!prefs_.begin(opt_.nvsNamespace, false)) return;

  const uint32_t fails = readFailCounter_(prefs_);
  const uint8_t rbCnt  = readRollbackCount_(prefs_);
#if CRG_FEATURE_PENDING_VERIFY_FIX
  const bool needOtaMark = pendingVerify_;
#else
  const bool needOtaMark = false;
#endif

  if (fails == 0 && rbCnt == 0 && !needOtaMark) {
    prefs_.end();
    healthyMarked_ = true;
    log(LogLevel::Debug, "[CRG] markHealthyNow() skipped (already clean).\n");
    return;
  }

  resetFailCounter_(prefs_);
  resetRollbackCount_(prefs_);
  prefs_.end();

#if CRG_FEATURE_PENDING_VERIFY_FIX
  if (pendingVerify_) {
    const esp_err_t res = esp_ota_mark_app_valid_cancel_rollback();
    if (res == ESP_OK) {
      log(LogLevel::Info, "[CRG] OTA image marked VALID.\n");
    } else {
      log(LogLevel::Error, "[CRG] Failed to mark OTA VALID (%d).\n", (int)res);
    }
    pendingVerify_ = false;
    runningImgState_ = ESP_OTA_IMG_VALID;
  }
#endif

  healthyMarked_ = true;
  log(LogLevel::Info, "[CRG] Marked healthy. fails reset.\n");
}

void CrashRollbackGuard::loopTick() {
#if CRG_FEATURE_STABLE_TICK
  if (healthyMarked_ || opt_.stableTimeMs == 0) return;
  if ((uint32_t)(millis() - stableStartMs_) >= opt_.stableTimeMs) {
    markHealthyNow();
  }
#else
  // Feature disabled to save flash/RAM; keep method as a no-op.
  (void)healthyMarked_;
#endif
}

void CrashRollbackGuard::armControlledRestart() {
  Preferences writer;
  if (!writer.begin(opt_.nvsNamespace, false)) return;

  char label[CRG_LABEL_BUFFER_SIZE];
  const bool hasLabel = readRunningLabel_(label, sizeof(label));
  storePendingAction_(writer, PendingAction::ControlledRestart, hasLabel ? label : nullptr);
  if (hasLabel) {
    log(LogLevel::Debug, "[CRG] Controlled restart armed for %s.\n", label);
  } else {
    log(LogLevel::Error, "[CRG] Controlled restart armed without label (partition lookup failed).\n");
  }

  writer.end();
}

Decision CrashRollbackGuard::attemptRollback_(Preferences& store, const char* why) {
  char current[CRG_LABEL_BUFFER_SIZE];
  char prev[CRG_LABEL_BUFFER_SIZE];
  current[0] = '\0';
  prev[0] = '\0';

  readRunningLabel_(current, sizeof(current));
  const LabelStatus prevStatus = loadLabelWithCrc_(store, K_PREV_LABEL, K_PREV_CRC, prev, sizeof(prev));
  if (prevStatus == LabelStatus::Corrupted) {
    log(LogLevel::Error, "[CRG] Previous slot label corrupted in NVS.\n");
    store.remove(K_PREV_LABEL);
    store.remove(K_PREV_CRC);
    prev[0] = '\0';
  }

  log(LogLevel::Error,
      "[CRG] %s. fails=%u current=%s prev=%s rr=%d\n",
      why ? why : "rollback",
      (unsigned)readFailCounter_(store),
      current,
      prev,
      (int)resetReason_);

  if (prev[0] == '\0') {
    log(LogLevel::Error, "[CRG] No previous slot stored.\n");
    return tryFactoryFallback_(store, Decision::SkippedNoPrev, "No previous slot");
  }

  if (strcmp(prev, current) == 0) {
    log(LogLevel::Error, "[CRG] Previous slot matches current (%s).\n", current);
    return tryFactoryFallback_(store, Decision::SkippedSameSlot, "Prev matches current");
  }

  const esp_partition_t* prevPartition = findAppPartitionByLabel_(prev);
  if (!prevPartition) {
    log(LogLevel::Error, "[CRG] Prev slot '%s' partition missing.\n", prev);
    return tryFactoryFallback_(store, Decision::SkippedNoPrev, "Partition missing");
  }

#if CRG_FEATURE_PENDING_VERIFY_FIX
  esp_ota_img_states_t prevState = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(prevPartition, &prevState) == ESP_OK) {
    if (prevState == ESP_OTA_IMG_INVALID || prevState == ESP_OTA_IMG_ABORTED) {
      log(LogLevel::Error, "[CRG] Prev slot '%s' marked INVALID.\n", prev);
      return tryFactoryFallback_(store, Decision::FailedSwitch, "Prev slot invalid");
    }
  }
#endif

  storePendingAction_(store, PendingAction::RollbackPrev, prev);
  if (switchBootPartitionByLabel_(prev)) {
    bumpRollbackCount_(store);
    log(LogLevel::Error, "[CRG] Switch boot to '%s' and reboot.\n", prev);
    esp_restart(); // Does not return.
    return Decision::RollbackToPrev;
  }

  clearPendingAction_(store);
  log(LogLevel::Error, "[CRG] Failed to switch to '%s'.\n", prev);
  return tryFactoryFallback_(store, Decision::FailedSwitch, "Failed to switch to prev slot");
}

Decision CrashRollbackGuard::beginEarly() {
  resetReason_ = esp_reset_reason();
  healthyMarked_ = false;
  stableStartMs_ = millis();

#if CRG_FEATURE_PENDING_VERIFY_FIX
  pendingVerify_ = false;
  runningImgState_ = ESP_OTA_IMG_UNDEFINED;
  if (const esp_partition_t* running = esp_ota_get_running_partition()) {
    if (esp_ota_get_state_partition(running, &runningImgState_) == ESP_OK) {
      pendingVerify_ = (runningImgState_ == ESP_OTA_IMG_PENDING_VERIFY);
      if (runningImgState_ == ESP_OTA_IMG_INVALID) {
        log(LogLevel::Error, "[CRG] Running slot marked INVALID.\n");
      }
    }
  }
#else
  pendingVerify_ = false;
#endif

  if (!prefs_.begin(opt_.nvsNamespace, false)) {
    log(LogLevel::Error, "[CRG] NVS open failed\n");
    return Decision::None;
  }

  uint32_t fails = readFailCounter_(prefs_);
  char runningLabel[CRG_LABEL_BUFFER_SIZE];
  readRunningLabel_(runningLabel, sizeof(runningLabel));

  bool pendingBoot = false;
  char pendingLabel[CRG_LABEL_BUFFER_SIZE];
  const PendingAction pendingAction = readPendingAction_(prefs_, pendingLabel, sizeof(pendingLabel));
  if (pendingAction != PendingAction::None) {
    const bool labelPresent = (pendingLabel[0] != '\0');
    const bool labelMatches = labelPresent && runningLabel[0] != '\0' && strcmp(pendingLabel, runningLabel) == 0;

    if (pendingAction == PendingAction::ControlledRestart) {
      pendingBoot = true;
      clearPendingAction_(prefs_);
      resetFailCounter_(prefs_);
      fails = 0;
      if (labelPresent && !labelMatches) {
        log(LogLevel::Error,
            "[CRG] Controlled restart label mismatch (stored=%s running=%s).\n",
            pendingLabel,
            runningLabel);
      } else if (!labelPresent) {
        log(LogLevel::Error, "[CRG] Controlled restart label missing, trusting user intent.\n");
      } else {
        log(LogLevel::Info,
            "[CRG] Controlled restart completed on %s.\n",
            runningLabel);
      }
    } else if (labelMatches) {
      pendingBoot = true;
      clearPendingAction_(prefs_);
      resetFailCounter_(prefs_);
      fails = 0;
      log(LogLevel::Info,
          "[CRG] Pending action %u completed on %s.\n",
          static_cast<unsigned>(pendingAction),
          runningLabel);
    } else {
      log(LogLevel::Error,
          "[CRG] Pending action %u mismatch (stored=%s running=%s).\n",
          static_cast<unsigned>(pendingAction),
          pendingLabel,
          runningLabel);
      clearPendingAction_(prefs_);
    }
  }

  if (opt_.autoSavePrevSlot) {
    char prev[CRG_LABEL_BUFFER_SIZE];
    const LabelStatus status = loadLabelWithCrc_(prefs_, K_PREV_LABEL, K_PREV_CRC, prev, sizeof(prev));
    if (status == LabelStatus::Missing && runningLabel[0] != '\0') {
      if (storeLabelWithCrc_(prefs_, K_PREV_LABEL, K_PREV_CRC, runningLabel)) {
        resetRollbackCount_(prefs_);
        log(LogLevel::Debug, "[CRG] Auto-saved prev slot: %s\n", runningLabel);
      }
    } else if (status == LabelStatus::Corrupted) {
      log(LogLevel::Error, "[CRG] Auto-saved prev slot corrupted. Clearing.\n");
      prefs_.remove(K_PREV_LABEL);
      prefs_.remove(K_PREV_CRC);
    }
  }

  const bool suspicious = !pendingBoot && isSuspicious(resetReason_);

  if (!suspicious) {
    if (fails != 0) writeFailCounter_(prefs_, 0);
    prefs_.end();
    return Decision::None;
  }

#if CRG_FEATURE_PENDING_VERIFY_FIX
  if (!pendingBoot && runningImgState_ == ESP_OTA_IMG_INVALID) {
    const Decision d = attemptRollback_(prefs_, "Running image invalid");
    prefs_.end();
    return d;
  }
#endif

  if (fails < 0xFFFFFFFFu) {
    const uint32_t cap = (opt_.failLimit > 0) ? opt_.failLimit : 0xFFFFFFFFu;
    if (fails < cap) {
      ++fails;
      writeFailCounter_(prefs_, fails);
    }
  }

  if (fails >= opt_.failLimit && opt_.failLimit > 0) {
    if (opt_.maxRollbackAttempts > 0) {
      const uint8_t guard = readRollbackCount_(prefs_);
      if (guard >= opt_.maxRollbackAttempts) {
        log(LogLevel::Error, "[CRG] Rollback guard hit (%u >= %u).\n", guard, opt_.maxRollbackAttempts);
        const Decision guarded = tryFactoryFallback_(prefs_, Decision::SkippedNoPrev, "Rollback guard active");
        prefs_.end();
        return guarded;
      }
    }

    const Decision d = attemptRollback_(prefs_, "Crash-loop limit reached");
    prefs_.end();
    return d;
  }

  prefs_.end();
  return Decision::None;
}

Decision CrashRollbackGuard::tryFactoryFallback_(Preferences& store, Decision failureDecision, const char* cause) {
#if !CRG_FEATURE_FACTORY_FALLBACK
  (void)cause;
  return failureDecision;
#else
  if (!opt_.fallbackToFactory || !opt_.factoryLabel || !opt_.factoryLabel[0]) {
    return failureDecision;
  }

  log(LogLevel::Error,
      "[CRG] %s -> fallback to factory '%s'.\n",
      cause ? cause : "Fallback",
      opt_.factoryLabel);

  storePendingAction_(store, PendingAction::RollbackFactory, opt_.factoryLabel);
  if (switchBootPartitionByLabel_(opt_.factoryLabel)) {
    esp_restart();
    return Decision::RollbackToFactory;
  }

  clearPendingAction_(store);
  log(LogLevel::Error, "[CRG] Factory switch failed for '%s'.\n", opt_.factoryLabel);
  return Decision::FailedSwitch;
#endif
}

} // namespace crg
