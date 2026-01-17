// Basic CrashRollbackGuard example: protects setup() and performs a controlled
// restart once the system is stable.

#include <Arduino.h>
#include <CrashRollbackGuard.h>

crg::CrashRollbackGuard guard;

constexpr uint32_t HEARTBEAT_MS = 2000;
constexpr uint32_t CONTROLLED_RESTART_MS = 45000;

uint32_t lastHeartbeatMs = 0;
bool restartScheduled = false;

// Handy helper for readable logs in Serial Monitor.
const char* decisionToString(crg::Decision d) {
	switch (d) {
		case crg::Decision::None:             return "None";
		case crg::Decision::RollbackToPrev:   return "RollbackToPrev";
		case crg::Decision::RollbackToFactory:return "RollbackToFactory";
		case crg::Decision::SkippedNoPrev:    return "SkippedNoPrev";
		case crg::Decision::SkippedSameSlot:  return "SkippedSameSlot";
		case crg::Decision::FailedSwitch:     return "FailedSwitch";
	}
	return "Unknown";
}

void setup() {
	Serial.begin(115200);
	while (!Serial && millis() < 2000) {
		delay(10);
	}

	crg::Options opt;
	opt.failLimit = 3;          // Allow up to 3 suspicious resets before rollback.
	opt.stableTimeMs = 30000;    // Require 30 s of stable loopTick() calls.
	opt.autoSavePrevSlot = true; // Auto-store running slot as previous if none is stored.
	guard.setOptions(opt);

	const crg::Decision decision = guard.beginEarly();
	Serial.printf("[CRG] beginEarly decision: %s\n", decisionToString(decision));
	Serial.printf("[CRG] Reset reason: %d, failCount=%lu\n",
								static_cast<int>(guard.lastResetReason()),
								static_cast<unsigned long>(guard.failCount()));

	Serial.println("[APP] Simulating service bring-up...");
	delay(4000); // Replace with WiFi/MQTT initialization work. 
	// Any crash before markHealthyNow() will count as suspicious

	guard.markHealthyNow();
	Serial.println("[CRG] System marked healthy");
}

void loop() {
	guard.loopTick();

	const uint32_t now = millis();
	if (now - lastHeartbeatMs >= HEARTBEAT_MS) {
		lastHeartbeatMs = now;
		Serial.printf("[APP] uptime=%lu ms, failCount=%lu\n",
									static_cast<unsigned long>(now),
									static_cast<unsigned long>(guard.failCount()));
	}

	if (!restartScheduled && now >= CONTROLLED_RESTART_MS) {
		restartScheduled = true;
		guard.armControlledRestart();
		Serial.println("[APP] Maintenance restart (won't increment fail counter)");
		delay(50);
		ESP.restart();
	}
}
