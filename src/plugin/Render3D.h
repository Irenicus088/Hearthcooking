// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

// The kitchen drawn by us, over the paused game (as Skyrim Yacht draws its table): our own camera and lights (a warm
// key light with soft shadows, a hemisphere fill, and the fire itself as a flickering point light), MSAA, filmic tone
// mapping, and the world behind blurred. The meshes are the game's (loaded with BSModelDB, ours and the food's own):
// we draw their vertex and index buffers and textures directly. Fire, steam, bubbles and salt are sprites.
namespace Render3D
{
	struct Item
	{
		ID3D11Buffer*             vb = nullptr;
		ID3D11Buffer*             ib = nullptr;
		std::uint64_t             desc = 0;  // BSGraphics::VertexDesc
		std::uint32_t             indexCount = 0;
		std::uint32_t             vertexCount = 0;
		std::string               name;
		float                     rot[9]{};  // world rotation (row-major), translation, uniform scale
		float                     pos[3]{};
		float                     scale = 1.0f;
		float                     scale3[3]{ 1, 1, 1 };  // and a stretch along the mesh's own axes (the pieces)
		ID3D11ShaderResourceView* diffuse = nullptr;
		ID3D11ShaderResourceView* normal = nullptr;
		ID3D11ShaderResourceView* env = nullptr;  // a cube map
		ID3D11ShaderResourceView* envMask = nullptr;
		float                     specColor[3]{ 1, 1, 1 };
		float                     specScale = 1.0f, gloss = 30.0f, envScale = 0.0f, alpha = 1.0f, alphaRef = 0.0f;
		bool                      blend = false;
		bool                      fade = false;  // blended by its alpha alone, not its texture's (faded in)
		bool                      test = false;

		// Our touches
		float tint[3]{ 1, 1, 1 };      // multiplies the colour
		float lift[4]{};               // rgb, amount: lerps the colour toward rgb (raw dough, the broth's colour)
		float emissive[3]{};           // light of its own (embers, a burning log), linear
		float clips[6][4]{};           // planes (world): what's past n.p > d isn't drawn; n = 0 for none
		float flesh[4]{};              // rgb, on: the inside seen through a cut is drawn as a flat face this colour
		float shape[4][4]{};           // the whole's rough shape (middle, w the ends' power; axes over radii): no cut
		                               // face outside it (a leaf or a stem seen from behind isn't flesh)
		float uv[4]{ 1, 1, 0, 0 };     // uv scale and offset (a piece shows a patch of its ingredient's skin)
		int   cookMode = 0;            // 0 none, 1 all over (cook[0]), 2 by side round an axis (the spit)
		float cook[12]{};              // how browned: 0 raw, 1 golden, 1.4 dark, 1.8 black
		float cookAxis[3]{ 1, 0, 0 };  // mode 2: the axis (world), and the side that's band 0
		float cookRef[3]{ 0, 0, -1 };
		bool  shadow = true;           // casts a shadow
	};

	struct Sprite
	{
		enum class Kind
		{
			Flame = 0,  // additive, upright
			Smoke = 1,  // soft puff (steam, smoke)
			Ember = 2,  // a bright speck, additive
			Glow = 3,   // a soft radial glow, additive
			Bubble = 4, // a ring on the broth
			Grain = 5   // a speck of salt or flour
		};
		float pos[3]{};
		float size = 1.0f;       // half-width (world units)
		float stretch = 1.0f;    // height / width
		float color[4]{ 1, 1, 1, 1 };
		Kind  kind = Kind::Flame;
		float seed = 0.0f;
		float rot = 0.0f;
	};

	struct Frame
	{
		std::vector<Item>   items;
		std::vector<Sprite> sprites;
		float               viewProj[16]{};  // row-major: clip = M * (x, y, z, 1), in kitchen space (z up)
		float               camPos[3]{};
		float               camRight[3]{ 1, 0, 0 };
		float               camUp[3]{ 0, 0, 1 };
		float               reveal = 0.0f;  // the kitchen fading in (0..1)
		float               blur = 0.0f;    // the world behind blurred and darkened (0..1)
		float               keyDir[3]{ -0.32f, -0.45f, 0.83f };  // toward the key light
		float               shadowCenter[3]{ 0, 0, 0 };
		float               shadowHalf[2]{ 40, 24 };
		float               fire[4]{ 18, 3, 3, 40 };    // the fire's light: where, and its reach
		float               fireColor[3]{ 1.0f, 0.5f, 0.2f };  // linear, its brightness included
		float               exposure = 1.1f;
		bool                show = false;
	};

	void Submit(Frame&& a_frame);  // main thread, every frame the kitchen is open
	void Clear();                  // closed: drop everything

	// The bounds of a mesh's vertices (in its own space), read back from the graphics card once: the food's meshes
	// don't say which way they're long, and the board needs to lay them along it. Ask on the main thread, collect later.
	void RequestMeasure(std::uint64_t a_id, ID3D11Buffer* a_vb, std::uint64_t a_desc, std::uint32_t a_vertexCount);
	bool MeasureResult(std::uint64_t a_id, float (&a_min)[3], float (&a_max)[3]);  // true once it's in
	void ForgetMeasures();  // the kitchen's meshes are let go: their buffers too

	// Render thread (Present), before the interface draws.
	void Draw(IDXGISwapChain* a_swapChain, ID3D11Device* a_device, ID3D11DeviceContext* a_ctx);
}
