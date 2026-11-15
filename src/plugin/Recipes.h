// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

#include "../sim/Kitchen.h"

// The game's own cooking recipes (every constructible object made at a cooking pot or a Hearthfire oven, from the
// game and from any mod), what each ingredient does in the minigame, and the better and worse dishes.
//
// Plain is always the game's own dish. Burnt, Hearty and Masterwork are empty dishes in Hearthcooking.esp: the first
// time you cook a dish to one of them, one is made into a copy of the game's dish (its name with the tier in front,
// its mesh, its effects stronger or weaker and longer or shorter, its price) and the co-save remembers which is which.
namespace Recipes
{
	enum class Role
	{
		Chop,     // cut on the board (vegetables, meat, cheese)
		Toss,     // goes in as it is (berries, ale, milk)
		Salt,     // a pinch from the salt bowl
		Meat,     // onto the spit
		Dough,    // the oven: flour, butter, eggs, milk make the dough
		Filling   // the oven: fruit, cheese and the rest: on the pie, or worked into the dough
	};

	struct Ingredient
	{
		RE::TESBoundObject* form = nullptr;
		std::string         name;
		std::string         model;  // meshes\-relative
		int                 count = 1;
		Role                role = Role::Toss;
	};

	struct Recipe
	{
		RE::BGSConstructibleObject* cobj = nullptr;
		RE::TESBoundObject*         result = nullptr;
		std::string                 name;
		int                         makes = 1;
		hc::Flow                    flow = hc::Flow::Pot;
		bool                        oven = false;    // made at the oven (else the pot and spit)
		bool                        pie = false;     // fillings on top and a crimped edge
		bool                        salted = false;  // salt is one of its ingredients
		bool                        dough = false;   // the oven: there's dough to make
		std::vector<Ingredient>     ings;
	};

	void OnDataLoaded();

	// The recipes made at this kind of station, cookable ones first, then by name
	std::vector<const Recipe*> ForStation(bool a_oven);
	bool Known(const Recipe& a_r);     // its conditions hold (as the game's own crafting menu checks them)
	int  CanMake(const Recipe& a_r);   // how many times over you carry the ingredients for
	int  Held(RE::TESBoundObject* a_form);
	void Consume(const Recipe& a_r);
	// Gives the dish at a tier; returns what was given (the game's own dish for Plain, or when a tier can't be made)
	RE::TESBoundObject* Give(const Recipe& a_r, hc::Tier a_tier);
	hc::Tier Best(const Recipe& a_r, bool& a_any);  // the best you've made it

	bool IsBurnt(RE::FormID a_form);  // one of our Burnt dishes (eating it costs some stamina)
	void InstallEatWatch();

	// SKSE co-save
	void Save(SKSE::SerializationInterface* a_intfc);
	void Load(SKSE::SerializationInterface* a_intfc);
	void Revert();
}
