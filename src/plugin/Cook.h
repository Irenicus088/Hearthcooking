// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

// The cooking: sitting down at a cooking pot or an oven opens the recipe book over the game's own crafting menu.
// Quick Cook hands you back to that menu (the game's own dishes); Cook by Hand opens the kitchen, where the dish is
// chopped, stirred, turned or kneaded and baked by hand, and comes out Burnt, Plain, Hearty or Masterwork.
namespace Cook
{
	void OnDataLoaded();
	void OnGameLoaded();
	void Tick(float a_dt);  // every frame while the kitchen is open, from its menu (main thread, game paused)
	void Abort();           // something failed: close the kitchen (nothing is used up)
	void SetupUi(float a_screenH);
	void DrawUi();          // ImGui, inside Present

	// SKSE co-save
	void Save(SKSE::SerializationInterface* a_intfc);
	void Load(SKSE::SerializationInterface* a_intfc);
	void Revert(SKSE::SerializationInterface* a_intfc);
}
