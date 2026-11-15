// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

// The kitchen's sounds, played through our own XAudio2 voices (the game's sound system is paused with the game).
// The WAVs come from tools/make_sounds.py (Data\SKSE\Plugins\Hearthcooking\Sounds).
namespace Audio
{
	struct Voice
	{
		std::string name;            // a sound in the bank: chop_3, log_1, splash_0 ...
		float       volume = 1.0f;
		float       pitch = 1.0f;    // frequency ratio
		float       lowpass = 0.0f;  // cutoff in Hz; 0 = none
		float       pan = 0.0f;      // -1 left .. 1 right
	};

	bool   Ready();
	void   Play(const Voice& a_voice);
	void   Take(const std::string& a_prefix, float a_volume, float a_pitchJitter = 0.06f, float a_pan = 0.0f);  // a random take
	int    Count(const std::string& a_prefix);  // how many takes a sound has (chop_ -> 6)
	void   SetLoop(const std::string& a_name, float a_volume);  // loop_fire, loop_simmer ...: eased toward the volume
	void   StopAll();
	void   SetMuted(bool a_muted);
	bool   Muted();
	double Now();  // seconds, for spacing sounds out
}
