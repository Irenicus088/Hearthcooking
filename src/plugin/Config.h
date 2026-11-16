// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

// Data\SKSE\Plugins\Hearthcooking.ini
namespace Config
{
	struct Values
	{
		bool  enabled = true;       // off: the cooking pot and the oven open the game's own menu, as if we weren't here

		// The kitchen on screen
		float tiltDeg = 56.0f;      // how far you look down on it
		float zoom = 1.0f;          // > 1 closer, < 1 further back

		// Play
		float mouseSpeed = 1.0f;
		float uiScale = 1.0f;
		bool  sound = true;         // all of the kitchen's sounds
		float volume = 0.5f;        // the kitchen: chopping, the fire, the pot
		float ambience = 1.0f;      // the loops: the fire's crackle, the simmer, the sizzle
		float effectsVolume = 1.0f; // menu clicks
		bool  hints = true;         // the line at the bottom saying what to do next

		// Diagnostics
		bool debugOverlay = false;  // draws where the mouse can grab things
		bool logInput = false;
	};

	const Values& Get();
	Values&       Edit();  // the in-game settings page changes these live
	void          Load();
	void          Save();  // writes them back to the INI
}
