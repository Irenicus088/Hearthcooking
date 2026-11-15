// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

#include "../sim/Math.h"
#include "Render3D.h"

// The kitchen's 3D: a little scene of its own (not in the world), drawn by Render3D over the paused, blurred game.
// Everything in it is a "prop": a copy of a mesh (ours, or the game's own food), placed in kitchen space (game units,
// z up, the worktop at z = 0, the same as src/sim/Kitchen.h), with how it should look (tinted, browned, cut).
// The menu that pauses the game while you cook lives here too.
namespace Scene
{
	using Id = int;

	struct Look
	{
		float tint[3]{ 1, 1, 1 };
		float lift[4]{};
		float emissive[3]{};
		float clip[4]{};     // a world plane: n.p > d isn't drawn
		float clip2[4]{};
		float uv[4]{ 1, 1, 0, 0 };
		int   cookMode = 0;  // see Render3D::Item
		float cook[12]{};
		float cookAxis[3]{ 1, 0, 0 };
		float cookRef[3]{ 0, 0, -1 };
		ID3D11ShaderResourceView* skin = nullptr;  // in place of its own colour texture (a piece wears its ingredient's)
		bool  shadow = true;
	};

	void Register();  // kDataLoaded: the menu
	void Open();      // shows the menu (pauses the game); the kitchen fades in
	void Close();
	bool IsOpen();

	Id    Add(const std::string& a_model);  // meshes\-relative; hidden until placed; -1 if it couldn't load
	void  Remove(Id a_id);
	void  RemoveAll();
	bool  Valid(Id a_id);
	void  Place(Id a_id, const hc::Vec3& a_pos, const hc::Quat& a_rot, float a_scale = 1.0f);
	void  Stretch(Id a_id, float a_x, float a_y, float a_z);  // along its own axes
	void  Hide(Id a_id, bool a_hidden);
	Look& LookOf(Id a_id);
	bool  Bounds(Id a_id, hc::Vec3& a_min, hc::Vec3& a_max);  // in its own space, once the graphics card has said
	ID3D11ShaderResourceView* Skin(Id a_id);                  // its own colour texture
	ID3D11ShaderResourceView* Texture(const char* a_path);    // a texture by path (textures\...)

	// The camera: looking down at the config's tilt toward a_target from a_dist away; it eases there
	void SetCamera(const hc::Vec3& a_target, float a_dist, bool a_snap = false);
	void SetFire(const hc::Vec3& a_pos, float a_heat);  // the fire's light; heat 0 is none
	void SetSprites(std::vector<Render3D::Sprite>&& a_sprites);

	// Kitchen space <-> screen pixels
	bool  Project(const hc::Vec3& a_p, float& a_sx, float& a_sy);
	bool  Unproject(float a_sx, float a_sy, double a_planeZ, double& a_x, double& a_y);
	float PixelsPerUnit(const hc::Vec3& a_at);

	void Update(float a_dt);  // every frame, from the menu (main thread), after the cooking has moved things
}
