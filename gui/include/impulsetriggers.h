// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_IMPULSETRIGGERS_H
#define CHIAKI_IMPULSETRIGGERS_H

#include <cstdint>

/*
 * Approximates DualSense adaptive trigger effects on controllers that only
 * have trigger rumble motors (Xbox "impulse triggers", e.g. ROG Xbox Ally X).
 *
 * The PS5 sends raw DualSense trigger effects: a mode byte plus 10 parameter
 * bytes. The trigger travel is split into 10 zones. A rumble motor can't
 * create resistance, so:
 *   - resistance / weapon / bow effects become short pulses when the trigger
 *     is pressed into the zone where the resistance starts or the "break" is,
 *   - vibration effects become rumble while the trigger is inside the active
 *     zones; low frequencies are reproduced by pulsing the motor.
 */

#define IMPULSE_TRIGGER_ZONES 10
#define IMPULSE_TRIGGER_PULSE_MS 45
#define IMPULSE_TRIGGER_MAX_GATED_FREQ 30.0f

struct ImpulseTriggerEffect
{
	bool active = false;
	// rumble strength 0..1 while the trigger is in this zone
	float vibration[IMPULSE_TRIGGER_ZONES] = {};
	// 0 = continuous, otherwise the motor is pulsed at this rate (Hz)
	float vibration_freq = 0.0f;
	// pulse strength 0..1 when the trigger enters this zone from below
	float edge[IMPULSE_TRIGGER_ZONES] = {};
};

// mode = DualSense trigger effect mode, data = the 10 parameter bytes
ImpulseTriggerEffect ImpulseTriggerEffectFromDualSense(uint8_t mode, const uint8_t *data);

// trigger value 0..255 -> zone 0..9
int ImpulseTriggerZone(uint8_t trigger_value);

class ImpulseTriggerState
{
	public:
		void SetEffect(const ImpulseTriggerEffect &effect);
		// Returns the rumble strength 0..1 for the current trigger position.
		float Update(uint8_t trigger_value, uint64_t now_ms);
		// No effect set and no pulse running: the motor can stay off.
		bool IsIdle(uint64_t now_ms) const;

	private:
		ImpulseTriggerEffect effect;
		int last_zone = 0;
		uint64_t pulse_until = 0;
		float pulse_strength = 0.0f;
};

#endif // CHIAKI_IMPULSETRIGGERS_H
