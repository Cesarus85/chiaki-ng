// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <impulsetriggers.h>

#include <algorithm>
#include <cmath>

/*
 * DualSense trigger effect modes as sent by the PS5. Layouts follow the
 * community documentation of the DualSense output report (Nielk1's
 * TriggerEffectGenerator). data[] excludes the mode byte.
 */
enum : uint8_t
{
	MODE_OFF_LEGACY = 0x00,
	MODE_SIMPLE_FEEDBACK = 0x01, // data[0] start position, data[1] strength
	MODE_SIMPLE_WEAPON = 0x02,   // data[0] start, data[1] end, data[2] strength
	MODE_OFF = 0x05,
	MODE_SIMPLE_VIBRATION = 0x06, // data[0] frequency, data[1] amplitude, data[2] start position
	MODE_FEEDBACK = 0x21,  // data[0..1] active zones, data[2..5] 3 bit strength per zone
	MODE_BOW = 0x22,       // data[0..1] start + end zone bits, data[2] strength | snap << 3
	MODE_GALLOPING = 0x23, // data[0..1] start + end zone bits, data[2] feet, data[3] frequency
	MODE_WEAPON = 0x25,    // data[0..1] start + end zone bits, data[2] strength
	MODE_VIBRATION = 0x26, // data[0..1] active zones, data[2..5] 3 bit amplitude per zone, data[8] frequency
	MODE_MACHINE = 0x27,   // data[0..1] start + end zone bits, data[2] amp a | amp b << 3, data[3] frequency
};

int ImpulseTriggerZone(uint8_t trigger_value)
{
	return std::min(IMPULSE_TRIGGER_ZONES - 1, (trigger_value * IMPULSE_TRIGGER_ZONES) / 256);
}

static uint16_t zone_bits(const uint8_t *data)
{
	return (uint16_t)(data[0] | (data[1] << 8)) & ((1 << IMPULSE_TRIGGER_ZONES) - 1);
}

static uint32_t zone_values(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

// value 0..7 of zone i from a packed 3 bit per zone field
static int zone_value(uint32_t packed, int i)
{
	return (packed >> (3 * i)) & 0x7;
}

static bool lowest_highest_bit(uint16_t bits, int *low, int *high)
{
	if(!bits)
		return false;
	*low = -1;
	for(int i = 0; i < IMPULSE_TRIGGER_ZONES; i++)
	{
		if(!(bits & (1 << i)))
			continue;
		if(*low < 0)
			*low = i;
		*high = i;
	}
	return true;
}

// A zone can only be "entered" from zone 1 on, the trigger rests in zone 0.
static void add_edge(ImpulseTriggerEffect &e, int zone, float strength)
{
	zone = std::max(1, std::min(IMPULSE_TRIGGER_ZONES - 1, zone));
	e.edge[zone] = std::max(e.edge[zone], std::min(1.0f, strength));
}

static void set_vibration(ImpulseTriggerEffect &e, int from_zone, int to_zone, float amplitude, float freq)
{
	for(int i = std::max(0, from_zone); i <= std::min(IMPULSE_TRIGGER_ZONES - 1, to_zone); i++)
		e.vibration[i] = std::min(1.0f, amplitude);
	e.vibration_freq = freq;
}

ImpulseTriggerEffect ImpulseTriggerEffectFromDualSense(uint8_t mode, const uint8_t *data)
{
	ImpulseTriggerEffect e;
	switch(mode)
	{
		case MODE_SIMPLE_FEEDBACK: {
			float strength = data[1] / 255.0f;
			if(strength <= 0.0f)
				return e;
			add_edge(e, ImpulseTriggerZone(data[0]), strength);
			break;
		}
		case MODE_SIMPLE_WEAPON: {
			float strength = data[2] / 255.0f;
			if(strength <= 0.0f)
				return e;
			add_edge(e, ImpulseTriggerZone(data[0]), strength * 0.3f);
			add_edge(e, ImpulseTriggerZone(data[1]), strength);
			break;
		}
		case MODE_SIMPLE_VIBRATION: {
			float amplitude = data[1] / 255.0f;
			if(amplitude <= 0.0f || data[0] == 0)
				return e;
			set_vibration(e, ImpulseTriggerZone(data[2]), IMPULSE_TRIGGER_ZONES - 1, amplitude, data[0]);
			break;
		}
		case MODE_FEEDBACK: {
			uint16_t active = zone_bits(data);
			uint32_t strengths = zone_values(data + 2);
			float prev = 0.0f;
			for(int i = 0; i < IMPULSE_TRIGGER_ZONES; i++)
			{
				float f = (active & (1 << i)) ? (zone_value(strengths, i) + 1) / 8.0f : 0.0f;
				// a pulse wherever the resistance gets noticeably stronger
				if(f > prev + 0.1f)
					add_edge(e, i, f);
				prev = f;
			}
			break;
		}
		case MODE_WEAPON: {
			int start, end;
			if(!lowest_highest_bit(zone_bits(data), &start, &end))
				return e;
			float strength = ((data[2] & 0x7) + 1) / 8.0f;
			add_edge(e, start, strength * 0.3f);
			add_edge(e, end, strength);
			break;
		}
		case MODE_BOW: {
			int start, end;
			if(!lowest_highest_bit(zone_bits(data), &start, &end))
				return e;
			float strength = ((data[2] & 0x7) + 1) / 8.0f;
			float snap = (((data[2] >> 3) & 0x7) + 1) / 8.0f;
			add_edge(e, start, strength * 0.5f);
			add_edge(e, end, snap);
			break;
		}
		case MODE_VIBRATION: {
			uint16_t active = zone_bits(data);
			uint32_t amplitudes = zone_values(data + 2);
			if(!active || data[8] == 0)
				return e;
			for(int i = 0; i < IMPULSE_TRIGGER_ZONES; i++)
				if(active & (1 << i))
					e.vibration[i] = (zone_value(amplitudes, i) + 1) / 8.0f;
			e.vibration_freq = data[8];
			break;
		}
		case MODE_GALLOPING: {
			int start, end;
			if(!lowest_highest_bit(zone_bits(data), &start, &end) || data[3] == 0)
				return e;
			set_vibration(e, start, end, 0.5f, data[3]);
			break;
		}
		case MODE_MACHINE: {
			int start, end;
			if(!lowest_highest_bit(zone_bits(data), &start, &end) || data[3] == 0)
				return e;
			int amp = std::max(data[2] & 0x7, (data[2] >> 3) & 0x7);
			if(amp == 0)
				return e;
			set_vibration(e, start, end, amp / 7.0f, data[3]);
			break;
		}
		case MODE_OFF_LEGACY:
		case MODE_OFF:
		default:
			// off, calibration and unknown modes
			return e;
	}

	for(int i = 0; i < IMPULSE_TRIGGER_ZONES; i++)
		if(e.edge[i] > 0.0f || e.vibration[i] > 0.0f)
			e.active = true;
	return e;
}

void ImpulseTriggerState::SetEffect(const ImpulseTriggerEffect &new_effect)
{
	effect = new_effect;
}

float ImpulseTriggerState::Update(uint8_t trigger_value, uint64_t now_ms)
{
	int zone = ImpulseTriggerZone(trigger_value);
	if(effect.active && zone > last_zone)
	{
		float strength = 0.0f;
		for(int i = last_zone + 1; i <= zone; i++)
			strength = std::max(strength, effect.edge[i]);
		if(strength > 0.0f)
		{
			pulse_strength = strength;
			pulse_until = now_ms + IMPULSE_TRIGGER_PULSE_MS;
		}
	}
	last_zone = zone;

	float out = 0.0f;
	if(effect.active)
	{
		out = effect.vibration[zone];
		if(out > 0.0f && effect.vibration_freq > 0.0f && effect.vibration_freq < IMPULSE_TRIGGER_MAX_GATED_FREQ)
		{
			double phase = std::fmod(now_ms * (double)effect.vibration_freq / 1000.0, 1.0);
			if(phase >= 0.5)
				out = 0.0f;
		}
	}
	if(now_ms < pulse_until)
		out = std::max(out, pulse_strength);
	return out;
}

bool ImpulseTriggerState::IsIdle(uint64_t now_ms) const
{
	return !effect.active && now_ms >= pulse_until;
}
