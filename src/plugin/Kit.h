// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

// The kitchen's things, from the game's own meshes wherever the game has one: the table, the pot, the ladle, the
// knife, the bowls and plates, the firewood, the salt, the fire pit, the spit, the oven, and the clutter set about.
// Each is found in the player's load order when the game starts (by the item's name and the mesh's file name, the way
// Skyrim Yacht finds its cups), fitted into the space the kitchen gives it, and logged; anything the game doesn't
// have uses Hearthcooking's own mesh. Any of them can be set in Hearthcooking.ini ([Meshes]).
namespace Kit
{
	enum class Role
	{
		Table,
		Pot,
		Ladle,
		Knife,
		Board,
		Bowl,
		Plate,
		Log,
		SaltBowl,
		Salt,
		FirePit,
		Spit,
		Oven,
		Peel,
		Count
	};

	struct Mesh
	{
		std::string path;           // meshes\-relative
		bool        vanilla = false;  // the game's (else ours, made to measure)
		double      turn = 0.0;     // degrees about the vertical, to face it the way the kitchen wants
		double      tilt = 0.0;     // degrees about x (a mesh modelled standing up that should lie down, or the reverse)
	};

	void        Resolve();  // kDataLoaded
	const Mesh& Get(Role a_role);
	const char* Name(Role a_role);
	std::string Ours(Role a_role);  // Hearthcooking's own mesh for it ("" if there's none)

	// The clutter set about the kitchen (jugs, baskets, sacks, cheese): the game's own, as many as are found
	const std::vector<std::string>& Decor();

	// Every knife the game has (meshes), for picking one; and setting a role's mesh now (kept in the INI), "" for
	// the one found by itself
	const std::vector<std::string>& Knives();
	void SetMesh(Role a_role, const std::string& a_path);

	// An ingredient's mesh, kept in Hearthcooking.ini [Ingredients] once it's been used (like the kitchen's things)
	std::vector<std::string> MeshesLike(const std::string& a_name);
	std::string IngredientMesh(const std::string& a_name);
	void SetIngredientMesh(const std::string& a_name, const std::string& a_path);
}
