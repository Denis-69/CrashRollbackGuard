#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"

#ifndef ESP_PARTITION_LABEL_MAX_LEN
  // IDF 4.x+ defines this (16). Arduino cores may miss it, so guard here.
  #define ESP_PARTITION_LABEL_MAX_LEN 16
#endif

//==================== Compile-time defaults ====================
// Можно переопределить через build_flags: -D CRG_FAIL_LIMIT=3 и т.п.

#ifndef CRG_NAMESPACE
  #define CRG_NAMESPACE "crg"
#endif

#ifndef CRG_FAIL_LIMIT
  #define CRG_FAIL_LIMIT 3
#endif

#ifndef CRG_STABLE_TIME_MS
  #define CRG_STABLE_TIME_MS 60000UL
#endif

#ifndef CRG_AUTOSAVE_PREV_SLOT
  #define CRG_AUTOSAVE_PREV_SLOT 0
#endif

#ifndef CRG_LOG_ENABLED
  #define CRG_LOG_ENABLED 1
#endif

#ifndef CRG_FEATURE_FACTORY_FALLBACK
  // 0 — полностью вырезать код factory fallback ради экономии.
  #define CRG_FEATURE_FACTORY_FALLBACK 1
#endif

#ifndef CRG_FEATURE_STABLE_TICK
  // 0 — loopTick() станет no-op, если нужен максимально лёгкий бинарник.
  #define CRG_FEATURE_STABLE_TICK 1
#endif

#ifndef CRG_FEATURE_PENDING_VERIFY_FIX
  // 0 — не будем читать OTA state (меньше кода, но меньше страховка).
  #define CRG_FEATURE_PENDING_VERIFY_FIX 1
#endif

#ifndef CRG_LABEL_BUFFER_SIZE
  // Минимум ESP_PARTITION_LABEL_MAX_LEN+1, можно увеличить для совместимости.
  #define CRG_LABEL_BUFFER_SIZE (ESP_PARTITION_LABEL_MAX_LEN + 1)
#endif

#ifndef CRG_LOG_BUFFER_SIZE
  // Размер временного стека для log(); больше = длиннее сообщения.
  #define CRG_LOG_BUFFER_SIZE 192
#endif

#ifndef CRG_NAMESPACE_MAX_LEN
  // Максимальная длина namespace в NVS (без учёта терминатора).
  #define CRG_NAMESPACE_MAX_LEN 15
#endif

namespace crg {

enum class LogLevel : uint8_t {
  None  = 0,
  Error = 1,
  Info  = 2,
  Debug = 3
};

struct Options {
  const char* nvsNamespace      = CRG_NAMESPACE;
  uint32_t    failLimit         = CRG_FAIL_LIMIT;
  uint32_t    stableTimeMs      = CRG_STABLE_TIME_MS;

  // Если true — beginEarly() сам сохранит текущий слот как "prev",
  // но обычно лучше вызывать saveCurrentAsPreviousSlot() перед OTA.
  bool        autoSavePrevSlot  = (CRG_AUTOSAVE_PREV_SLOT != 0);

  LogLevel    logLevel          = (CRG_LOG_ENABLED ? LogLevel::Info : LogLevel::None);
  Print*      logOutput         = &Serial;

  // Если true — при достижении failLimit будет пытаться fallback на factory,
  // если prev-slot не задан или недоступен.
  bool        fallbackToFactory = false;

  // Label для factory, если используешь fallbackToFactory
  const char* factoryLabel      = "factory";

  // Ограничивает количество подряд rollback без успешной health-mark.
  uint8_t     maxRollbackAttempts = 1; // 0 = без лимита, но ping-pong риск.

  // Политика reset reason по умолчанию.
  bool        swResetCountsAsCrash      = false; // ESP_RST_SW
  bool        brownoutCountsAsCrash     = false; // ESP_RST_BROWNOUT
};

enum class Decision : uint8_t {
  None,
  RollbackToPrev,
  RollbackToFactory,
  SkippedNoPrev,
  SkippedSameSlot,
  FailedSwitch
};

// Пользовательский фильтр reset reason
using ResetReasonPredicate = bool (*)(esp_reset_reason_t);

class CrashRollbackGuard {
public:
  CrashRollbackGuard();

  // Настройка (можно вызывать до beginEarly)
  void setOptions(const Options& opt);
  const Options& options() const;

  // Если хочешь своё правило "подозрительности" reset reason
  void setSuspiciousResetPredicate(ResetReasonPredicate pred);

  // Вызывать рано в setup()
  // Возвращает решение (например, выполнялся rollback или нет)
  Decision beginEarly();

  // Вызвать когда система "точно жива" (после WiFi/MQTT/Web)
  void markHealthyNow();

  // Авто-сброс по времени "стабильной" работы: вызови в loop()
  void loopTick();

  // Пометить, что следующий перезапуск через esp_restart()/ESP.restart() ожидаем и не считаем фейлом.
  void armControlledRestart();

  // Сохранить текущий running slot label как "previous"
  bool saveCurrentAsPreviousSlot();

  // Получить сохранённый prev slot label
  bool getPreviousSlot(char* out, size_t len) const;
  String getPreviousSlot() const;

  // Сбросить prev slot (если нужно)
  void clearPreviousSlot();

  // Получить текущий running slot label
  static bool getRunningLabel(char* out, size_t len);
  static String getRunningLabel();

  // Полезные данные
  esp_reset_reason_t lastResetReason() const;
  uint32_t failCount() const;
  bool pendingVerifyState() const { return pendingVerify_; }
  Print* logOutput() const { return opt_.logOutput; }

private:
  Options opt_ = Options{};
  Preferences prefs_;
  ResetReasonPredicate suspiciousPred_ = nullptr;

  bool healthyMarked_ = false;
  esp_reset_reason_t resetReason_ = ESP_RST_UNKNOWN;
  bool pendingVerify_ = false;
  uint32_t stableStartMs_ = 0;

#if CRG_FEATURE_PENDING_VERIFY_FIX
  esp_ota_img_states_t runningImgState_ = ESP_OTA_IMG_UNDEFINED;
#endif

  char ownedNamespace_[CRG_NAMESPACE_MAX_LEN + 1] = {0};
  char ownedFactoryLabel_[CRG_LABEL_BUFFER_SIZE] = {0};

  // NVS keys
  static constexpr const char* K_FAILS      = "fails";
  static constexpr const char* K_FAILS_INV  = "failsInv";
  static constexpr const char* K_PREV_LABEL = "prev";
  static constexpr const char* K_PREV_CRC   = "prevCrc";
  static constexpr const char* K_ROLL_COUNT = "rbCnt";
  static constexpr const char* K_ROLL_COUNT_INV = "rbCntInv";
  static constexpr const char* K_PENDING_ACT = "pendAct";
  static constexpr const char* K_PENDING_LABEL = "pendLbl";
  static constexpr const char* K_PENDING_CRC = "pendCrc";

  enum class PendingAction : uint8_t {
    None = 0,
    RollbackPrev,
    RollbackFactory,
    ControlledRestart
  };

  enum class LabelStatus : uint8_t {
    Missing,
    Ok,
    Corrupted
  };

  bool isSuspicious(esp_reset_reason_t r) const;

  void log(LogLevel lvl, const char* fmt, ...) const;

  Decision attemptRollback_(Preferences& store, const char* why);
  Decision tryFactoryFallback_(Preferences& store, Decision failureDecision, const char* cause);

  static bool switchBootPartitionByLabel_(const char* label);
  static const esp_partition_t* findAppPartitionByLabel_(const char* label);

  static void copyLabel_(char* dst, size_t len, const char* src);
  static bool readRunningLabel_(char* out, size_t len);
  static bool storeLabelPref_(Preferences& store, const char* key, const char* value);

  static uint32_t crc32_(const void* data, size_t len);
  bool storeLabelWithCrc_(Preferences& store,
                          const char* labelKey,
                          const char* crcKey,
                          const char* value) const;
  static LabelStatus loadLabelWithCrc_(Preferences& store,
                                       const char* labelKey,
                                       const char* crcKey,
                                       char* out,
                                       size_t len);

  uint32_t readFailCounter_(Preferences& store, bool allowRepair = true) const;
  void writeFailCounter_(Preferences& store, uint32_t value) const;
  void resetFailCounter_(Preferences& store) const;

  uint8_t readRollbackCount_(Preferences& store, bool allowRepair = true) const;
  void writeRollbackCount_(Preferences& store, uint8_t value) const;
  void resetRollbackCount_(Preferences& store) const;
  void bumpRollbackCount_(Preferences& store) const;

  void storePendingAction_(Preferences& store, PendingAction action, const char* label) const;
  PendingAction readPendingAction_(Preferences& store, char* labelBuf, size_t bufLen) const;
  void clearPendingAction_(Preferences& store) const;
};

} // namespace crg
