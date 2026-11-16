// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Cook.h"

#include "Audio.h"
#include "Config.h"
#include "Kit.h"
#include "Overlay.h"
#include "Recipes.h"
#include "Scene.h"
#include "Ui.h"

#include "../sim/Kitchen.h"

namespace Cook
{
	namespace
	{
		using hc::Quat;
		using hc::Vec3;
		namespace L = hc::layout;
		using namespace Ui;
		using Recipes::Role;

		constexpr double kPi = 3.14159265358979;
		constexpr double kCarryZ = 9.0;    // how high things are carried
		constexpr double kBoardTop = L::kBoardH;
		constexpr int    kMaxUnits = 10;

		// ---------------------------------------------------------------- small helpers
		double Clamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
		double Smooth(double t)
		{
			t = Clamp(t, 0.0, 1.0);
			return t * t * (3.0 - 2.0 * t);
		}
		float Rand01() { return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX); }
		double Wrap(double a)
		{
			while (a > kPi) {
				a -= 2 * kPi;
			}
			while (a < -kPi) {
				a += 2 * kPi;
			}
			return a;
		}
		float Hash(int a_k, int a_salt = 0)
		{
			unsigned h = static_cast<unsigned>(a_k) * 0x9E3779B1u + static_cast<unsigned>(a_salt) * 0x85EBCA77u;
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12;
			return (h & 0xFFFFFF) / 16777216.0f;
		}
		Quat RotX(double a) { return Quat::FromAxisAngle({ 1, 0, 0 }, a); }
		Quat RotY(double a) { return Quat::FromAxisAngle({ 0, 1, 0 }, a); }
		Quat RotZ(double a) { return Quat::FromAxisAngle({ 0, 0, 1 }, a); }

		// A rotation from its matrix (rows)
		Quat FromRows(const double m[3][3])
		{
			Quat         q;
			const double tr = m[0][0] + m[1][1] + m[2][2];
			if (tr > 0) {
				const double s = std::sqrt(tr + 1.0) * 2;
				q = { 0.25 * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s };
			} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
				const double s = std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
				q = { (m[2][1] - m[1][2]) / s, 0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s };
			} else if (m[1][1] > m[2][2]) {
				const double s = std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
				q = { (m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s };
			} else {
				const double s = std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
				q = { (m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s };
			}
			q.Normalize();
			return q;
		}

		// ---------------------------------------------------------------- state
		enum class Phase
		{
			Idle,
			Book,
			Cooking,
			Result
		};

		enum class Hold
		{
			None,
			Ingredient,  // carrying something from the cloth
			Log,         // a log from the pile
			FireLog,     // a burning log pulled out
			Board,       // tipping the board into the pot
			Ladle,       // stirring
			LadleOut,    // the ladle lifted out, to the bowl
			Salt,        // a pinch of salt
			Crank,       // turning the spit
			Meat,        // the roast lifted off the spit
			Dough,       // kneading, or carrying the dough to the oven
			Spread,      // spreading the filling
			Crimp,       // pinching the edge
			Door,        // the oven door
			Bread,       // taking it out of the oven
			Peel,        // the peel: to the dough to slide it on, then into the oven
			Mix,         // stirring the flour into the water
			Ball,        // the ball of dough, out of the bowl onto the board
			Apples       // the bowl of cut filling, over the dough
		};

		struct Unit
		{
			int                       ing = 0;
			Role                      role = Role::Toss;
			std::string               name, model;
			Scene::Id                 prop = -1;
			std::vector<Scene::Id>    segs, cores;  // on the board: its own mesh cut to each piece, and its solid inside
			enum class At
			{
				Cloth,
				Held,
				Board,
				Pot,
				Spit,
				Dough,
				Gone
			} at = At::Cloth;
			bool                      fitted = false;
			Quat                      lie;          // lays it along x, flat
			Vec3                      center;       // its middle, in its own space
			double                    dims[3]{ 6, 2, 2 };  // long, middle, short (its own size)
			Vec3                      clothAt;
			int                       lane = -1;
			hc::Cutting               cut;
			double                    boardLen = 8.0;
			double                    spitX = 0.0;
			bool                      wentWhole = false;  // into the pot without being cut
			bool                      thin = false;       // a leek, a carrot: only ever cut across
			bool                      dice = false;       // the oven: cut on the board first, then into the bowl
		};

		// A piece: the ingredient's own mesh cut to a box (x along its length, y across, z up; about its middle, at the
		// board's scale; whole where cut is false), its body's middle at mid in the same frame
		struct Piece
		{
			Scene::Id prop = -1;
			Scene::Id core = -1;  // its solid inside: a smooth egg of flesh the thing's size, cut the same
			int       unit = 0;
			double    box[6]{};  // x0 x1 y0 y1 z0 z1
			Vec3      mid;
			bool      cut = false;
			bool      onDough = false;  // the oven: set on the dough, here (from its middle)
			Vec3      doughOff;
			double    turn = 0.0;
		};

		struct Grain
		{
			Vec3   p, v;
			double life = 1.0, age = 0.0;
			float  col[4]{ 1, 1, 1, 1 };
			float  size = 0.12f;
			Render3D::Sprite::Kind kind = Render3D::Sprite::Kind::Grain;
			bool   gravity = true;
		};

		struct Props
		{
			Scene::Id kitchen = -1, room = -1, cloth = -1, saltPile = -1, board = -1, knife = -1, salt = -1, firepit = -1, pot = -1, broth = -1, ladle = -1, ladleSoup = -1, bowl = -1, posts = -1, rod = -1,
					  plate = -1, ovenBase = -1, ovenDome = -1, door = -1, peel = -1, dough = -1, disk = -1, result = -1, heldLog = -1,
					  mixBowl = -1, mixWater = -1, mixBall = -1, fillBowl = -1;
			Scene::Id fireLogs[hc::Fire::kMaxLogs]{ -1, -1, -1, -1, -1 };
			Scene::Id pile[3]{ -1, -1, -1 };
			std::vector<Scene::Id> crimps;
			std::vector<std::pair<Scene::Id, Vec3>> fill;  // the filling on the pie (disk-local, units)
			std::array<bool, static_cast<int>(Kit::Role::Count)> game{};  // this one is the game's own mesh (fitted), not ours
			std::vector<std::pair<Scene::Id, int>> decor;                   // the clutter, and its spot
		};

		// Something of the game's, fitted into the space the kitchen gives it
		struct Fitting
		{
			Scene::Id  id = -1;
			Kit::Role  role = Kit::Role::Count;
			Vec3       at;        // where its base's middle goes
			Vec3       size;      // the space (0: any)
			double     rotZ = 0;
			bool       stretch = false;
		};
		std::vector<Fitting> g_fits;

		std::recursive_mutex g_mtx;
		Phase                g_phase = Phase::Idle;
		bool                 g_ovenStation = false;
		bool                 g_quick = false;  // Quick Cook was chosen: let the game's menu be
		std::vector<const Recipes::Recipe*> g_book;
		int                  g_sel = 0;
		const Recipes::Recipe* g_recipe = nullptr;
		hc::Flow             g_flow = hc::Flow::Pot;
		double               g_clock = 0.0;
		std::string          g_toast;
		double               g_toastAt = -10.0;
		bool                 g_help = false, g_confirm = false;

		// the dish on the go
		std::vector<Unit>        g_units;
		std::vector<Piece>       g_pieces;
		hc::Chunks               g_chunks;
		hc::Fire                 g_fire{ 5.0 };
		std::unique_ptr<hc::Pot>  g_pot;
		std::unique_ptr<hc::Spit> g_spit;
		std::unique_ptr<hc::Oven> g_oven;
		Props                    P;
		std::vector<Grain>       g_grains;
		float                    g_broth[3]{ 0.6f, 0.42f, 0.22f };
		int                      g_tossedIn = 0;

		// hands
		Hold   g_hold = Hold::None;
		int    g_holdUnit = -1;
		Vec3   g_carry;              // where the held thing is
		double g_grabX = 0, g_grabY = 0;
		double g_tip = 0, g_tipWant = 0, g_boardY = L::kBoardY;
		bool   g_tipped = false;     // the board's last lot has gone over its edge
		hc::Chunks::Board g_boardPose, g_boardPrev;
		double g_knifeX = L::kBoardX, g_knifeY = L::kBoardY, g_knifeAnim = -1;
		bool   g_knifeOn = false, g_knifeCut = false;
		bool   g_knifeAlong = false;  // turned to cut along the food (the second way, for dice)
		int    g_knifeLane = -1;
		double g_ladleA = -kPi * 0.6, g_ladleR = 3.6, g_lastStir = 0, g_stirred = 0;
		Vec3   g_ladleOutAt;
		double g_crankPrev = 0, g_crankRun = 0;
		double g_pinch = 0;
		double g_door = 1.0, g_doorWant = 1.0;  // the oven door, 0 shut .. 1 open
		Vec3   g_doughAt{ L::kBoardX, L::kBoardY, kBoardTop };
		// the oven's bowls, behind the board: water and flour stirred to a dough; the cut filling waiting
		const Vec3 kMixAt{ -11.0, 11.5, 0.0 }, kFillAt{ -1.0, 11.5, 0.0 };
		double g_mix = 0.0, g_mixA = 0.0;  // how far the flour is stirred in (1: a ball), the spoon's angle
		bool   g_ball = false;             // a ball of dough in the bowl
		bool   g_doughOnBoard = false;     // the dough out on the board (kneaded, filled, folded there)
		int    g_doughParts = 0, g_doughNeeded = 0;
		bool   g_rolled = false;
		double g_squash = 0, g_squashA = 0, g_strokeLen = 0;
		int    g_stock = 0;
		int    g_lastFill = 0;          // the filling being spread
		Vec3   g_lastSpread;
		double g_peelT = -1;        // the peel's trip into or out of the oven
		// how many times faster than real each cooks
		constexpr double kPotSpeed = 2.0, kSpitSpeed = 1.5, kOvenSpeed = 6.0;
		Vec3   g_peelAt;            // the peel in the hand
		Vec3   g_peelFrom;          // where its trip into the oven starts
		bool   g_peelLoaded = false;  // the dough on it
		bool   g_peelBread = false;   // the bake on it, out of the oven
		bool   g_peelIn = true;
		bool   g_served = false;
		double g_serveAt = 0;
		hc::Result g_result;
		RE::TESBoundObject* g_given = nullptr;
		std::string g_givenName;
		bool   g_burntLook = false;

		// UI -> kitchen
		enum class Act
		{
			Select,
			CookByHand,
			QuickCook,
			Leave,
			Abandon,
			AbandonYes,
			AbandonNo,
			Again,
			Done,
			Help,
			Mute
		};
		bool board_kneading();
		bool MenuBtnInk(const char* a_id, const std::string& a_label, float a_cx, float a_y, bool a_enabled);
		std::vector<std::pair<Act, int>> g_acts;
		std::mutex                       g_actMtx;
		void Post(Act a_act, int a_arg = 0)
		{
			std::lock_guard lk(g_actMtx);
			g_acts.emplace_back(a_act, a_arg);
		}

		void Toast(std::string a_text)
		{
			g_toast = std::move(a_text);
			g_toastAt = g_clock;
		}

		RE::GPtr<RE::CraftingMenu> Crafting()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui ? ui->GetMenu<RE::CraftingMenu>() : RE::GPtr<RE::CraftingMenu>();
		}

		void ShowCrafting(bool a_show)
		{
			if (auto m = Crafting(); m && m->uiMovie) {
				m->uiMovie->SetVisible(a_show);
			}
		}

		// ---------------------------------------------------------------- the kitchen's furniture
		Scene::Id Add(const char* a_name)
		{
			const auto id = Scene::Add(std::string("Hearthcooking\\") + a_name + ".nif");
			if (id < 0) {
				spdlog::warn("Kitchen: {} didn't load", a_name);
			}
			return id;
		}

		void Put(Scene::Id a_id, const Vec3& a_at, double a_rotZ = 0.0, float a_scale = 1.0f)
		{
			Scene::Place(a_id, a_at, RotZ(a_rotZ), a_scale);
		}

		Vec3 FireCenter()
		{
			return g_flow == hc::Flow::Oven ? Vec3{ L::kOvenX, -0.5, L::kFireboxZ } : Vec3{ L::kFireX, L::kFireY, 0.6 };
		}

		// ---- the game's meshes: how one lies (longest side along x, shortest up) and fitting one into a space
		struct Lie
		{
			Quat   q;
			Vec3   c;          // its middle, in its own space
			double d[3]{ 1, 1, 1 };  // long, middle, short
		};

		bool LieOf(Scene::Id a_id, Lie& a_out)
		{
			Vec3 mn, mx;
			if (a_id < 0 || !Scene::Bounds(a_id, mn, mx)) {
				return false;
			}
			const double e[3] = { mx.x - mn.x, mx.y - mn.y, mx.z - mn.z };
			int          ord[3] = { 0, 1, 2 };
			std::sort(ord, ord + 3, [&](int a, int b) { return e[a] > e[b]; });
			double m[3][3]{};
			for (int r = 0; r < 3; ++r) {
				m[r][ord[r]] = 1.0;
			}
			const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
			                   m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
			if (det < 0) {
				for (auto& v : m[1]) {
					v = -v;
				}
			}
			a_out.q = FromRows(m);
			a_out.c = { (mn.x + mx.x) * 0.5, (mn.y + mx.y) * 0.5, (mn.z + mx.z) * 0.5 };
			for (int k = 0; k < 3; ++k) {
				a_out.d[k] = (std::max)(0.05, e[ord[k]]);
			}
			return true;
		}

		struct Pose
		{
			Vec3  pos;
			Quat  q;
			float s = 1.0f;
			float w[3]{ 1, 1, 1 };
		};

		// Fits a mesh's bounds into a box: its base's middle at a_at, a_size across (x, y) and high (z), turned a_rotZ
		// (and the INI's turn and tilt for it). Uniform: as big as fits. Stretched: exactly the box.
		bool FitBox(Scene::Id a_id, const Kit::Mesh& a_m, const Vec3& a_at, const Vec3& a_size, double a_rotZ, bool a_stretch, Pose& a_out)
		{
			Vec3 mn, mx;
			if (a_id < 0 || !Scene::Bounds(a_id, mn, mx)) {
				return false;
			}
			const Quat q = RotZ(a_rotZ + a_m.turn * kPi / 180.0) * RotX(a_m.tilt * kPi / 180.0);
			Vec3       lo{ 1e9, 1e9, 1e9 }, hi{ -1e9, -1e9, -1e9 };
			for (int c = 0; c < 8; ++c) {
				const Vec3 v = q.Rotate({ (c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z });
				lo = { (std::min)(lo.x, v.x), (std::min)(lo.y, v.y), (std::min)(lo.z, v.z) };
				hi = { (std::max)(hi.x, v.x), (std::max)(hi.y, v.y), (std::max)(hi.z, v.z) };
			}
			const double e[3] = { (std::max)(0.01, hi.x - lo.x), (std::max)(0.01, hi.y - lo.y), (std::max)(0.01, hi.z - lo.z) };
			const double want[3] = { a_size.x, a_size.y, a_size.z };
			double       r[3]{}, fit = 1e9, sum = 0;
			int          n = 0;
			for (int k = 0; k < 3; ++k) {
				if (want[k] > 0) {
					r[k] = want[k] / e[k];
					fit = (std::min)(fit, r[k]);
					sum += r[k];
					++n;
				}
			}
			if (n == 0) {
				fit = 1.0;
			}
			Pose p;
			p.q = q;
			if (a_stretch && n > 0) {
				p.s = 1.0f;
				for (int k = 0; k < 3; ++k) {
					p.w[k] = static_cast<float>(want[k] > 0 ? r[k] : sum / n);
				}
			} else {
				p.s = static_cast<float>(fit);
			}
			const Vec3 bc{ (lo.x + hi.x) * 0.5 * p.s, (lo.y + hi.y) * 0.5 * p.s, lo.z * p.s };
			p.pos = a_at - Vec3{ bc.x * p.w[0], bc.y * p.w[1], bc.z * p.w[2] };
			a_out = p;
			return true;
		}

		void Apply(Scene::Id a_id, const Pose& a_p)
		{
			Scene::Place(a_id, a_p.pos, a_p.q, a_p.s);
			Scene::StretchWorld(a_id, a_p.w[0], a_p.w[1], a_p.w[2]);
			Scene::Hide(a_id, false);
		}

		bool Game(Kit::Role a_r) { return P.game[static_cast<int>(a_r)]; }

		// One of the kitchen's things: the game's mesh if there is one (and it loads), else ours
		Scene::Id KitAdd(Kit::Role a_r)
		{
			const auto& m = Kit::Get(a_r);
			P.game[static_cast<int>(a_r)] = false;
			if (m.vanilla && !m.path.empty()) {
				const auto id = Scene::Add(m.path);
				if (id >= 0) {
					P.game[static_cast<int>(a_r)] = true;
					Scene::Hide(id, true);  // until it's measured and fitted
					return id;
				}
				spdlog::warn("Kitchen: the game's {} ({}) didn't load; using ours", Kit::Name(a_r), m.path);
			}
			const auto ours = Kit::Ours(a_r);
			return ours.empty() ? -1 : Scene::Add(ours);
		}

		// Places a fixed thing: ours exactly where it was made to go, the game's fitted into the same space
		void Settle(Scene::Id a_id, Kit::Role a_r, const Vec3& a_at, const Vec3& a_size, double a_rotZ, bool a_stretch, const Vec3& a_oursAt, double a_oursRot = 0.0)
		{
			if (a_id < 0) {
				return;
			}
			if (Game(a_r)) {
				g_fits.push_back({ a_id, a_r, a_at, a_size, a_rotZ, a_stretch });
			} else {
				Put(a_id, a_oursAt, a_oursRot);
			}
		}

		void Refit()
		{
			for (const auto& f : g_fits) {
				Pose p;
				if (FitBox(f.id, f.role == Kit::Role::Count ? Kit::Mesh{} : Kit::Get(f.role), f.at, f.size, f.rotZ, f.stretch, p)) {
					Apply(f.id, p);
				}
			}
		}

		// A log (ours, or the game's firewood) lying along a_dir's angle at a_at, a_len long
		void PlaceLog(Scene::Id a_id, const Vec3& a_at, double a_rotZ, double a_len, double a_tiltY = 0.0)
		{
			if (a_id < 0) {
				return;
			}
			if (!Game(Kit::Role::Log)) {
				Scene::Place(a_id, a_at, RotZ(a_rotZ) * RotY(a_tiltY), static_cast<float>(a_len / 8.4));
				return;
			}
			Lie lie;
			if (!LieOf(a_id, lie)) {
				Scene::Hide(a_id, true);
				return;
			}
			const double s = a_len / lie.d[0];
			const Quat   q = RotZ(a_rotZ) * RotY(a_tiltY) * lie.q;
			Scene::Place(a_id, a_at - q.Rotate(lie.c) * s, q, static_cast<float>(s));
		}

		constexpr double kClothShift = 4.0;  // the cloth, in from the table's left edge

		void BuildKitchen(bool a_oven, hc::Flow a_flow)
		{
			Scene::RemoveAll();
			P = Props{};
			g_fits.clear();
			// the table (the game's, stretched under the worktop's space) and the room (the hearth slab, the wall)
			P.kitchen = KitAdd(Kit::Role::Table);
			Settle(P.kitchen, Kit::Role::Table, { -13.5, 0.0, -14.0 }, { 37.0, 37.0, 14.0 }, 0.0, true, {});
			P.room = Add("Room");
			Put(P.room, {});
			P.cloth = Add("Cloth");
			Put(P.cloth, { kClothShift, 0.0, 0.0 });
			for (int k = 0; k < 3; ++k) {
				P.pile[k] = KitAdd(Kit::Role::Log);
			}
			for (auto& f : P.fireLogs) {
				f = KitAdd(Kit::Role::Log);
				Scene::Hide(f, true);
			}
			P.heldLog = KitAdd(Kit::Role::Log);
			Scene::Hide(P.heldLog, true);
			if (!a_oven) {
				P.firepit = KitAdd(Kit::Role::FirePit);
				Settle(P.firepit, Kit::Role::FirePit, { L::kFireX, L::kFireY, 0.0 }, { 2 * (L::kFireR + 1.8), 2 * (L::kFireR + 1.8), 0.0 }, 0.0, false, { L::kFireX, L::kFireY, 0.0 });
			}
			if (a_flow != hc::Flow::Spit) {
				P.board = KitAdd(Kit::Role::Board);
			}
			if (a_flow == hc::Flow::Oven) {
				P.knife = KitAdd(Kit::Role::Knife);  // a pie's fruit is cut first
			}
			if (a_flow == hc::Flow::Pot) {
				P.knife = KitAdd(Kit::Role::Knife);
				P.pot = KitAdd(Kit::Role::Pot);
				Settle(P.pot, Kit::Role::Pot, { L::kPotX, L::kPotY, 0.0 }, { 2 * (L::kPotR + 0.9), 2 * (L::kPotR + 0.9), L::kPotRim }, 0.0, true, { L::kPotX, L::kPotY, 0.0 });
				P.broth = Add("Broth");
				P.ladleSoup = Add("Broth");  // a ladleful, carried to the bowl
				Scene::Hide(P.ladleSoup, true);
				P.ladle = KitAdd(Kit::Role::Ladle);
				P.bowl = KitAdd(Kit::Role::Bowl);
				Settle(P.bowl, Kit::Role::Bowl, { L::kServeX, L::kServeY, 0.0 }, { 7.0, 7.0, 0.0 }, 0.0, false, { L::kServeX, L::kServeY, 0.0 });
			}
			if (a_flow == hc::Flow::Spit) {
				P.posts = KitAdd(Kit::Role::Spit);
				if (Game(Kit::Role::Spit)) {
					// the game's spit comes dressed: pots, chains, meat on its own turner. Only its frame stays
					Scene::HideShapes(P.posts, { "pot", "chain", "lid", "beef", "venison", "goat", "meat", "turner", "chicken", "rabbit", "salmon", "food" });
				}
				Settle(P.posts, Kit::Role::Spit, { L::kFireX, L::kFireY, 0.0 }, { 2 * L::kSpitX1 - 2 * L::kFireX + 2.0, 0.0, L::kSpitZ + 1.4 }, 0.0, true, { L::kFireX, L::kFireY, 0.0 });
				P.rod = Add("SpitRod");
			}
			if (a_flow != hc::Flow::Pot) {
				P.plate = KitAdd(Kit::Role::Plate);
				Settle(P.plate, Kit::Role::Plate, { L::kServeX, L::kServeY, 0.0 }, { 9.4, 9.4, 0.0 }, 0.0, false, { L::kServeX, L::kServeY, 0.0 });
			}
			if (a_oven) {
				P.ovenDome = KitAdd(Kit::Role::Oven);
				if (Game(Kit::Role::Oven)) {
					// the game's oven, in the space ours takes (its mouth toward you; OvenTurn in the INI if it isn't)
					Settle(P.ovenDome, Kit::Role::Oven, { L::kOvenX, L::kOvenY - 0.5, 0.0 }, { 25.0, 23.0, L::kOvenFloorZ + 9.5 }, 0.0, true, {});
				} else {
					P.ovenBase = Add("OvenBase");
					Put(P.ovenBase, { L::kOvenX, L::kOvenY, 0.0 });
					Put(P.ovenDome, { L::kOvenX, L::kOvenY, 0.0 });
				}
				P.door = Add("OvenDoor");
				P.peel = KitAdd(Kit::Role::Peel);
				// the mixing bowl (water, then dough) and the bowl the cut filling waits in
				P.mixBowl = KitAdd(Kit::Role::Bowl);
				Settle(P.mixBowl, Kit::Role::Bowl, kMixAt, { 8.0, 8.0, 0.0 }, 0.0, false, kMixAt);
				P.mixWater = Add("Broth");
				P.mixBall = Add("Dough");
				Scene::Hide(P.mixBall, true);
				P.fillBowl = KitAdd(Kit::Role::Bowl);
				Settle(P.fillBowl, Kit::Role::Bowl, kFillAt, { 7.0, 7.0, 0.0 }, 0.0, false, kFillAt);
				P.dough = Add("Dough");
				Scene::Hide(P.dough, true);
				P.disk = Add("DoughDisk");
				Scene::Hide(P.disk, true);
			}
			// no clutter: only what's used
		}

		// ---------------------------------------------------------------- the ingredients
		const Recipes::Ingredient& Ing(const Unit& a_u) { return g_recipe->ings[a_u.ing]; }

		double Thick(const Unit& a_u, double a_len, int a_k) { return a_u.dims[a_k] * a_len / a_u.dims[0]; }

		// How it lies flat: its longest side along x, its shortest up
		bool Fit(Unit& a_u)
		{
			if (a_u.fitted) {
				return true;
			}
			Lie lie;
			if (!LieOf(a_u.prop, lie)) {
				return false;
			}
			a_u.lie = lie.q;
			a_u.center = lie.c;
			for (int k = 0; k < 3; ++k) {
				a_u.dims[k] = lie.d[k];
			}
			a_u.boardLen = Clamp(a_u.dims[0] * 0.5, 4.0, 9.0);
			a_u.cut.x0 = 0.0;
			a_u.cut.x1 = a_u.boardLen;
			a_u.cut.target = (std::max)(0.6, a_u.boardLen / 6.0);
			a_u.cut.width = Thick(a_u, a_u.boardLen, 1) * 0.96;
			{
				std::string n = a_u.name;
				std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				a_u.thin = a_u.dims[0] > 2.4 * a_u.dims[1] || n.find("leek") != std::string::npos || n.find("carrot") != std::string::npos ||
				           n.find("garlic") != std::string::npos || n.find("root") != std::string::npos || n.find("stalk") != std::string::npos;
			}
			a_u.fitted = true;
			return true;
		}

		// Puts a unit lying along x with its middle at a_at, a_len long (and turned a_rot about z)
		void Lay(Unit& a_u, const Vec3& a_at, double a_len, const Quat& a_extra = {})
		{
			if (!Fit(a_u)) {
				Scene::Hide(a_u.prop, true);
				return;
			}
			const double s = a_len / a_u.dims[0];
			const Quat   q = a_extra * a_u.lie;
			const Vec3   c = q.Rotate(a_u.center) * s;
			Scene::Place(a_u.prop, a_at - c, q, static_cast<float>(s));
			Scene::Hide(a_u.prop, false);
		}


		void BuildUnits()
		{
			g_units.clear();
			for (int i = 0; i < static_cast<int>(g_recipe->ings.size()); ++i) {
				const auto& in = g_recipe->ings[i];
				if (in.role == Role::Salt) {
					continue;
				}
				for (int c = 0; c < in.count && static_cast<int>(g_units.size()) < kMaxUnits; ++c) {
					Unit u;
					u.ing = i;
					u.role = in.role;
					u.name = in.name;
					u.model = in.model;
					u.prop = u.model.empty() ? -1 : Scene::Add(u.model);
					if (u.prop < 0) {
						u.prop = Add("Chunk");  // a stand-in if its mesh won't load
						if (u.prop >= 0) {
							u.fitted = true;
							u.dims[0] = 1.0;
							u.dims[1] = 0.8;
							u.dims[2] = 0.7;
							u.boardLen = 5.0;
							u.cut.x1 = 5.0;
						}
					}
					Scene::Hide(u.prop, true);
					const int k = static_cast<int>(g_units.size());
					u.clothAt = { (k < 5 ? -31.5 : -25.5) + kClothShift, 8.5 - (k % 5) * 6.0, 0.0 };
					g_units.push_back(std::move(u));
				}
			}
		}

		int FreeLane()
		{
			for (int l = 0; l < 3; ++l) {
				bool used = false;
				for (const auto& u : g_units) {
					used |= u.at == Unit::At::Board && u.lane == l;
				}
				if (!used) {
					return l;
				}
			}
			return -1;
		}

		// ---------------------------------------------------------------- the pieces
		// How thick it is at a point along its length (round things narrow toward their ends)
		std::pair<double, double> Cross(const Unit& u, double a_x)
		{
			const double L = u.boardLen, dy = Thick(u, L, 1), dz = Thick(u, L, 2);
			const double t = Clamp(std::abs(a_x) * 2.0 / L, 0.0, 1.0);
			double       f = 1.0;
			if (u.dims[0] < 1.6 * u.dims[1]) {
				f = std::sqrt((std::max)(0.04, 1.0 - t * t));  // an apple, a cabbage, a potato
			} else if (t > 0.85) {
				f = std::sqrt((std::max)(0.08, 1.0 - std::pow((t - 0.85) / 0.15, 2.0)));  // a leek, a carrot: only the tips
			}
			return { dy * f * 0.96, dz * f * 0.96 };
		}

		// The colour inside it
		void Flesh(const Unit& u, float (&a_out)[3])
		{
			std::string n = u.name;
			std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			auto has = [&](std::initializer_list<const char*> ws) {
				for (const char* w : ws) {
					if (n.find(w) != std::string::npos) {
						return true;
					}
				}
				return false;
			};
			float c[3] = { 0.92f, 0.88f, 0.76f };
			if (has({ "beef", "venison", "meat", "horker", "mammoth", "goat", "horse", "dog", "rabbit", "haunch", "leg", "steak" })) {
				c[0] = 0.72f, c[1] = 0.3f, c[2] = 0.27f;
			} else if (has({ "salmon" })) {
				c[0] = 0.95f, c[1] = 0.55f, c[2] = 0.4f;
			} else if (has({ "chicken", "pheasant", "fish", "clam", "mudcrab" })) {
				c[0] = 0.93f, c[1] = 0.8f, c[2] = 0.72f;
			} else if (has({ "carrot" })) {
				c[0] = 0.95f, c[1] = 0.55f, c[2] = 0.18f;
			} else if (has({ "tomato" })) {
				c[0] = 0.86f, c[1] = 0.25f, c[2] = 0.15f;
			} else if (has({ "cabbage", "leek" })) {
				c[0] = 0.86f, c[1] = 0.9f, c[2] = 0.68f;
			} else if (has({ "cheese" })) {
				c[0] = 0.96f, c[1] = 0.88f, c[2] = 0.58f;
			} else if (has({ "apple", "pear" })) {
				c[0] = 0.96f, c[1] = 0.92f, c[2] = 0.74f;
			} else if (has({ "potato", "yam", "gourd" })) {
				c[0] = 0.94f, c[1] = 0.88f, c[2] = 0.66f;
			}
			std::memcpy(a_out, c, sizeof(c));
		}

		int NewPiece(int a_unit, int a_body, const double (&a_box)[6], const Vec3& a_mid, bool a_cut)
		{
			auto& u = g_units[a_unit];
			Piece p;
			p.unit = a_unit;
			std::copy(std::begin(a_box), std::end(a_box), p.box);
			p.mid = a_mid;
			p.cut = a_cut;
			p.prop = u.model.empty() ? -1 : Scene::Add(u.model);
			if (p.prop < 0) {
				p.prop = Add("Chunk");  // its mesh won't load: a plain piece
			}
			if (a_cut) {
				p.core = Add("Hull");
			}
			if (static_cast<int>(g_pieces.size()) <= a_body) {
				g_pieces.resize(a_body + 1);
			}
			g_pieces[a_body] = p;
			return a_body;
		}

		// How far apart the pieces of one thing sit on the board, so the cuts show
		double Spread(int a_k, int a_n) { return (a_k - (a_n - 1) * 0.5) * 0.18; }

		void HideBoardBits(Unit& u)
		{
			for (auto id : u.segs) {
				Scene::Hide(id, true);
			}
			for (auto id : u.cores) {
				Scene::Hide(id, true);
			}
		}

		// Its pieces stacked in layers when it's diced both ways (a horizontal cut or two, as a cook would), so a
		// tall thing comes apart in dice rather than in posts
		int Layers(const Unit&) { return 1; }  // pieces go over as they were cut on the board
		// What's on the board goes over: a body per piece, diced pieces in layers, empty corners (past a round thing's
		// edge) left out
		void FinishBoard()
		{
			for (int i = 0; i < static_cast<int>(g_units.size()); ++i) {
				auto& u = g_units[i];
				if (u.at != Unit::At::Board || u.cut.finished) {
					continue;
				}
				const int    nx = u.cut.Pieces(), ny = u.cut.Strips(), nz = Layers(u);
				const bool   cut = nx * ny > 1;
				const double half = u.boardLen * 0.5, w = u.cut.width, h = Thick(u, u.boardLen, 2);
				for (int ix = 0; ix < nx; ++ix) {
					double xa, xb;
					u.cut.Piece(ix, xa, xb);
					xa -= half, xb -= half;
					const auto [cy, cz] = Cross(u, Clamp(0.0, xa, xb));  // its fullest, within this piece
					const double ry = (std::max)(0.2, cy * 0.5), rz = (std::max)(0.2, cz * 0.5);
					for (int iy = 0; iy < ny; ++iy) {
						double ya, yb;
						u.cut.Strip(iy, ya, yb);
						ya -= w * 0.5, yb -= w * 0.5;
						if (!cut || ny == 1) {
							ya = -ry, yb = ry;
						}
						for (int iz = 0; iz < nz; ++iz) {
							double za = -h * 0.5 + h * iz / nz, zb = -h * 0.5 + h * (iz + 1) / nz;
							if (nz == 1) {
								za = -rz, zb = rz;
							}
							// is there anything in this cell?
							const double py = Clamp(0.0, ya, yb) / ry, pz = Clamp(0.0, za, zb) / rz;
							if (py * py + pz * pz > 0.92) {
								continue;
							}
							// the body: what of the cell the thing fills
							const double ey0 = (std::max)(ya, -ry), ey1 = (std::min)(yb, ry), ez0 = (std::max)(za, -rz), ez1 = (std::min)(zb, rz);
							const Vec3   mid{ (xa + xb) * 0.5, (ey0 + ey1) * 0.5, (ez0 + ez1) * 0.5 };
							const Vec3   size{ xb - xa, (std::max)(0.2, ey1 - ey0), (std::max)(0.2, ez1 - ez0) };
							const Vec3   local{ mid.x + Spread(ix, nx), L::kLaneY[u.lane] + mid.y + Spread(iy, ny), L::kBoardH * 0.5 + h * 0.5 + mid.z };
							const int    b = g_chunks.Add(i, local, size, static_cast<unsigned>(g_pieces.size() * 31 + 7));
							if (b < 0) {
								continue;
							}
							const double box[6] = { cut ? xa : -1e7, cut ? xb : 1e7, ny > 1 ? ya : -1e7, ny > 1 ? yb : 1e7, nz > 1 ? za : -1e7, nz > 1 ? zb : 1e7 };
							NewPiece(i, b, box, mid, cut);
							g_chunks.At(b).q = {};  // a fresh piece lies as it was cut, not turned
						}
					}
				}
				u.wentWhole = !cut;
				u.cut.Finish();
				Scene::Hide(u.prop, true);
				HideBoardBits(u);
				u.at = Unit::At::Pot;  // on its way
			}
		}


		// ---------------------------------------------------------------- starting a dish
		void BrothColour()
		{
			std::string s = g_recipe->name;
			for (const auto& in : g_recipe->ings) {
				s += " " + in.name;
			}
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			auto has = [&](const char* w) { return s.find(w) != std::string::npos; };
			const float* c = nullptr;
			static const float tomato[3] = { 0.72f, 0.24f, 0.12f }, green[3] = { 0.55f, 0.52f, 0.26f }, cream[3] = { 0.86f, 0.78f, 0.6f },
							   brown[3] = { 0.46f, 0.27f, 0.13f }, plain[3] = { 0.62f, 0.44f, 0.24f };
			if (has("tomato")) {
				c = tomato;
			} else if (has("milk") || has("cheese") || has("chowder") || has("fondue") || has("cream") || has("potato soup")) {
				c = cream;
			} else if (has("beef") || has("venison") || has("horker") || has("meat") || has("stew") || has("goat") || has("mammoth")) {
				c = brown;
			} else if (has("cabbage") || has("leek") || has("vegetable")) {
				c = green;
			} else {
				c = plain;
			}
			std::memcpy(g_broth, c, sizeof(g_broth));
		}

		void ResetHands()
		{
			g_hold = Hold::None;
			g_holdUnit = -1;
			g_tip = g_tipWant = 0;
			g_tipped = false;
			g_boardY = L::kBoardY;
			g_knifeAnim = -1;
			g_knifeOn = false;
			g_knifeAlong = false;
			g_ladleA = -kPi * 0.6;
			g_ladleR = 3.6;
			g_pinch = 0;
			g_door = g_doorWant = 1.0;
			g_doughParts = 0;
			g_rolled = false;
			g_mix = 0.0;
			g_ball = false;
			g_doughOnBoard = false;
			g_squash = 0;
			g_stock = 0;
			g_peelT = -1;
			g_peelLoaded = false;
			g_peelBread = false;
			g_served = false;
			g_grains.clear();
			g_tossedIn = 0;
			g_crankRun = 0;
			g_stirred = 0;
		}

		void ShowStation()
		{
			// the book over the kitchen as it stands: the fire going, nothing out yet
			g_flow = g_ovenStation ? hc::Flow::Oven : hc::Flow::Pot;
			g_recipe = nullptr;
			g_units.clear();
			g_pieces.clear();
			g_chunks.Clear();
			BuildKitchen(g_ovenStation, g_flow);
			g_fire = hc::Fire(g_ovenStation ? 9.0 : 5.0);
			g_fire.Light(2);
			g_pot.reset();
			g_spit.reset();
			g_oven.reset();
			ResetHands();
			Scene::SetCamera({ 0.0, -1.0, 0.0 }, 96.0f, true);
		}

		void StartCooking(const Recipes::Recipe* a_r)
		{
			g_recipe = a_r;
			g_flow = a_r->flow;
			g_chunks.Clear();
			g_pieces.clear();
			BuildKitchen(a_r->oven, g_flow);
			g_fire = hc::Fire(a_r->oven ? 9.0 : 5.0);
			g_fire.Light(2);
			g_pot.reset();
			g_spit.reset();
			g_oven.reset();
			if (g_flow == hc::Flow::Pot) {
				g_pot = std::make_unique<hc::Pot>(a_r->salted);
				BrothColour();
			} else if (g_flow == hc::Flow::Spit) {
				g_spit = std::make_unique<hc::Spit>(a_r->salted);
			} else {
				g_oven = std::make_unique<hc::Oven>(a_r->pie, a_r->salted, a_r->dough);
			}
			if (a_r->salted) {
				P.salt = KitAdd(Kit::Role::SaltBowl);
				Settle(P.salt, Kit::Role::SaltBowl, { L::kSaltX, L::kSaltY, 0.0 }, { 4.4, 4.4, 0.0 }, 0.0, false, { L::kSaltX, L::kSaltY, 0.0 });
				if (Game(Kit::Role::SaltBowl) && Kit::Get(Kit::Role::Salt).vanilla) {
					P.saltPile = KitAdd(Kit::Role::Salt);
					Settle(P.saltPile, Kit::Role::Salt, { L::kSaltX, L::kSaltY, 0.7 }, { 3.0, 3.0, 0.0 }, 0.0, false, {});
				}
			}
			ResetHands();
			BuildUnits();
			g_doughNeeded = 0;
			for (const auto& u : g_units) {
				g_doughNeeded += u.role == Role::Dough;
			}
			// no dough to make (a dish built on bread already made): the first filling is the base
			if (g_flow == hc::Flow::Oven && g_doughNeeded == 0) {
				g_doughParts = 1;
				g_oven->Knead(5.0, 0.0);
				g_ball = g_doughOnBoard = true;
			}
			// the oven: a pie's fruit and vegetables are cut first, and wait in a bowl
			if (g_flow == hc::Flow::Oven) {
				for (auto& u : g_units) {
					std::string n = u.name;
					std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					const bool soft = n.find("honey") != std::string::npos || n.find("sugar") != std::string::npos || n.find("butter") != std::string::npos ||
					                  n.find("milk") != std::string::npos || n.find("egg") != std::string::npos || n.find("jug") != std::string::npos ||
					                  n.find("sack") != std::string::npos || n.find("bread") != std::string::npos || n.find("jazbay") != std::string::npos ||
					                  n.find("berr") != std::string::npos || n.find("ear") != std::string::npos || n.find("mirriam") != std::string::npos;
					u.dice = g_recipe->pie && u.role == Role::Filling && !soft && !u.model.empty();
				}
				g_chunks.SetBowl({ kFillAt.x, kFillAt.y, 1.0 }, 2.8, 2.6, false);
			} else {
				g_chunks.SetBowl({ L::kPotX, L::kPotY, L::kLiquidZ }, L::kPotR, L::kPotRim, true);
			}
			g_phase = Phase::Cooking;
			g_served = false;
			g_help = false;
			g_confirm = false;
			Scene::SetCamera({ 0.0, -1.0, 0.0 }, 88.0f);
			Audio::Play({ "ui_equip", 0.8f });
			spdlog::info("Cooking {} by hand ({})", a_r->name, g_flow == hc::Flow::Pot ? "pot" : g_flow == hc::Flow::Spit ? "spit" : "oven");
		}

		void CloseKitchen(bool a_closeCrafting)
		{
			Audio::StopAll();
			Scene::Close();
			g_phase = Phase::Idle;
			g_recipe = nullptr;
			g_units.clear();
			g_pieces.clear();
			g_chunks.Clear();
			ShowCrafting(true);
			if (a_closeCrafting) {
				SKSE::GetTaskInterface()->AddTask([] {
					if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
						RE::CraftingMenu::QuitMenu();
					}
				});
			}
		}

		// ---------------------------------------------------------------- serving
		// The finished dish: its mesh, wearing the textures its form gives it (as the inventory shows it)
		Scene::Id AddResult()
		{
			auto* form = g_recipe ? g_recipe->result : nullptr;
			auto* m = form ? form->As<RE::TESModel>() : nullptr;
			if (!m || !m->GetModel() || !*m->GetModel()) {
				return -1;
			}
			const auto id = Scene::Add(m->GetModel());
			auto*      sw = form->As<RE::TESModelTextureSwap>();
			if (id >= 0 && sw && sw->alternateTextures) {
				for (std::uint32_t i = 0; i < sw->numAlternateTextures; ++i) {
					const auto& alt = sw->alternateTextures[i];
					if (!alt.textureSet) {
						continue;
					}
					const char* name = alt.name3D.c_str();
					Scene::SwapTextures(id, name ? name : "", static_cast<int>(alt.index3D), alt.textureSet->textures[0].textureName.c_str(),
						alt.textureSet->textures[1].textureName.c_str());
				}
			}
			return id;
		}

		void Serve()
		{
			if (g_served || !g_recipe) {
				return;
			}
			g_served = true;
			if (g_flow == hc::Flow::Pot) {
				std::vector<double> cuts;
				int                 lost = g_chunks.CountIn(hc::Chunks::State::Lost);
				for (const auto& u : g_units) {
					if (u.role == Role::Chop) {
						cuts.push_back(u.wentWhole ? 0.05 : u.cut.Score());
					}
					if (u.at == Unit::At::Cloth || u.at == Unit::At::Board) {
						++lost;  // never went in
					}
				}
				const int in = g_chunks.CountIn(hc::Chunks::State::Pot) + g_tossedIn;
				g_result = g_pot->Score(cuts.data(), static_cast<int>(cuts.size()), in, lost);
			} else if (g_flow == hc::Flow::Spit) {
				g_result = g_spit->Score();
			} else {
				g_oven->Unload();
				g_result = g_oven->Score();
			}
			Recipes::Consume(*g_recipe);
			g_given = Recipes::Give(*g_recipe, g_result.tier);
			g_givenName = g_given && g_given->GetName() ? g_given->GetName() : g_recipe->name;
			g_burntLook = g_result.tier == hc::Tier::Burnt;
			g_phase = Phase::Result;
			g_serveAt = g_clock;
			Audio::Play({ g_result.tier == hc::Tier::Burnt ? "burnt" : "done", 0.9f });
			spdlog::info("Served {}: {} (quality {:.2f})", g_recipe->name, hc::TierName(g_result.tier), g_result.quality);
			// the dish itself, on the plate or in the bowl
			for (auto& u : g_units) {
				Scene::Hide(u.prop, true);
				HideBoardBits(u);
			}
			for (auto& p : g_pieces) {
				Scene::Hide(p.prop, true);
				Scene::Hide(p.core, true);
			}
			if (P.dough >= 0) {
				Scene::Hide(P.dough, true);
			}
			if (P.disk >= 0) {
				Scene::Hide(P.disk, true);
			}
			for (auto& [id, at] : P.fill) {
				Scene::Hide(id, true);
			}
			for (auto id : P.crimps) {
				Scene::Hide(id, true);
			}
			if (P.bowl >= 0) {
				Scene::Hide(P.bowl, true);
			}
			if (P.result >= 0) {
				Scene::Remove(P.result);
			}
			P.result = AddResult();
			Scene::SetCamera({ L::kServeX, L::kServeY + 2.0, 1.0 }, 42.0f);
		}

		// ---------------------------------------------------------------- picking
		bool NearScreen(const Vec3& a_p, float a_mx, float a_my, double a_r, float a_minPx = 16.0f)
		{
			float sx = 0, sy = 0;
			if (!Scene::Project(a_p, sx, sy)) {
				return false;
			}
			const float r = (std::max)(S(a_minPx), static_cast<float>(a_r) * Scene::PixelsPerUnit(a_p));
			return std::hypot(sx - a_mx, sy - a_my) <= r;
		}

		bool OnPlane(float a_mx, float a_my, double a_z, double& a_x, double& a_y) { return Scene::Unproject(a_mx, a_my, a_z, a_x, a_y); }

		bool OverBoard(float a_mx, float a_my, double& a_lx, double& a_ly)
		{
			double x = 0, y = 0;
			if (!OnPlane(a_mx, a_my, kBoardTop, x, y)) {
				return false;
			}
			a_lx = x - L::kBoardX;
			a_ly = y - g_boardY;
			return std::abs(a_lx) <= L::kBoardW * 0.5 && std::abs(a_ly) <= L::kBoardD * 0.5;
		}

		bool OverPot(float a_mx, float a_my, double a_extra = 0.0)
		{
			double x = 0, y = 0;
			return OnPlane(a_mx, a_my, L::kPotRim, x, y) && std::hypot(x - L::kPotX, y - L::kPotY) <= L::kPotR + a_extra;
		}

		bool OverFire(float a_mx, float a_my)
		{
			if (g_flow == hc::Flow::Oven) {
				double x = 0, y = 0;
				return OnPlane(a_mx, a_my, L::kFireboxZ, x, y) && std::abs(x - L::kOvenX) < 7.0 && y > -9.0 && y < 2.0;
			}
			double x = 0, y = 0;
			return OnPlane(a_mx, a_my, 1.0, x, y) && std::hypot(x - L::kFireX, y - L::kFireY) <= L::kFireR + 1.5;
		}

		bool OverServe(float a_mx, float a_my) { return NearScreen({ L::kServeX, L::kServeY, 1.0 }, a_mx, a_my, 5.0, 40.0f); }

		Vec3 SpitAxis(double a_x) { return { L::kFireX + a_x, L::kFireY, L::kSpitZ }; }
		Vec3 CrankGrip()
		{
			const double a = g_spit ? g_spit->Angle() : 0.0;
			return { L::kFireX + L::kCrankOff + 0.95, L::kFireY - std::sin(a) * L::kCrankR, L::kSpitZ + std::cos(a) * L::kCrankR };
		}

		// all done but the baking: it takes the dish's own shape
		bool DoughReady()
		{
			return g_oven && g_doughOnBoard && g_doughParts > 0 && (g_doughNeeded == 0 || (g_doughParts >= g_doughNeeded && g_oven->Dough() >= 0.9)) &&
			       (!g_oven->Pie() || (g_rolled && g_oven->Crimped() >= 0.9)) && !g_oven->Out();
		}
		bool OverMix(float mx, float my) { return NearScreen(kMixAt + Vec3{ 0, 0, 1.6 }, mx, my, 4.2, 40.0f); }
		bool OverFill(float mx, float my) { return NearScreen(kFillAt + Vec3{ 0, 0, 1.4 }, mx, my, 6.0, 70.0f); }
		bool AnyOnBoard()
		{
			for (const auto& u : g_units) {
				if (u.at == Unit::At::Board) {
					return true;
				}
			}
			return false;
		}
		int InFillBowl()
		{
			int n = 0;
			for (int b = 0; b < g_chunks.Count() && b < static_cast<int>(g_pieces.size()); ++b) {
				n += g_chunks.At(b).state == hc::Chunks::State::Pot && !g_pieces[b].onDough;
			}
			return n;
		}
		Vec3 PeelRest() { return { -1.0, -17.0, 0.3 }; }
		Vec3 MouthAt() { return { L::kOvenX, L::kOvenMouthY - 0.6, L::kOvenFloorZ + 2.5 }; }
		Vec3 BreadAt() { return { L::kOvenX, L::kOvenMouthY + 4.2, L::kOvenFloorZ }; }
		Vec3 DoorCentre() { return { L::kOvenX - 4.9 + std::cos(-g_door * 1.75) * 5.0, L::kOvenMouthY - 0.35 + std::sin(-g_door * 1.75) * 5.0, L::kOvenFloorZ + 3.2 }; }

		int UnitAt(float a_mx, float a_my, Unit::At a_where)
		{
			for (int i = 0; i < static_cast<int>(g_units.size()); ++i) {
				const auto& u = g_units[i];
				if (u.at != a_where) {
					continue;
				}
				Vec3 at = u.clothAt + Vec3{ 0, 0, 1.0 };
				if (a_where == Unit::At::Spit) {
					at = SpitAxis(u.spitX);
				}
				if (NearScreen(at, a_mx, a_my, a_where == Unit::At::Spit ? 3.5 : 3.0, 26.0f)) {
					return i;
				}
			}
			return -1;
		}

		bool AllInPot()
		{
			for (const auto& u : g_units) {
				if (u.at == Unit::At::Cloth || u.at == Unit::At::Board || u.at == Unit::At::Held) {
					return false;
				}
			}
			return true;
		}

		int Mounted()
		{
			int n = 0;
			for (const auto& u : g_units) {
				n += u.at == Unit::At::Spit;
			}
			return n;
		}

		// ---------------------------------------------------------------- particles
		void Spray(const Vec3& a_at, int a_n, const float (&a_col)[4], double a_speed, double a_up, float a_size, bool a_gravity = true,
			Render3D::Sprite::Kind a_kind = Render3D::Sprite::Kind::Grain, double a_life = 0.9)
		{
			for (int k = 0; k < a_n && g_grains.size() < 600; ++k) {
				Grain g;
				g.p = a_at + Vec3{ (Rand01() - 0.5) * 0.6, (Rand01() - 0.5) * 0.6, 0.0 };
				const double a = Rand01() * 2 * kPi;
				g.v = { std::cos(a) * a_speed * Rand01(), std::sin(a) * a_speed * Rand01(), a_up * (0.5 + Rand01()) };
				g.life = a_life * (0.6 + 0.8 * Rand01());
				std::memcpy(g.col, a_col, sizeof(g.col));
				g.size = a_size * (0.7f + 0.6f * Rand01());
				g.kind = a_kind;
				g.gravity = a_gravity;
				g_grains.push_back(g);
			}
		}

		void SaltOn(const Vec3& a_at, double a_amount)
		{
			static const float white[4] = { 0.95f, 0.95f, 0.93f, 1.0f };
			Spray(a_at + Vec3{ 0, 0, 5.0 }, static_cast<int>(10 + a_amount * 40), white, 1.2, -2.0, 0.09f);
			Audio::Take("salt_", 0.6f);
		}

		void Flour(const Vec3& a_at)
		{
			static const float dust[4] = { 0.92f, 0.9f, 0.86f, 0.35f };
			Spray(a_at + Vec3{ 0, 0, 0.8 }, 10, dust, 3.0, 2.0, 1.6f, false, Render3D::Sprite::Kind::Smoke, 1.4);
			Audio::Play({ "flour", 0.7f });
		}

		// ---------------------------------------------------------------- hands: picking up
		void Grab(float mx, float my)
		{
			if (g_phase != Phase::Cooking || g_confirm) {
				return;
			}
			const bool pot = g_flow == hc::Flow::Pot, spit = g_flow == hc::Flow::Spit, oven = g_flow == hc::Flow::Oven;
			// the knife comes down where it is
			if ((pot || oven) && g_knifeOn && g_knifeAnim < 0 && g_tip <= 0.001) {
				g_knifeAnim = 0.0;
				g_knifeCut = false;
				return;
			}
			// the board, by its left end
			if ((pot || (oven && AnyOnBoard() && !g_doughOnBoard)) && P.board >= 0) {
				double lx = 0, ly = 0;
				if (OverBoard(mx, my, lx, ly) && lx < -L::kBoardW * 0.5 + 3.6) {
					bool any = g_chunks.CountIn(hc::Chunks::State::Board) > 0;
					for (const auto& u : g_units) {
						any |= u.at == Unit::At::Board;
					}
					if (any) {
						g_hold = Hold::Board;
						double x = 0, y = 0;
						OnPlane(mx, my, kBoardTop, x, y);
						g_grabX = x;
						g_grabY = y - g_boardY;
						return;
					}
				}
			}
			// the ladle (anywhere in the pot)
			if (pot && (OverPot(mx, my, -0.6) || NearScreen({ L::kPotX + std::cos(g_ladleA) * g_ladleR, L::kPotY + std::sin(g_ladleA) * g_ladleR, L::kLiquidZ + 3.0 }, mx, my, 3.0))) {
				g_hold = Hold::Ladle;
				return;
			}
			// the spit's crank, and the roast on it
			if (spit && NearScreen(CrankGrip(), mx, my, 2.6, 30.0f)) {
				g_hold = Hold::Crank;
				float hx = 0, hy = 0;
				Scene::Project({ L::kFireX + L::kCrankOff, L::kFireY, L::kSpitZ }, hx, hy);
				g_crankPrev = std::atan2(my - hy, mx - hx);
				return;
			}
			if (spit) {
				if (const int k = UnitAt(mx, my, Unit::At::Spit); k >= 0) {
					g_hold = Hold::Meat;
					g_holdUnit = k;
					return;
				}
			}
			// the oven: its door, what's in it, the dough
			if (oven) {
				if (g_oven->InOven() && g_door > 0.6 && g_peelT < 0 && NearScreen(BreadAt() + Vec3{ 0, 0, 1.5 }, mx, my, 4.0, 30.0f)) {
					Toast("Too hot for hands: take it out with the peel");
					return;
				}
				// the bowls: a ball of dough to lift out, flour to stir, the cut filling to carry over
				if (P.mixBowl >= 0 && OverMix(mx, my)) {
					if (g_ball && !g_doughOnBoard) {
						g_hold = Hold::Ball;
						return;
					}
					if (!g_ball && g_doughParts > 0) {
						g_hold = Hold::Mix;
						double x = 0, y = 0;
						if (OnPlane(mx, my, 1.8, x, y)) {
							g_mixA = std::atan2(y - kMixAt.y, x - kMixAt.x);
						}
						return;
					}
				}
				if (P.fillBowl >= 0 && InFillBowl() > 0 && OverFill(mx, my)) {
					g_hold = Hold::Apples;
					return;
				}
				if (P.peel >= 0 && g_peelT < 0 && NearScreen(PeelRest(), mx, my, 4.5, 40.0f)) {
					g_hold = Hold::Peel;
					g_peelLoaded = false;
					g_peelAt = PeelRest();
					return;
				}
				if (NearScreen(DoorCentre(), mx, my, 3.5, 30.0f)) {
					g_hold = Hold::Door;
					g_grabX = mx;
					g_grabY = g_door;
					return;
				}
				if (g_doughParts > 0 && g_doughOnBoard && !g_oven->InOven() && !g_oven->Out()) {
					double x = 0, y = 0;
					if (OnPlane(mx, my, kBoardTop + 1.0, x, y)) {
						const double dx = x - g_doughAt.x, dy = y - g_doughAt.y, r = std::hypot(dx, dy);
						const double R = g_rolled ? 5.0 : 3.2;
						if (g_rolled && std::abs(r - R) < 1.2) {
							g_hold = Hold::Crimp;
							return;
						}
						if (g_rolled && r < R - 1.0 && g_stock > 0) {
							g_hold = Hold::Spread;
							g_lastSpread = { x, y, 0 };
							return;
						}
						if (r < R + 0.8) {
							g_hold = Hold::Dough;
							g_grabX = x;
							g_grabY = y;
							g_strokeLen = 0;
							return;
						}
					}
				}
			}
			// salt
			if (P.salt >= 0 && NearScreen({ L::kSaltX, L::kSaltY, 1.5 }, mx, my, 2.6, 26.0f)) {
				g_hold = Hold::Salt;
				g_pinch = 0;
				return;
			}
			// logs: from the pile, or a burning one out of the fire
			for (int k = 0; k < 3; ++k) {
				if (NearScreen({ L::kPileX, L::kPileY + (k - 1) * 1.6, 1.5 }, mx, my, 3.0, 22.0f)) {
					g_hold = Hold::Log;
					return;
				}
			}
			if (g_fire.Logs() > 0) {
				const Vec3 fc = FireCenter();
				const bool ring = pot ? (!OverPot(mx, my, 0.8) && OverFire(mx, my)) : oven ? OverFire(mx, my) : NearScreen(fc, mx, my, 4.0);
				if (ring && g_fire.RemoveLog()) {
					g_hold = Hold::FireLog;
					Audio::Take("log_", 0.5f);
					return;
				}
			}
			// an ingredient off the cloth
			if (const int k = UnitAt(mx, my, Unit::At::Cloth); k >= 0) {
				g_hold = Hold::Ingredient;
				g_holdUnit = k;
				g_units[k].at = Unit::At::Held;
				return;
			}
		}

		// ---------------------------------------------------------------- hands: carrying
		void Carry(float mx, float my, double dt)
		{
			double x = 0, y = 0;
			switch (g_hold) {
			case Hold::Ingredient:
			case Hold::Log:
			case Hold::FireLog:
			case Hold::Meat:
			case Hold::Bread:
				if (OnPlane(mx, my, kCarryZ, x, y)) {
					g_carry = { x, y, kCarryZ };
				}
				break;
			case Hold::Board:
				if (OnPlane(mx, my, kBoardTop, x, y)) {
					g_tipWant = Clamp((x - g_grabX) / 10.0, 0.0, 1.0);
					g_boardY = Clamp(y - g_grabY, L::kPotY - 8.0, L::kPotY + 8.0);
				}
				break;
			case Hold::Ladle:
				if (OnPlane(mx, my, L::kLiquidZ, x, y)) {
					const double dx = x - L::kPotX, dy = y - L::kPotY, r = std::hypot(dx, dy);
					if (r > L::kPotR + 3.0) {
						g_hold = Hold::LadleOut;  // lifted out
						Audio::Take("splash_", 0.35f);
						break;
					}
					const double a = std::atan2(dy, dx);
					const double dA = Wrap(a - g_ladleA);
					g_ladleA = a;
					g_ladleR = Clamp(r, 0.8, L::kPotR - 2.6);  // the cup well in, so the handle clears the rim
					if (g_pot && std::abs(dA) < 1.5) {
						g_pot->Stir(dA);
						g_stirred += std::abs(dA);
						if (g_stirred > 1.4 && Audio::Now() - g_lastStir > 0.25) {
							g_stirred = 0;
							g_lastStir = Audio::Now();
							Audio::Take("stir_", 0.5f);
						}
					}
				}
				break;
			case Hold::LadleOut:
				if (OnPlane(mx, my, kCarryZ, x, y)) {
					g_ladleOutAt = { x, y, kCarryZ };
					if (std::hypot(x - L::kPotX, y - L::kPotY) < L::kPotR - 1.0) {
						g_hold = Hold::Ladle;  // back in
					}
				}
				break;
			case Hold::Salt:
				g_pinch = (std::min)(1.4, g_pinch + dt / 0.5);
				if (OnPlane(mx, my, kCarryZ, x, y)) {
					g_carry = { x, y, kCarryZ };
				}
				break;
			case Hold::Crank:
				{
					float hx = 0, hy = 0;
					if (g_spit && Scene::Project({ L::kFireX + L::kCrankOff, L::kFireY, L::kSpitZ }, hx, hy)) {
						const double a = std::atan2(my - hy, mx - hx);
						const double dA = Wrap(a - g_crankPrev);
						g_crankPrev = a;
						if (std::abs(dA) < 1.2) {
							g_spit->Turn(dA);
							g_crankRun += std::abs(dA);
							if (g_crankRun > kPi / 3) {
								g_crankRun = 0;
								Audio::Take("crank_", 0.45f);
							}
						}
					}
				}
				break;
			case Hold::Mix:
				if (OnPlane(mx, my, 1.8, x, y)) {
					const double a = std::atan2(y - kMixAt.y, x - kMixAt.x), dA = Wrap(a - g_mixA);
					g_mixA = a;
					if (std::abs(dA) < 1.5 && g_doughParts >= (std::max)(1, g_doughNeeded)) {
						g_mix = (std::min)(1.0, g_mix + std::abs(dA) / (2.0 * kPi * 4.0));  // four turns round
						g_stirred += std::abs(dA);
						if (g_stirred > 1.6 && Audio::Now() - g_lastStir > 0.3) {
							g_stirred = 0;
							g_lastStir = Audio::Now();
							Audio::Take("knead_", 0.35f);
						}
						if (g_mix >= 1.0) {
							g_ball = true;
							g_hold = Hold::None;
							Toast("A ball of dough: lift it onto the board");
						}
					} else if (g_doughParts < (std::max)(1, g_doughNeeded) && Audio::Now() - g_lastStir > 1.5) {
						g_lastStir = Audio::Now();
						Toast("Everything for the dough goes in the bowl first");
					}
				}
				break;
			case Hold::Ball:
			case Hold::Apples:
				if (OnPlane(mx, my, kCarryZ, x, y)) {
					g_carry = { x, y, kCarryZ };
				}
				break;
			case Hold::Peel:
				if (OnPlane(mx, my, kBoardTop + 1.0, x, y)) {
					g_peelAt = { x, y, kBoardTop + 1.2 };
					// into the open oven for what's baking
					if (!g_peelBread && g_oven->InOven() && g_door > 0.6 && NearScreen(MouthAt(), mx, my, 6.0, 50.0f)) {
						g_oven->Unload();
						g_peelBread = true;
						Audio::Play({ "peel", 0.7f });
					}
					// slid under the dough: it's on
					if (!g_peelLoaded && !g_peelBread && !g_oven->InOven() && !g_oven->Out() && g_doughOnBoard && std::hypot(x - g_doughAt.x, y - g_doughAt.y) < 3.5) {
						if (g_doughNeeded > 0 && g_doughParts < g_doughNeeded) {
							if (Audio::Now() - g_lastStir > 1.5) {
								g_lastStir = Audio::Now();
								Toast("The dough isn't all in yet");
							}
						} else {
							g_peelLoaded = true;
							Audio::Take("thunk_", 0.4f);
						}
					}
				}
				break;
			case Hold::Dough:
				if (OnPlane(mx, my, kBoardTop + 1.0, x, y)) {
					// kneading stays on the board: the dough never leaves it (the peel carries it to the oven)
					x = Clamp(x, L::kBoardX - L::kBoardW * 0.5 + 3.0, L::kBoardX + L::kBoardW * 0.5 - 3.0);
					y = Clamp(y, L::kBoardY - L::kBoardD * 0.5 + 3.0, L::kBoardY + L::kBoardD * 0.5 - 3.0);
					const double dx = x - g_grabX, dy = y - g_grabY;
					g_strokeLen = std::hypot(dx, dy);
					g_squash = Clamp(g_strokeLen / 6.0, 0.0, 1.0);
					g_squashA = std::atan2(dy, dx);
					g_carry = { x, y, kCarryZ };
				}
				break;
			case Hold::Spread:
				if (OnPlane(mx, my, kBoardTop + 1.0, x, y) && g_stock > 0) {
					const double dx = x - g_doughAt.x, dy = y - g_doughAt.y;
					if (std::hypot(x - g_lastSpread.x, y - g_lastSpread.y) > 0.7 && std::hypot(dx, dy) < 4.6) {
						g_lastSpread = { x, y, 0 };
						g_oven->Fill(dx / 5.0, dy / 5.0);
						--g_stock;
						const auto id = Add("Chunk");
						P.fill.emplace_back(id, Vec3{ dx, dy, static_cast<double>(g_lastFill) });
						Audio::Take("knead_", 0.15f, 0.2f);
					}
				}
				break;
			case Hold::Crimp:
				if (OnPlane(mx, my, kBoardTop + 1.0, x, y)) {
					const double dx = x - g_doughAt.x, dy = y - g_doughAt.y;
					if (std::abs(std::hypot(dx, dy) - 5.0) < 1.6) {
						const double a = std::atan2(dy, dx);
						const int    before = static_cast<int>(std::lround(g_oven->Crimped() * hc::Oven::kCrimps));
						g_oven->Crimp(a);
						const int after = static_cast<int>(std::lround(g_oven->Crimped() * hc::Oven::kCrimps));
						if (after > before) {
							Audio::Take("knead_", 0.2f, 0.3f);
						}
					}
				}
				break;
			case Hold::Door:
				{
					const float px = Scene::PixelsPerUnit({ L::kOvenX, L::kOvenMouthY, L::kOvenFloorZ });
					const double want = Clamp(g_grabY + (g_grabX - mx) / (px * 9.0), 0.0, 1.0);
					if ((want > 0.5) != (g_doorWant > 0.5)) {
						Audio::Play({ want > 0.5 ? "door_open" : "door_close", 0.6f });
					}
					g_doorWant = want;
				}
				break;
			default:
				break;
			}
		}

		// ---------------------------------------------------------------- hands: letting go
		void PutBack(int a_unit)
		{
			if (a_unit >= 0 && a_unit < static_cast<int>(g_units.size())) {
				g_units[a_unit].at = Unit::At::Cloth;
			}
		}

		void Drop(float mx, float my)
		{
			const Hold h = g_hold;
			g_hold = Hold::None;
			const int ui = g_holdUnit;
			g_holdUnit = -1;
			switch (h) {
			case Hold::Ingredient:
				{
					if (ui < 0) {
						break;
					}
					auto& u = g_units[ui];
					double lx = 0, ly = 0;
					if (g_flow == hc::Flow::Pot) {
						if (u.role == Role::Chop && P.board >= 0 && g_tip < 0.05 && OverBoard(mx, my, lx, ly)) {
							const int lane = FreeLane();
							if (lane >= 0 && Fit(u)) {
								u.at = Unit::At::Board;
								u.lane = lane;
								Audio::Take("thunk_", 0.4f);
								break;
							}
							Toast("The board is full");
						} else if (OverPot(mx, my, 1.0)) {
							u.at = Unit::At::Pot;
							if (u.role == Role::Chop) {
								// in whole: a piece the size of the thing
								Fit(u);
								u.wentWhole = true;
								const double len = u.boardLen;
								const int    b = g_chunks.AddFree(ui, g_carry, {}, { len, Thick(u, len, 1), Thick(u, len, 2) }, static_cast<unsigned>(ui * 13 + 5));
								if (b >= 0) {
									const double box[6] = { -1e7, 1e7, -1e7, 1e7, -1e7, 1e7 };
									NewPiece(ui, b, box, {}, false);
								}
							} else {
								++g_tossedIn;
								Audio::Play({ u.role == Role::Toss && (u.name.find("Ale") != std::string::npos || u.name.find("Milk") != std::string::npos ||
																	u.name.find("Wine") != std::string::npos || u.name.find("Mead") != std::string::npos)
												  ? "pour"
												  : "splash_0",
									0.7f });
								static const float drop[4] = { 0.5f, 0.35f, 0.2f, 0.9f };
								Spray({ L::kPotX, L::kPotY, L::kLiquidZ }, 10, drop, 4.0, 18.0, 0.18f);
							}
							break;
						}
						PutBack(ui);
					} else if (g_flow == hc::Flow::Spit) {
						if (u.role == Role::Meat && NearScreen(SpitAxis(0.0), mx, my, 11.0, 60.0f)) {
							int n = Mounted();
							u.at = Unit::At::Spit;
							Fit(u);
							static constexpr double kSpots[3] = { 0.0, -5.0, 5.0 };
							u.spitX = kSpots[(std::min)(n, 2)];
							Audio::Take("log_", 0.3f);
							break;
						}
						if (u.role != Role::Meat && Mounted() > 0 && NearScreen(SpitAxis(0.0), mx, my, 9.0, 50.0f)) {
							u.at = Unit::At::Gone;  // rubbed on
							Audio::Take("knead_", 0.4f);
							break;
						}
						PutBack(ui);
					} else {
						double x = 0, y = 0;
						const bool onDough = OnPlane(mx, my, kBoardTop + 1.0, x, y) && std::hypot(x - g_doughAt.x, y - g_doughAt.y) < 7.0;
						if (u.dice) {
							// cut on the board first (while the board's free of dough)
							if (P.board >= 0 && OverBoard(mx, my, lx, ly)) {
								const int lane = g_doughOnBoard ? -1 : FreeLane();
								if (lane >= 0 && Fit(u)) {
									u.at = Unit::At::Board;
									u.lane = lane;
									Audio::Take("thunk_", 0.4f);
									break;
								}
								Toast(g_doughOnBoard ? "The dough has the board: cut it first next time" : "The board is full");
							} else {
								Toast(fmt::format("Cut the {} on the board first", u.name));
							}
							PutBack(ui);
							break;
						}
						if (u.role == Role::Dough && P.mixBowl >= 0 && OverMix(mx, my) && !g_ball) {
							u.at = Unit::At::Dough;
							++g_doughParts;
							Flour(kMixAt + Vec3{ 0, 0, 1.0 });
							break;
						}
						if (u.role == Role::Dough) {
							Toast("Into the bowl of water, to make the dough");
							PutBack(ui);
							break;
						}
						if (u.role == Role::Filling && g_doughOnBoard && onDough && !g_oven->InOven()) {
							u.at = Unit::At::Dough;
							if (g_oven->Pie()) {
								if (!g_rolled) {
									g_rolled = true;
									Flour(g_doughAt);
								}
								g_stock += 12;
								g_lastFill = ui;
								// a first dollop where it landed
								for (int k = 0; k < 3; ++k) {
									const double ox = (x - g_doughAt.x) + (Rand01() - 0.5) * 1.4, oy = (y - g_doughAt.y) + (Rand01() - 0.5) * 1.4;
									if (std::hypot(ox, oy) < 4.6) {
										g_oven->Fill(ox / 5.0, oy / 5.0);
										const auto id = Add("Chunk");
										P.fill.emplace_back(id, Vec3{ ox, oy, static_cast<double>(ui) });
										--g_stock;
									}
								}
								Audio::Take("knead_", 0.4f);
							} else {
								Audio::Take("knead_", 0.5f);  // worked into the dough
							}
							break;
						}
						PutBack(ui);
					}
				}
				break;
			case Hold::Log:
				if (OverFire(mx, my) && g_fire.AddLog()) {
					Audio::Take("log_", 0.8f);
					static const float spark[4] = { 1.0f, 0.55f, 0.15f, 1.0f };
					Spray(FireCenter() + Vec3{ 0, 0, 1.0 }, 18, spark, 4.0, 12.0, 0.12f, false, Render3D::Sprite::Kind::Ember, 1.2);
				} else if (OverFire(mx, my)) {
					Toast("The fire can't take another log");
				}
				break;
			case Hold::FireLog:
				if (OverFire(mx, my)) {
					g_fire.AddLog();  // put back
				} else {
					static const float smoke[4] = { 0.3f, 0.28f, 0.26f, 0.5f };
					Spray(g_carry, 6, smoke, 1.5, 3.0, 1.4f, false, Render3D::Sprite::Kind::Smoke, 1.6);
					Audio::Take("hiss_", 0.4f);
				}
				break;
			case Hold::Board:
				g_tipWant = 0.0;
				if (g_flow == hc::Flow::Oven && OverFill(mx, my)) {
					// swept off the board into the bowl
					FinishBoard();
					for (int b = 0; b < g_chunks.Count(); ++b) {
						g_chunks.Launch(b);
					}
					Audio::Take("splash_", 0.25f);
				}
				break;
			case Hold::Mix:
				break;
			case Hold::Ball:
				{
					double lx = 0, ly = 0;
					if (P.board >= 0 && OverBoard(mx, my, lx, ly)) {
						if (AnyOnBoard()) {
							Toast("Clear the board first");
						} else {
							g_doughOnBoard = true;
							Flour(g_doughAt);
						}
					}
				}
				break;
			case Hold::Apples:
				{
					double x = 0, y = 0;
					const bool on = OnPlane(mx, my, kBoardTop + 1.0, x, y) && std::hypot(x - g_doughAt.x, y - g_doughAt.y) < 6.0;
					if (!on || !g_doughOnBoard) {
						break;
					}
					if (g_doughNeeded > 0 && g_oven->Dough() < 0.9) {
						Toast("Knead the dough first");
						break;
					}
					// onto the dough, heaped in the middle (the edge is left for folding)
					int k = 0;
					for (int b = 0; b < g_chunks.Count() && b < static_cast<int>(g_pieces.size()); ++b) {
						auto& body = g_chunks.At(b);
						auto& pc = g_pieces[b];
						if (body.state != hc::Chunks::State::Pot || pc.onDough) {
							continue;
						}
						const double r = 0.55 * std::sqrt(static_cast<double>(k) + 0.5), a = k * 2.39996;
						pc.onDough = true;
						pc.doughOff = { std::cos(a) * (std::min)(r, 3.0), std::sin(a) * (std::min)(r, 3.0), 0.35 + 0.3 * (k / 14) };
						pc.turn = Hash(b, 41) * 6.28;
						body.state = hc::Chunks::State::Lost;
						g_units[pc.unit].at = Unit::At::Dough;
						++k;
					}
					if (k > 0) {
						if (!g_rolled) {
							g_rolled = true;  // flattened out to take it
						}
						for (int r = 0; r < 2; ++r) {
							for (int a = 0; a < 16; ++a) {
								const double ang = a / 16.0 * 2.0 * kPi, rad = r ? 0.75 : 0.35;
								g_oven->Fill(std::cos(ang) * rad, std::sin(ang) * rad);
							}
						}
						Audio::Take("knead_", 0.45f);
					}
				}
				break;
			case Hold::LadleOut:
				if (OverServe(mx, my)) {
					if (g_chunks.CountIn(hc::Chunks::State::Pot) + g_tossedIn > 0) {
						Audio::Take("splash_", 0.6f);
						Serve();
					} else {
						Toast("There's nothing in the pot yet");
					}
				}
				break;
			case Hold::Salt:
				{
					Vec3 target;
					bool hit = false;
					if (g_flow == hc::Flow::Pot && OverPot(mx, my, 1.0)) {
						target = { L::kPotX, L::kPotY, L::kLiquidZ };
						g_pot->Seasoning().amount += g_pinch;
						hit = true;
					} else if (g_flow == hc::Flow::Spit && Mounted() > 0 && NearScreen(SpitAxis(0.0), mx, my, 9.0, 50.0f)) {
						target = SpitAxis(0.0);
						g_spit->Seasoning().amount += g_pinch;
						hit = true;
					} else if (g_flow == hc::Flow::Oven && g_doughParts > 0 && !g_oven->InOven() && NearScreen(g_doughAt, mx, my, 5.0, 40.0f)) {
						target = g_doughAt;
						g_oven->Seasoning().amount += g_pinch;
						hit = true;
					}
					if (hit) {
						SaltOn(target, g_pinch);
					} else {
						SaltOn({ g_carry.x, g_carry.y, 0.0 }, g_pinch * 0.5);  // spilt
					}
					g_pinch = 0;
				}
				break;
			case Hold::Meat:
				if (OverServe(mx, my)) {
					Serve();
				}
				break;
			case Hold::Peel:
				if (g_peelBread) {
					if (OverServe(mx, my)) {
						Serve();  // off the peel onto the plate
						g_peelBread = false;
					}
					break;  // else it rests on the peel
				}
				if (g_peelLoaded && NearScreen(MouthAt(), mx, my, 6.0, 50.0f)) {
					if (g_door < 0.6) {
						Toast("Open the oven door first");
						g_peelLoaded = false;  // the dough slides back onto the board
					} else {
						g_peelFrom = g_peelAt;
						g_peelT = 0.0;  // in it goes
						g_peelIn = true;
						Audio::Play({ "peel", 0.7f });
					}
				} else if (g_peelLoaded) {
					g_peelLoaded = false;  // put down: the dough back on the board
					Toast("Carry it into the open oven");
				}
				g_peelLoaded = g_peelLoaded && g_peelT >= 0;
				break;
			case Hold::Dough:
				{
					double x = 0, y = 0;
					const bool board = OnPlane(mx, my, kBoardTop + 1.0, x, y) && std::abs(x - L::kBoardX) < L::kBoardW * 0.5 + 1.0 && std::abs(y - L::kBoardY) < L::kBoardD * 0.5 + 1.0;
					if (NearScreen(MouthAt(), mx, my, 6.0, 50.0f)) {
						Toast("Use the peel: drag it under the dough, then into the oven");
					} else if (board && g_strokeLen > 1.2 && !g_rolled && g_doughParts > 0) {
						g_oven->Knead(g_strokeLen, g_clock);
						Audio::Take("knead_", 0.7f);
						static const float dust[4] = { 0.92f, 0.9f, 0.86f, 0.18f };
						Spray(g_doughAt + Vec3{ 0, 0, 1.0 }, 3, dust, 2.0, 1.0, 1.2f, false, Render3D::Sprite::Kind::Smoke, 1.0);
					}
					g_squash = 0;
				}
				break;
			case Hold::Door:
				g_doorWant = g_doorWant > 0.5 ? 1.0 : 0.0;
				break;
			case Hold::Bread:
				if (OverServe(mx, my)) {
					Audio::Play({ "peel", 0.7f });
					Serve();
				}
				break;
			default:
				break;
			}
		}

		void Cancel()
		{
			if (g_hold == Hold::Ingredient) {
				PutBack(g_holdUnit);
			} else if (g_hold == Hold::FireLog) {
				g_fire.AddLog();
			} else if (g_hold == Hold::Board) {
				g_tipWant = 0;
			}
			g_hold = Hold::None;
			g_holdUnit = -1;
			g_pinch = 0;
			g_squash = 0;
		}

		void HandleInput(double dt)
		{
			float mx = 0, my = 0;
			Overlay::MousePos(mx, my);
			for (const auto& p : Overlay::TakePresses()) {
				if (p.button == 1 && p.down) {
					if (g_hold == Hold::None && (g_flow == hc::Flow::Pot || g_flow == hc::Flow::Oven) && g_phase == Phase::Cooking) {
						g_knifeAlong = !g_knifeAlong;  // the knife turns: across, along
					}
					Cancel();
					continue;
				}
				if (p.button != 0) {
					continue;
				}
				if (p.down) {
					if (!p.onUi && g_hold == Hold::None) {
						Grab(p.x, p.y);
					}
				} else if (g_hold != Hold::None) {
					Drop(p.x, p.y);
				}
			}
			if (g_hold != Hold::None && !Overlay::ButtonHeld(0)) {
				Drop(mx, my);  // a release that never arrived
			}
			if (g_hold != Hold::None) {
				Carry(mx, my, dt);
			}
			// the knife follows the mouse over the board
			g_knifeOn = false;
			if ((g_flow == hc::Flow::Pot || (g_flow == hc::Flow::Oven && AnyOnBoard())) && g_hold == Hold::None && P.knife >= 0 && g_tip < 0.01 && g_phase == Phase::Cooking) {
				double lx = 0, ly = 0;
				if (OverBoard(mx, my, lx, ly) && lx > -L::kBoardW * 0.5 + 3.6) {
					int    best = -1;
					double bd = 1e9;
					for (int l = 0; l < 3; ++l) {
						for (const auto& u : g_units) {
							if (u.at == Unit::At::Board && u.lane == l && std::abs(ly - L::kLaneY[l]) < bd) {
								bd = std::abs(ly - L::kLaneY[l]);
								best = l;
							}
						}
					}
					g_knifeOn = true;
					g_knifeLane = best;
					if (g_knifeAnim < 0) {
						if (g_knifeAlong && best >= 0) {
							// along the food: over its middle, wherever across it the mouse is
							double w = 1.0;
							for (const auto& u : g_units) {
								if (u.at == Unit::At::Board && u.lane == best) {
									w = (std::max)(w, u.cut.width);
								}
							}
							g_knifeX = L::kBoardX;
							g_knifeY = g_boardY + L::kLaneY[best] + Clamp(ly - L::kLaneY[best], -w * 0.5 + 0.2, w * 0.5 - 0.2);
						} else {
							g_knifeX = L::kBoardX + Clamp(lx, -L::kBoardW * 0.5 + 1.0, L::kBoardW * 0.5 - 1.0);
							g_knifeY = g_boardY + (best >= 0 ? L::kLaneY[best] : ly);
						}
					}
				}
			}
		}

		// ---------------------------------------------------------------- every frame: the kitchen moves
		void Step(double dt)
		{
			g_fire.Step(dt);
			// the knife's chop lands a moment after the click
			if (g_knifeAnim >= 0) {
				g_knifeAnim += dt;
				if (!g_knifeCut && g_knifeAnim >= 0.06) {
					g_knifeCut = true;
					bool cut = false;
					for (int i = 0; i < static_cast<int>(g_units.size()); ++i) {
						auto& u = g_units[i];
						if (u.at != Unit::At::Board || u.lane != g_knifeLane || u.cut.finished) {
							continue;
						}
						if (g_knifeAlong && u.thin) {
							Toast(fmt::format("A {} is only cut across", u.name));
							continue;
						}
						if ((g_knifeAlong ? u.cut.alongCount : u.cut.cutCount) >= hc::Cutting::kMaxCuts) {
							Toast(g_knifeAlong ? "Six cuts along is enough" : "Six cuts across is enough");
							continue;
						}
						if (g_knifeAlong) {
							const double y = (g_knifeY - g_boardY - L::kLaneY[u.lane]) + u.cut.width * 0.5;
							cut |= u.cut.ChopAlong(y);
						} else {
							const double x = (g_knifeX - L::kBoardX) + u.boardLen * 0.5;
							cut |= u.cut.Chop(x);
						}
					}
					Audio::Take(cut ? "chop_" : "thunk_", cut ? 0.8f : 0.5f, 0.08f, static_cast<float>(Clamp((g_knifeX - 0.0) / 40.0, -0.8, 0.8)));
				}
				if (g_knifeAnim > 0.2) {
					g_knifeAnim = -1;
				}
			}
			if (g_flow == hc::Flow::Pot && g_pot) {
				// the board tips toward what's asked, and eases back
				g_tip += Clamp(g_tipWant - g_tip, -dt * 2.2, dt * 1.6);
				if (g_tip > 0.25 && !g_tipped) {
					g_tipped = true;
					FinishBoard();
				}
				if (g_tip < 0.02 && g_tipped && g_chunks.CountIn(hc::Chunks::State::Free) == 0) {
					g_tipped = false;  // ready for another lot
				}
				g_boardPrev = g_boardPose;
				g_boardPose = hc::BoardPose(g_tip, g_boardY);
				g_boardPose.v = dt > 0 ? (g_boardPose.c - g_boardPrev.c) * (1.0 / dt) : Vec3{};
				const int before = g_chunks.CountIn(hc::Chunks::State::Pot);
				g_chunks.Step(dt, g_boardPose, g_pot->Swirl());
				hc::Chunks::Event ev[32];
				const int         n = g_chunks.TakeEvents(ev, 32);
				for (int k = 0; k < n; ++k) {
					const auto& e = ev[k];
					const float pan = static_cast<float>(Clamp(e.at.x / 40.0, -0.8, 0.8));
					if (e.kind == hc::Chunks::Event::Kind::Splash) {
						Audio::Take("splash_", static_cast<float>(Clamp(0.3 + e.speed / 60.0, 0.3, 0.9)), 0.1f, pan);
						static const float drop[4] = { 0.55f, 0.4f, 0.25f, 0.9f };
						float c[4] = { g_broth[0] * 0.9f, g_broth[1] * 0.9f, g_broth[2] * 0.9f, 0.9f };
						(void)drop;
						Spray(e.at, 6, c, 3.0, 14.0, 0.16f);
					} else if (e.kind == hc::Chunks::Event::Kind::Land) {
						const double fx = e.at.x - L::kFireX, fy = e.at.y - L::kFireY;
						if (fx * fx + fy * fy < L::kFireR * L::kFireR) {
							Audio::Take("hiss_", 0.5f, 0.1f, pan);
						} else {
							Audio::Take("thunk_", 0.35f, 0.1f, pan);
						}
					}
				}
				const int after = g_chunks.CountIn(hc::Chunks::State::Pot);
				if (after > before) {
					g_pot->AddPieces(before, after - before);
				}
				g_pot->Step(dt * kPotSpeed, g_fire.Heat(), after + g_tossedIn > 0);
				g_chunks.Cook(dt * kPotSpeed * 0.02 * Clamp(g_pot->Temp() - 0.3, 0.0, 1.0));
				const double t = g_pot->Temp();
				Audio::SetLoop("loop_simmer", static_cast<float>(Clamp((t - 0.38) * 3.0, 0.0, 0.7)));
				Audio::SetLoop("loop_boil", static_cast<float>(Clamp((t - 0.8) * 4.0, 0.0, 0.8)));
			}
			if (g_flow == hc::Flow::Spit && g_spit) {
				const int m = Mounted();
				g_spit->Step(dt * kSpitSpeed, g_fire.Heat(), m > 0);
				Audio::SetLoop("loop_sizzle", m > 0 ? static_cast<float>(Clamp((g_fire.Heat() - 0.3) * 1.4, 0.0, 0.8)) : 0.0f);
			}
			if (g_flow == hc::Flow::Oven && g_oven) {
				g_door += Clamp(g_doorWant - g_door, -dt * 3.0, dt * 3.0);
				g_oven->SetDoor(g_door > 0.5);
				g_oven->Step(dt * kOvenSpeed, g_fire.Heat());
				// the cut filling, flying into its bowl
				g_chunks.Step(dt, g_boardPose, 0.0);
				hc::Chunks::Event ev[32];
				if (g_chunks.TakeEvents(ev, 32) > 0) {
					Audio::Take("thunk_", 0.25f);
				}
				if (g_peelT >= 0) {
					g_peelT += dt / 0.7;
					if (g_peelT >= 1.0) {
						g_peelT = -1;
						if (g_peelIn) {
							g_oven->Load();
							Toast("Shut the door, and let it bake");
						}
					}
				}
			}
			Audio::SetLoop("loop_fire", static_cast<float>(Clamp(g_fire.Heat() * 0.75, 0.0, 0.85)));
			// the fire pops now and then
			if (Rand01() < dt * (0.5 + 3.0 * g_fire.Heat())) {
				Audio::Take("crackle_", 0.25f + 0.3f * Rand01(), 0.15f, g_flow == hc::Flow::Oven ? 0.3f : 0.4f);
			}
			// the grains, drops, sparks and puffs
			for (auto& g : g_grains) {
				g.age += dt;
				if (g.gravity) {
					g.v.z -= 60.0 * dt;
				} else {
					g.v = g.v * (1.0 / (1.0 + dt * 1.5));
					g.v.z += (g.kind == Render3D::Sprite::Kind::Smoke ? 3.0 : 6.0) * dt;
				}
				g.p += g.v * dt;
				if (g.p.z < 0.05) {
					g.p.z = 0.05;
					g.v = {};
				}
			}
			std::erase_if(g_grains, [](const Grain& g) { return g.age > g.life; });
		}

		// ---------------------------------------------------------------- every frame: the kitchen as it looks
		// The ingredient's own mesh, laid in the frame (x along it, y across, z up) with its middle at a_c, cut to the
		// box (a bound past 1e6 is no cut); the inside shows through the cuts as solid flesh
		// Cuts a thing's look to the box (frame axes about a_c; a bound past 1e6 is no cut)
		void ClipTo(Scene::Look& lk, const Vec3& a_c, const Quat& a_frame, const double (&a_box)[6], bool a_cut)
		{
			for (int k = 0; k < 6; ++k) {
				const int    axis = k / 2;
				const bool   hi = (k & 1) != 0;
				const double v = a_box[k];
				const Vec3   n = a_frame.Rotate({ axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0 }) * (hi ? 1.0 : -1.0);
				const bool   on = a_cut && std::abs(v) < 1e6;
				lk.clips[k][0] = on ? static_cast<float>(n.x) : 0.0f;
				lk.clips[k][1] = on ? static_cast<float>(n.y) : 0.0f;
				lk.clips[k][2] = on ? static_cast<float>(n.z) : 0.0f;
				lk.clips[k][3] = static_cast<float>(n.Dot(a_c) + (hi ? v : -v));
			}
			std::memset(lk.shape, 0, sizeof(lk.shape));
		}

		// A piece of a thing: its own mesh (the skin) cut to the box, and inside it the core - a closed egg of its
		// flesh the thing's size, cut the same, whose cut faces are drawn solid. Laid in the frame (x along it, y
		// across, z up) with the whole's middle at a_c. Nothing can be seen through.
		void PlacePiece(Scene::Id a_id, Scene::Id a_core, Unit& u, const Vec3& a_c, const Quat& a_frame, const double (&a_box)[6], bool a_cut)
		{
			if (!Fit(u)) {
				Scene::Hide(a_id, true);
				Scene::Hide(a_core, true);
				return;
			}
			const double s = u.boardLen / u.dims[0];
			const Quat   q = a_frame * u.lie;
			Scene::Place(a_id, a_c - q.Rotate(u.center) * s, q, static_cast<float>(s));
			Scene::Hide(a_id, false);
			auto& lk = Scene::LookOf(a_id);
			ClipTo(lk, a_c, a_frame, a_box, a_cut);
			float fl[3];
			Flesh(u, fl);
			std::copy(fl, fl + 3, lk.flesh);
			lk.flesh[3] = a_cut ? 3.0f : 0.0f;  // seen from inside, the skin shows its flesh
			if (a_core < 0) {
				return;
			}
			// only a die needs a solid (one from the middle has no skin at all); a slice is whole round its edge, and
			// its skin's inside shows as flesh
			const bool diced = std::abs(a_box[2]) < 1e6 || std::abs(a_box[3]) < 1e6 || std::abs(a_box[4]) < 1e6 || std::abs(a_box[5]) < 1e6;
			if (!a_cut || !diced) {
				Scene::Hide(a_core, true);
				return;
			}
			// the solid: what of the cell the thing's rounded shape fills, found exactly by the shader in a box round it
			const double k = 0.92;  // inside the skin; the skin's own inside (drawn as flesh) rims it
			const double R[3] = { u.boardLen * 0.5 * k, (std::max)(0.05, Thick(u, u.boardLen, 1) * 0.5) * k, (std::max)(0.05, Thick(u, u.boardLen, 2) * 0.5) * k };
			double       lo[3], hi[3];
			for (int ax = 0; ax < 3; ++ax) {
				lo[ax] = (std::max)(a_box[ax * 2], -R[ax]);
				hi[ax] = (std::min)(a_box[ax * 2 + 1], R[ax]);
				if (hi[ax] <= lo[ax]) {
					Scene::Hide(a_core, true);
					return;
				}
			}
			const Vec3 mid{ (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5 };
			Scene::Place(a_core, a_c + a_frame.Rotate(mid), a_frame, 1.0f);
			Scene::Stretch(a_core, static_cast<float>((hi[0] - lo[0]) * 1.04), static_cast<float>((hi[1] - lo[1]) * 1.04), static_cast<float>((hi[2] - lo[2]) * 1.04));
			Scene::Hide(a_core, false);
			auto& cl = Scene::LookOf(a_core);
			ClipTo(cl, a_c, a_frame, a_box, true);
			std::copy(fl, fl + 3, cl.flesh);
			cl.flesh[3] = 4.0f;
			const Vec3 axv[3] = { a_frame.Rotate({ 1, 0, 0 }) * (1.0 / R[0]), a_frame.Rotate({ 0, 1, 0 }) * (1.0 / R[1]), a_frame.Rotate({ 0, 0, 1 }) * (1.0 / R[2]) };
			cl.shape[0][0] = static_cast<float>(a_c.x);
			cl.shape[0][1] = static_cast<float>(a_c.y);
			cl.shape[0][2] = static_cast<float>(a_c.z);
			cl.shape[0][3] = 2.0f;
			for (int j = 0; j < 3; ++j) {
				cl.shape[j + 1][0] = static_cast<float>(axv[j].x);
				cl.shape[j + 1][1] = static_cast<float>(axv[j].y);
				cl.shape[j + 1][2] = static_cast<float>(axv[j].z);
			}
			cl.cookMode = lk.cookMode;
			cl.cook[0] = lk.cook[0];
			cl.shadow = false;  // the skin casts the shadow
		}

		void PlaceBoardUnit(Unit& u)
		{
			const double h = Thick(u, u.boardLen, 2);
			const Vec3   at = g_boardPose.ToWorld({ 0.0, L::kLaneY[u.lane], L::kBoardH * 0.5 + h * 0.5 });
			const Quat   frame = RotY(g_boardPose.tilt);
			const int    nx = u.cut.Pieces(), ny = u.cut.Strips(), n = nx * ny;
			// its own mesh cut to each piece (the first is the thing itself), nudged apart so the cuts show
			if (u.segs.empty()) {
				u.segs.push_back(u.prop);
			}
			while (static_cast<int>(u.segs.size()) < n) {
				const auto id = u.model.empty() ? Add("Chunk") : Scene::Add(u.model);
				u.segs.push_back(id >= 0 ? id : Add("Chunk"));
			}
			while (static_cast<int>(u.cores.size()) < n) {
				u.cores.push_back(Add("Hull"));
			}
			const double half = u.boardLen * 0.5, w = u.cut.width;
			for (int ix = 0; ix < nx; ++ix) {
				double a, b;
				u.cut.Piece(ix, a, b);
				for (int iy = 0; iy < ny; ++iy) {
					double ya, yb;
					u.cut.Strip(iy, ya, yb);
					const double box[6] = { nx > 1 ? a - half : -1e7, nx > 1 ? b - half : 1e7, ny > 1 ? ya - w * 0.5 : -1e7, ny > 1 ? yb - w * 0.5 : 1e7, -1e7, 1e7 };
					const Vec3   c = at + frame.Rotate({ Spread(ix, nx), Spread(iy, ny), 0.0 });
					PlacePiece(u.segs[ix * ny + iy], u.cores[ix * ny + iy], u, c, frame, box, n > 1);
				}
			}
			for (int k = n; k < static_cast<int>(u.segs.size()); ++k) {
				Scene::Hide(u.segs[k], true);
			}
			for (int k = n; k < static_cast<int>(u.cores.size()); ++k) {
				Scene::Hide(u.cores[k], true);
			}
		}

		void Look3D()
		{
			const double t = g_clock;
			std::vector<Render3D::Sprite> sp;
			const double heat = g_fire.Heat();
			Refit();
			for (int k = 0; k < 3; ++k) {
				PlaceLog(P.pile[k], { L::kPileX + (k == 2 ? 0.0 : (k ? 1.0 : -1.0)), L::kPileY + (k == 2 ? 0.0 : -0.4), 0.95 + (k == 2 ? 1.65 : 0.0) }, kPi * 0.5 + 0.12 * k, 8.4);
				Scene::Hide(P.pile[k], false);
			}
			const Vec3   fc = FireCenter();
			// ---- the fire: its logs, flames, embers, smoke
			for (int k = 0; k < hc::Fire::kMaxLogs; ++k) {
				const auto id = P.fireLogs[k];
				if (id < 0) {
					continue;
				}
				if (k >= g_fire.Logs()) {
					Scene::Hide(id, true);
					continue;
				}
				const double glow = g_fire.Glow(k), fuel = g_fire.Fuel(k);
				Vec3         at;
				double       rz = 0;
				if (g_flow == hc::Flow::Oven) {
					at = { L::kOvenX - 3.0 + k * 1.5, -0.4 + (k % 2) * 0.8, 1.6 + (k / 3) * 1.2 };
					rz = kPi * 0.5 + (k % 2 ? 0.2 : -0.2);
				} else {
					static constexpr double kA[5] = { 0.3, 2.4, 4.4, 1.3, 3.4 };
					at = { fc.x + std::cos(kA[k]) * 1.2, fc.y + std::sin(kA[k]) * 1.2, 0.95 + (k >= 3 ? 1.3 : 0.0) };
					rz = kA[k] + kPi * 0.5;
				}
				PlaceLog(id, at, rz, 8.4 * (0.8 + 0.2 * fuel), k >= 3 ? 0.15 : 0.0);
				auto& lk = Scene::LookOf(id);
				const float dark = static_cast<float>(0.25 + 0.75 * fuel);
				lk.tint[0] = lk.tint[1] = lk.tint[2] = dark;
				const float e = static_cast<float>(glow * Clamp(heat * 1.2, 0.0, 1.2) * (1.0 - fuel * 0.6));
				lk.emissive[0] = 0.9f * e;
				lk.emissive[1] = 0.28f * e;
				lk.emissive[2] = 0.05f * e;
				Scene::Hide(id, false);
				// flames along the log
				for (int f = 0; f < 3; ++f) {
					Render3D::Sprite s;
					const double off = (f - 1) * 2.4;
					s.pos[0] = static_cast<float>(at.x + std::cos(rz - kPi * 0.5) * off * 0.0 + std::cos(rz) * off);
					s.pos[1] = static_cast<float>(at.y + std::sin(rz) * off);
					s.pos[2] = static_cast<float>(at.z + 0.6);
					s.size = static_cast<float>((0.9 + 1.4 * glow) * Clamp(heat * 1.3, 0.2, 1.4));
					s.stretch = 1.9f;
					s.seed = static_cast<float>(k * 3 + f) * 1.37f;
					s.color[3] = static_cast<float>(glow * Clamp(heat * 1.5, 0.0, 1.0));
					s.kind = Render3D::Sprite::Kind::Flame;
					sp.push_back(s);
				}
			}
			if (heat > 0.02) {
				const int n = static_cast<int>(2 + heat * 6);
				for (int k = 0; k < n; ++k) {
					Render3D::Sprite s;
					const double a = k * 2.399 + 0.3, r = (g_flow == hc::Flow::Oven ? 0.8 : 1.4) * Hash(k, 3);
					s.pos[0] = static_cast<float>(fc.x + std::cos(a) * r * (g_flow == hc::Flow::Oven ? 2.5 : 1.0));
					s.pos[1] = static_cast<float>(fc.y + std::sin(a) * r);
					s.pos[2] = static_cast<float>(fc.z + 0.6);
					s.size = static_cast<float>(1.4 + 2.2 * heat * (0.6 + 0.4 * Hash(k, 4)));
					s.stretch = 2.0f;
					s.seed = 20.0f + k * 2.11f;
					s.color[3] = static_cast<float>(Clamp(heat * 1.3, 0.0, 1.0));
					s.kind = Render3D::Sprite::Kind::Flame;
					sp.push_back(s);
				}
				Render3D::Sprite g;
				g.pos[0] = static_cast<float>(fc.x);
				g.pos[1] = static_cast<float>(fc.y);
				g.pos[2] = static_cast<float>(fc.z + 0.8);
				g.size = static_cast<float>(6.0 + 4.0 * heat);
				g.color[0] = 1.0f;
				g.color[1] = 0.42f;
				g.color[2] = 0.12f;
				g.color[3] = static_cast<float>(0.22 * Clamp(heat, 0.0, 1.2));
				g.kind = Render3D::Sprite::Kind::Glow;
				sp.push_back(g);
				// embers rising, and a little smoke
				for (int k = 0; k < 14; ++k) {
					const double p = std::fmod(t * (0.3 + 0.25 * Hash(k, 5)) + Hash(k, 6), 1.0);
					Render3D::Sprite e;
					e.pos[0] = static_cast<float>(fc.x + (Hash(k, 7) - 0.5) * 4.0 + std::sin(t * 2.0 + k) * p * 1.5);
					e.pos[1] = static_cast<float>(fc.y + (Hash(k, 8) - 0.5) * 3.0);
					e.pos[2] = static_cast<float>(fc.z + 1.0 + p * (6.0 + 14.0 * heat));
					e.size = 0.13f;
					e.color[0] = 1.0f;
					e.color[1] = 0.55f;
					e.color[2] = 0.18f;
					e.color[3] = static_cast<float>((1.0 - p) * Clamp(heat * 1.4, 0.0, 1.0));
					e.kind = Render3D::Sprite::Kind::Ember;
					sp.push_back(e);
				}
			}
			for (int k = 0; k < 6 && g_flow != hc::Flow::Pot; ++k) {
				const double p = std::fmod(t * 0.12 + k / 6.0, 1.0);
				Render3D::Sprite s;
				s.pos[0] = static_cast<float>(fc.x + std::sin(t * 0.4 + k) * 2.0 * p);
				s.pos[1] = static_cast<float>(fc.y + (g_flow == hc::Flow::Oven ? -2.0 * p : 0.0));
				s.pos[2] = static_cast<float>(fc.z + 6.0 + p * 22.0);
				s.size = static_cast<float>(2.0 + p * 6.0);
				s.color[0] = s.color[1] = s.color[2] = 0.32f;
				s.color[3] = static_cast<float>(0.16 * (1.0 - p) * Clamp(heat, 0.2, 1.0));
				s.seed = k * 3.3f;
				s.kind = Render3D::Sprite::Kind::Smoke;
				sp.push_back(s);
			}
			Scene::SetFire(fc + Vec3{ 0, 0, 3.0 }, static_cast<float>(heat));

			// ---- the log carried
			if (P.heldLog >= 0) {
				const bool held = g_hold == Hold::Log || g_hold == Hold::FireLog;
				Scene::Hide(P.heldLog, !held);
				if (held) {
					PlaceLog(P.heldLog, g_carry, 0.4, 8.4);
					auto& lk = Scene::LookOf(P.heldLog);
					const float e = g_hold == Hold::FireLog ? 0.5f : 0.0f;
					lk.emissive[0] = 0.9f * e;
					lk.emissive[1] = 0.3f * e;
					lk.emissive[2] = 0.05f * e;
				}
			}

			// ---- the ingredients
			for (int i = 0; i < static_cast<int>(g_units.size()); ++i) {
				auto& u = g_units[i];
				if (u.prop < 0) {
					continue;
				}
				switch (u.at) {
				case Unit::At::Cloth:
					Lay(u, u.clothAt + Vec3{ 0, 0, Thick(u, (std::min)(5.2, u.dims[0] * 0.6), 2) * 0.5 + 0.2 }, (std::min)(5.2, Clamp(u.dims[0] * 0.6, 2.5, 5.2)), RotZ(0.25 * (Hash(i, 9) - 0.5)));
					break;
				case Unit::At::Held:
					Lay(u, g_carry, Clamp(u.dims[0] * 0.6, 2.5, 5.2), RotZ(0.3));
					break;
				case Unit::At::Board:
					PlaceBoardUnit(u);
					break;
				case Unit::At::Spit:
					{
						const double len = Clamp(u.dims[0] * 0.5, 5.0, 9.0);
						const double a = g_spit ? g_spit->Angle() : 0.0;
						Lay(u, SpitAxis(u.spitX), len, RotX(a));
						auto& lk = Scene::LookOf(u.prop);
						lk.cookMode = 2;
						for (int b = 0; b < hc::Spit::kBands; ++b) {
							lk.cook[b] = static_cast<float>(g_spit->Band(b));
						}
						lk.cookAxis[0] = 1.0f;
						lk.cookAxis[1] = lk.cookAxis[2] = 0.0f;
						lk.cookRef[0] = 0.0f;
						lk.cookRef[1] = static_cast<float>(std::sin(a));
						lk.cookRef[2] = static_cast<float>(-std::cos(a));
						// it sizzles and smokes as it browns
						if (g_fire.Heat() > 0.4 && g_spit->Mean() > 0.15) {
							for (int k = 0; k < 3; ++k) {
								const double p = std::fmod(t * 0.5 + k / 3.0 + i * 0.13, 1.0);
								Render3D::Sprite s;
								const auto   at = SpitAxis(u.spitX);
								s.pos[0] = static_cast<float>(at.x + (Hash(k + i * 5, 11) - 0.5) * len);
								s.pos[1] = static_cast<float>(at.y);
								s.pos[2] = static_cast<float>(at.z + 1.0 + p * 8.0);
								s.size = static_cast<float>(1.0 + p * 2.5);
								s.color[0] = s.color[1] = s.color[2] = 0.75f;
								s.color[3] = static_cast<float>(0.12 * (1.0 - p) * Clamp(g_spit->Mean(), 0.0, 1.2));
								s.seed = k * 5.1f + i;
								s.kind = Render3D::Sprite::Kind::Smoke;
								sp.push_back(s);
							}
						}
					}
					break;
				default:
					Scene::Hide(u.prop, true);
					HideBoardBits(u);
					break;
				}
				if (u.at != Unit::At::Board) {
					auto& lk = Scene::LookOf(u.prop);
					std::memset(lk.clips, 0, sizeof(lk.clips));
					lk.flesh[3] = 0.0f;
					if (u.at != Unit::At::Pot && u.at != Unit::At::Gone) {
						for (auto id : u.segs) {
							if (id != u.prop) {
								Scene::Hide(id, true);
							}
						}
						for (auto id : u.cores) {
							Scene::Hide(id, true);
						}
					}
				}
			}
			if (g_hold == Hold::Meat && g_holdUnit >= 0) {
				auto&        u = g_units[g_holdUnit];
				const double len = Clamp(u.dims[0] * 0.5, 5.0, 9.0);
				Lay(u, g_carry, len, RotX(g_spit ? g_spit->Angle() : 0.0));
			}

			// ---- the board and knife (the pot)
			if (P.board >= 0) {
				const auto pose = g_flow == hc::Flow::Pot ? g_boardPose : hc::BoardPose(0.0, L::kBoardY);
				if (Game(Kit::Role::Board)) {
					Pose p0;
					if (FitBox(P.board, Kit::Get(Kit::Role::Board), { 0.0, 0.0, -L::kBoardH * 0.5 }, { L::kBoardW, L::kBoardD, L::kBoardH * 2.0 }, 0.0, false, p0)) {
						const Quat t = RotY(pose.tilt);
						Scene::Place(P.board, pose.c + t.Rotate(p0.pos), t * p0.q, p0.s);
						Scene::Hide(P.board, false);
					}
				} else {
					Scene::Place(P.board, pose.c, RotY(pose.tilt), 1.0f);
					// ours wears the game's own wood (the table's), lighter, a patch of it to a face
					if (Game(Kit::Role::Table)) {
						if (auto* wood = Scene::Skin(P.kitchen)) {
							auto& lk = Scene::LookOf(P.board);
							lk.skin = wood;
							lk.tint[0] = 1.45f, lk.tint[1] = 1.3f, lk.tint[2] = 1.12f;
							lk.uv[0] = lk.uv[1] = 0.45f;
						}
					}
				}
			}
			if (P.knife >= 0) {
				if (g_knifeOn || g_knifeAnim >= 0) {
					const double a = g_knifeAnim < 0 ? 0.0 : g_knifeAnim < 0.06 ? g_knifeAnim / 0.06 : 1.0 - (g_knifeAnim - 0.06) / 0.14;
					double       top = 0.0;  // how tall what's on its lane stands
					for (const auto& u : g_units) {
						if (u.at == Unit::At::Board && u.lane == g_knifeLane) {
							top = (std::max)(top, Thick(u, u.boardLen, 2));
						}
					}
					const double z = kBoardTop + (top + 1.6) * (1.0 - Clamp(a, 0.0, 1.0));
					if (Game(Kit::Role::Knife)) {
						// the game's knife on its edge across the lane: long along y, its blade's depth up, its thin side along x
						Lie lie;
						if (LieOf(P.knife, lie)) {
							static const double kCyc[3][3] = { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } };
							// turned over about its length so the edge is down, toward the food
							const Quat   q = (g_knifeAlong ? RotZ(kPi * 0.5) : Quat{}) * RotY(kPi) * FromRows(kCyc) * lie.q;
							const double ks = 8.5 / lie.d[0];
							const Vec3   c{ g_knifeX, g_knifeY, z + lie.d[1] * ks * 0.5 };
							Scene::Place(P.knife, c - q.Rotate(lie.c) * ks, q, static_cast<float>(ks));
							Scene::Hide(P.knife, false);
						}
					} else {
						if (g_knifeAlong) {
							Scene::Place(P.knife, { g_knifeX - 3.8, g_knifeY, z }, RotZ(-kPi * 0.5), 1.0f);
						} else {
							Scene::Place(P.knife, { g_knifeX, g_knifeY - 3.8, z }, {}, 1.0f);
						}
					}
				} else if (Game(Kit::Role::Knife)) {
					Lie lie;
					if (LieOf(P.knife, lie)) {
						const Quat   q = RotZ(0.2) * lie.q;
						const double ks = 8.5 / lie.d[0];
						const Vec3   c{ L::kBoardX + 3.0, L::kBoardY - 9.6, lie.d[2] * ks * 0.5 };
						Scene::Place(P.knife, c - q.Rotate(lie.c) * ks, q, static_cast<float>(ks));
						Scene::Hide(P.knife, false);
					}
				} else {
					Scene::Place(P.knife, { L::kBoardX + 5.0, L::kBoardY - 9.2, 0.12 }, RotZ(-kPi * 0.5) * RotY(kPi * 0.5), 1.0f);
				}
			}
			// ---- the pieces: the ingredient's own mesh, cut to each
			for (int b = 0; b < g_chunks.Count() && b < static_cast<int>(g_pieces.size()); ++b) {
				const auto& body = g_chunks.At(b);
				auto&       pc = g_pieces[b];
				if (pc.prop < 0 || g_units.empty()) {
					continue;
				}
				if (body.state == hc::Chunks::State::None) {
					Scene::Hide(pc.prop, true);
					Scene::Hide(pc.core, true);
					continue;
				}
				auto&      u = g_units[(std::min)(pc.unit, static_cast<int>(g_units.size()) - 1)];
				Quat       frame = body.state == hc::Chunks::State::Board ? RotY(g_boardPose.tilt) * body.q : body.q;
				Vec3       at = body.p;
				if (pc.onDough) {
					// on the dough until it's folded up (then the dish's own shape stands for it)
					if (!g_oven || DoughReady() || !g_doughOnBoard || g_oven->InOven() || g_oven->Out() || g_peelT >= 0 || g_peelLoaded) {
						Scene::Hide(pc.prop, true);
						Scene::Hide(pc.core, true);
						continue;
					}
					frame = RotZ(pc.turn) * body.q;
					at = g_doughAt + pc.doughOff + Vec3{ 0, 0, 0.9 };
				}
				const Vec3 c = at - frame.Rotate(pc.mid);  // where its whole would have its middle
				auto& lk = Scene::LookOf(pc.prop);
				lk.cookMode = 1;
				lk.cook[0] = static_cast<float>(body.charred ? 1.7 : body.cooked * 0.6);
				lk.shadow = body.state != hc::Chunks::State::Pot;
				PlacePiece(pc.prop, pc.core, u, c, frame, pc.box, pc.cut);
			}
			// ---- the pot, the broth, the ladle, bubbles and steam
			if (P.broth >= 0 && g_pot) {
				Scene::Place(P.broth, { L::kPotX, L::kPotY, L::kLiquidZ }, RotZ(std::fmod(t * 0.05, 6.283) + g_ladleA * 0.15), 1.0f);
				auto&       lk = Scene::LookOf(P.broth);
				const bool  any = g_chunks.CountIn(hc::Chunks::State::Pot) + g_tossedIn > 0;
				const float c = static_cast<float>(Clamp(g_pot->Cooked(), 0.0, 1.5));
				lk.lift[0] = any ? g_broth[0] * (1.0f - 0.15f * c) : 0.55f;
				lk.lift[1] = any ? g_broth[1] * (1.0f - 0.15f * c) : 0.55f;
				lk.lift[2] = any ? g_broth[2] * (1.0f - 0.15f * c) : 0.52f;
				lk.lift[3] = any ? 0.82f : 0.6f;  // plain water until something's in it
				const double tp = g_pot->Temp();
				if (tp > 0.42) {
					const int n = static_cast<int>((tp - 0.42) * 40.0);
					for (int k = 0; k < n; ++k) {
						const double rate = 0.9 + Hash(k, 12);
						const double ph = t * rate + Hash(k, 13);
						const int    cyc = static_cast<int>(std::floor(ph));
						const double p = ph - cyc;
						const double a = Hash(k * 17 + cyc, 14) * 6.283, r = std::sqrt(Hash(k * 17 + cyc, 15)) * (L::kPotR - 0.8);
						Render3D::Sprite s;
						s.pos[0] = static_cast<float>(L::kPotX + std::cos(a) * r);
						s.pos[1] = static_cast<float>(L::kPotY + std::sin(a) * r);
						s.pos[2] = static_cast<float>(L::kLiquidZ + 0.05);
						s.size = static_cast<float>(0.12 + 0.35 * p);
						s.color[0] = s.color[1] = s.color[2] = 0.95f;
						s.color[3] = static_cast<float>(0.8 * (1.0 - p * p));
						s.kind = Render3D::Sprite::Kind::Bubble;
						sp.push_back(s);
					}
				}
				if (tp > 0.35) {
					for (int k = 0; k < 7; ++k) {
						const double p = std::fmod(t * 0.22 + k / 7.0, 1.0);
						Render3D::Sprite s;
						s.pos[0] = static_cast<float>(L::kPotX + (Hash(k, 16) - 0.5) * 6.0 + std::sin(t * 0.6 + k) * p * 2.0);
						s.pos[1] = static_cast<float>(L::kPotY + (Hash(k, 17) - 0.5) * 6.0);
						s.pos[2] = static_cast<float>(L::kLiquidZ + 1.0 + p * 16.0);
						s.size = static_cast<float>(1.6 + p * 5.0);
						s.color[0] = s.color[1] = s.color[2] = 0.92f;
						s.color[3] = static_cast<float>(0.14 * (1.0 - p) * Clamp((tp - 0.35) * 3.0, 0.0, 1.0));
						s.seed = 40.0f + k * 1.9f;
						s.kind = Render3D::Sprite::Kind::Smoke;
						sp.push_back(s);
					}
				}
				// smoke when it catches
				if (g_pot->Burn() > 0.35) {
					for (int k = 0; k < 4; ++k) {
						const double p = std::fmod(t * 0.3 + k / 4.0, 1.0);
						Render3D::Sprite s;
						s.pos[0] = static_cast<float>(L::kPotX + (Hash(k, 18) - 0.5) * 4.0);
						s.pos[1] = static_cast<float>(L::kPotY + (Hash(k, 19) - 0.5) * 4.0);
						s.pos[2] = static_cast<float>(L::kLiquidZ + 2.0 + p * 14.0);
						s.size = static_cast<float>(2.0 + p * 5.0);
						s.color[0] = s.color[1] = s.color[2] = 0.18f;
						s.color[3] = static_cast<float>(0.3 * (1.0 - p) * Clamp(g_pot->Burn(), 0.0, 1.0));
						s.seed = 60.0f + k;
						s.kind = Render3D::Sprite::Kind::Smoke;
						sp.push_back(s);
					}
				}
			}
			if (P.ladle >= 0) {
				Vec3 mn, mx;
				if (Game(Kit::Role::Ladle) && Scene::Bounds(P.ladle, mn, mx) && mx.x - mn.x > mx.y - mn.y) {
					// the game's ladle as it was made: upright, its cup at the +x end opening up, the handle rising
					// along -x. Turned so the handle points out of the pot (and away from it once lifted out); the cup
					// sits in the broth, or level under the pointer with a ladleful in it.
					const double len = mx.x - mn.x, ls = 12.0 / len;
					const double cupR = (mx.y - mn.y) * 0.5;
					const Vec3   cup{ mx.x - cupR, (mn.y + mx.y) * 0.5, mn.z + cupR * 0.9 };  // the middle of its cup, at the rim
					const bool   out = g_hold == Hold::LadleOut;
					// leaned back so the handle climbs out over the rim (the cup tips a little with it); held out, nearly level
					const Quat   q = RotZ(g_ladleA - kPi) * RotY(out ? 0.12 : 0.85);
					const Vec3   at = out ? g_ladleOutAt : Vec3{ L::kPotX + std::cos(g_ladleA) * g_ladleR, L::kPotY + std::sin(g_ladleA) * g_ladleR, L::kLiquidZ - 0.3 };
					Scene::Place(P.ladle, at - q.Rotate(cup) * ls, q, static_cast<float>(ls));
					Scene::Hide(P.ladle, false);
					const bool full = out && g_chunks.CountIn(hc::Chunks::State::Pot) + g_tossedIn > 0;
					if (P.ladleSoup >= 0) {
						const double r = cupR * ls * 0.82;
						Scene::Place(P.ladleSoup, at + Vec3{ 0, 0, -cupR * ls * 0.3 }, RotZ(g_clock * 0.3), static_cast<float>(r / 6.1));
						Scene::Hide(P.ladleSoup, !full);
						if (P.broth >= 0) {
							auto&       sl = Scene::LookOf(P.ladleSoup);
							const auto& bl = Scene::LookOf(P.broth);
							std::copy(std::begin(bl.lift), std::end(bl.lift), sl.lift);
							std::copy(std::begin(bl.tint), std::end(bl.tint), sl.tint);
							sl.shadow = false;
						}
					}
				} else if (g_hold == Hold::LadleOut) {
					const double handle = std::atan2(-0.45, -0.25);
					Scene::Place(P.ladle, g_ladleOutAt, RotZ(g_ladleA - handle), 1.0f);
				} else {
					const Vec3   at{ L::kPotX + std::cos(g_ladleA) * g_ladleR, L::kPotY + std::sin(g_ladleA) * g_ladleR, L::kLiquidZ + 0.4 };
					const double handle = std::atan2(-0.45, -0.25);  // which way the mesh's handle points
					Scene::Place(P.ladle, at, RotZ(g_ladleA - handle), 1.0f);
				}
			}
			// ---- the spit's rod and crank
			if (P.rod >= 0) {
				Scene::Place(P.rod, { L::kFireX, L::kFireY, L::kSpitZ }, RotX(g_spit ? g_spit->Angle() : 0.0), 1.0f);
			}
			// ---- the oven: door, peel, dough, the bake
			if (P.door >= 0) {
				Scene::Place(P.door, { L::kOvenX - 4.9, L::kOvenMouthY - 0.35, L::kOvenFloorZ }, RotZ(-g_door * 1.75), 1.0f);
			}
			if (P.peel >= 0) {
				Vec3   at = PeelRest();
				double rz = 0.35;
				if (g_hold == Hold::Peel) {
					at = g_peelAt;
					rz = 0.0;
				}
				if (g_peelT >= 0) {
					const double k = std::sin(Clamp(g_peelT, 0.0, 1.0) * kPi);
					at = g_peelFrom + (BreadAt() + Vec3{ 0, 0, 0.3 } - g_peelFrom) * Clamp(g_peelT * 1.4, 0.0, 1.0);
					at.z += k * 1.5;
					rz = 0.0;
				}
				Lie lie;
				if (Game(Kit::Role::Peel) && LieOf(P.peel, lie)) {
					const double ps = 21.0 / lie.d[0];
					const Quat   q = RotZ(rz + kPi * 0.5) * lie.q;
					const Vec3   c = at + Vec3{ 0.0, -7.0, lie.d[2] * ps * 0.5 };
					Scene::Place(P.peel, c - q.Rotate(lie.c) * ps, q, static_cast<float>(ps));
					Scene::Hide(P.peel, false);
				} else {
					Scene::Place(P.peel, at, RotZ(rz), 1.0f);
				}
			}
			if (g_oven) {
				const bool baking = g_oven->InOven() || (g_peelT >= 0 && g_peelIn);
				Vec3       at = g_doughAt;
				if (g_hold == Hold::Dough && g_strokeLen > 0 && !board_kneading()) {
					at = { g_carry.x, g_carry.y, kBoardTop + 4.0 };
				}
				if (g_hold == Hold::Peel && g_peelLoaded) {
					at = g_peelAt + Vec3{ 0, 0, 0.3 };  // on the peel
				}
				if (g_peelT >= 0) {
					at = g_peelFrom + (BreadAt() - g_peelFrom) * Clamp(g_peelT * 1.4, 0.0, 1.0);
					at.z += std::sin(Clamp(g_peelT, 0.0, 1.0) * kPi) * 1.5 + 0.3;
				}
				if (g_oven->InOven()) {
					at = BreadAt();
				}
				if (g_hold == Hold::Bread) {
					at = g_carry;
				}
				if (g_peelBread) {
					at = (g_hold == Hold::Peel ? g_peelAt : PeelRest()) + Vec3{ 0, 0, 0.3 };
				}
				// ready for the oven: it takes the shape of what it'll be (raw); in the oven and after, the dish itself
				const bool ready = DoughReady();
				const bool showDish = baking || g_hold == Hold::Bread || g_peelBread || ready;
				if (showDish && P.result < 0 && g_recipe) {
					P.result = AddResult();
				}
				Vec3 mn, mx;
				if (showDish && P.result >= 0 && Scene::Bounds(P.result, mn, mx)) {
					const double big = (std::max)(mx.x - mn.x, mx.y - mn.y);
					const double s = 8.0 / (std::max)(0.1, big) * (0.8 + 0.2 * Clamp(g_oven->Rise(), 0.0, 1.0));
					const Vec3   c{ (mn.x + mx.x) * 0.5, (mn.y + mx.y) * 0.5, mn.z };
					Scene::Place(P.result, at - c * s, {}, static_cast<float>(s));
					auto& lk = Scene::LookOf(P.result);
					lk.cookMode = 1;
					const double br = g_oven->Brown();
					lk.cook[0] = static_cast<float>(br > 0.6 ? (br - 0.6) * 1.2 : 0.0);  // its own colour is "golden"; burnt past it
					lk.lift[0] = 0.94f;
					lk.lift[1] = 0.86f;
					lk.lift[2] = 0.7f;
					lk.lift[3] = static_cast<float>(Clamp(0.85 - br * 0.9, 0.0, 0.85));  // pale and doughy at first
					Scene::Hide(P.result, false);
					Scene::Hide(P.dough, true);
					Scene::Hide(P.disk, true);
				} else {
					if (P.result >= 0 && !showDish) {
						Scene::Hide(P.result, true);
					}
					const bool dough = g_doughParts > 0 && g_doughOnBoard && !g_oven->Out();
					Scene::Hide(P.dough, !dough || g_rolled);
					Scene::Hide(P.disk, !dough || !g_rolled);
					if (dough && !g_rolled) {
						const double grow = Clamp(static_cast<double>(g_doughParts) / (std::max)(1, g_doughNeeded), 0.3, 1.0);
						const double r = 3.0 * std::cbrt(grow);
						const double sq = g_squash * 0.45;
						const Quat   q = RotZ(g_squashA);
						Scene::Place(P.dough, at + Vec3{ 0, 0, r * 0.62 * (1.0 - sq) + 0.1 }, q, static_cast<float>(r));
						Scene::Stretch(P.dough, static_cast<float>(1.0 + sq), static_cast<float>(1.0 - sq * 0.3), static_cast<float>(0.62 * (1.0 - sq)));
						auto& lk = Scene::LookOf(P.dough);
						const float smooth = static_cast<float>(Clamp(g_oven->Dough(), 0.0, 1.0));
						lk.tint[0] = lk.tint[1] = lk.tint[2] = 0.88f + 0.12f * smooth;
					} else if (dough) {
						Scene::Place(P.disk, at + Vec3{ 0, 0, 0.3 }, {}, 5.0f);
						Scene::Stretch(P.disk, 1.0f, 1.0f, 2.0f);
					}
				}
				// the mixing bowl: water clouding with flour as it's stirred, then a ball of dough
				if (P.mixWater >= 0) {
					const double m = Clamp(static_cast<double>(g_doughParts) / (std::max)(1, g_doughNeeded), 0.0, 1.0);
					const bool   water = !g_ball && g_doughNeeded > 0;
					Scene::Place(P.mixWater, kMixAt + Vec3{ 0, 0, 1.5 + 0.4 * g_mix }, RotZ(g_mixA * 0.3), 0.52f);
					Scene::Hide(P.mixWater, !water);
					auto& wl = Scene::LookOf(P.mixWater);
					// clear water (a cool blue over the bowl's wood), clouding with the flour as it goes in and is stirred
					const double cloud = Clamp(m * 0.45 + g_mix * 0.6, 0.0, 1.0);
					wl.lift[0] = static_cast<float>(0.32 + 0.6 * cloud);
					wl.lift[1] = static_cast<float>(0.46 + 0.4 * cloud);
					wl.lift[2] = static_cast<float>(0.6 + 0.12 * cloud);
					wl.lift[3] = static_cast<float>(0.45 + 0.5 * cloud);
					wl.shadow = false;
					if (P.mixBall >= 0) {
						const bool inBowl = (g_mix > 0.25 || g_ball) && !g_doughOnBoard && g_hold != Hold::Ball;
						const bool held = g_hold == Hold::Ball;
						const double r = 1.0 + 1.6 * Clamp(g_mix, 0.0, 1.0);
						Scene::Hide(P.mixBall, !(inBowl || held));
						if (inBowl || held) {
							const Vec3 c = held ? g_carry : kMixAt + Vec3{ 0, 0, 1.6 + r * 0.45 };
							Scene::Place(P.mixBall, c, RotZ(g_mixA), static_cast<float>(r));
							Scene::Stretch(P.mixBall, 1.0f, 1.0f, 0.75f);
							auto& bl = Scene::LookOf(P.mixBall);
							bl.tint[0] = bl.tint[1] = bl.tint[2] = static_cast<float>(0.82 + 0.1 * g_mix);
						}
					}
				}
				// the filling on the pie, and its crimped edge
				for (auto& [id, off] : P.fill) {
					if (id < 0) {
						continue;
					}
					const bool show = g_rolled && !showDish && g_peelT < 0 && g_hold != Hold::Dough;
					Scene::Hide(id, !show);
					if (show) {
						const int ui = static_cast<int>(off.z);
						Scene::Place(id, at + Vec3{ off.x, off.y, 1.35 }, RotZ(Hash(id, 21) * 6.28), 1.0f);
						Scene::Stretch(id, 0.9f, 0.8f, 0.55f);
						auto& lk = Scene::LookOf(id);
						for (const auto& u : g_units) {
							if (&u - g_units.data() == ui) {
								lk.skin = Scene::Skin(u.prop);
							}
						}
						lk.uv[0] = lk.uv[1] = 0.3f;
						lk.uv[2] = Hash(id, 22) * 0.7f;
						lk.uv[3] = Hash(id, 23) * 0.7f;
					}
				}
				if (g_rolled) {
					while (static_cast<int>(P.crimps.size()) < hc::Oven::kCrimps) {
						const auto id = Add("Dough");
						P.crimps.push_back(id);
					}
					for (int k = 0; k < hc::Oven::kCrimps; ++k) {
						const auto id = P.crimps[k];
						const bool show = g_oven->CrimpedAt(k) && !showDish && g_peelT < 0 && g_hold != Hold::Dough;
						Scene::Hide(id, !show);
						if (show) {
							const double a = (k + 0.5) / hc::Oven::kCrimps * 2 * kPi - kPi;
							// a fold of the edge, lifted up and over toward the middle (the dumpling closing)
							Scene::Place(id, at + Vec3{ std::cos(a) * 3.7, std::sin(a) * 3.7, 1.3 }, RotZ(a) * RotY(-0.75), 0.9f);
							Scene::Stretch(id, 1.25f, 0.85f, 0.42f);
						}
					}
				}
			}
			// ---- the dish served
			if (g_phase == Phase::Result && P.result >= 0) {
				Vec3 mn, mx;
				if (Scene::Bounds(P.result, mn, mx)) {
					const double big = (std::max)(mx.x - mn.x, mx.y - mn.y);
					const double s = (g_flow == hc::Flow::Pot ? 7.5 : 8.0) / (std::max)(0.1, big);
					const Vec3   c{ (mn.x + mx.x) * 0.5, (mn.y + mx.y) * 0.5, mn.z };
					const Quat   q = RotZ(std::fmod((g_clock - g_serveAt) * 0.4, 6.283));
					const Vec3   on{ L::kServeX, L::kServeY, g_flow == hc::Flow::Pot ? 0.0 : 0.45 };
					Scene::Place(P.result, on - q.Rotate(c) * s, q, static_cast<float>(s));
					auto& lk = Scene::LookOf(P.result);
					lk.lift[3] = 0.0f;
					lk.cookMode = g_burntLook ? 1 : 0;
					lk.cook[0] = 1.65f;
					Scene::Hide(P.result, false);
				}
			}
			// ---- the salt in the hand, the grains in the air
			if (g_hold == Hold::Salt) {
				for (int k = 0; k < static_cast<int>(4 + g_pinch * 10); ++k) {
					Render3D::Sprite s;
					s.pos[0] = static_cast<float>(g_carry.x + (Hash(k, 24) - 0.5) * 0.8);
					s.pos[1] = static_cast<float>(g_carry.y + (Hash(k, 25) - 0.5) * 0.8);
					s.pos[2] = static_cast<float>(g_carry.z + Hash(k, 26) * 0.5);
					s.size = 0.1f;
					s.color[0] = s.color[1] = s.color[2] = 0.96f;
					s.color[3] = 1.0f;
					s.kind = Render3D::Sprite::Kind::Grain;
					sp.push_back(s);
				}
			}
			for (const auto& g : g_grains) {
				Render3D::Sprite s;
				s.pos[0] = static_cast<float>(g.p.x);
				s.pos[1] = static_cast<float>(g.p.y);
				s.pos[2] = static_cast<float>(g.p.z);
				const double k = g.age / (std::max)(0.01, g.life);
				s.size = g.kind == Render3D::Sprite::Kind::Smoke ? g.size * static_cast<float>(1.0 + k * 1.5) : g.size;
				std::memcpy(s.color, g.col, sizeof(s.color));
				s.color[3] *= static_cast<float>(1.0 - k);
				s.kind = g.kind;
				s.seed = static_cast<float>(&g - g_grains.data()) * 0.7f;
				sp.push_back(s);
			}
			Scene::SetSprites(std::move(sp));
		}

		bool board_kneading()
		{
			// a stroke that stays on the board is kneading; one that leaves it is carrying the dough away
			return std::abs(g_carry.x - L::kBoardX) < L::kBoardW * 0.5 + 1.0 && std::abs(g_carry.y - L::kBoardY) < L::kBoardD * 0.5 + 1.0;
		}

		// ---------------------------------------------------------------- the menu's buttons, and the keys
		void Leave()
		{
			spdlog::info("Left the kitchen");
			CloseKitchen(true);
		}

		void ProcessActs()
		{
			std::vector<std::pair<Act, int>> acts;
			{
				std::lock_guard lk(g_actMtx);
				acts.swap(g_acts);
			}
			for (const auto& [act, arg] : acts) {
				switch (act) {
				case Act::Select:
					g_sel = arg;
					break;
				case Act::CookByHand:
					if (g_phase == Phase::Book && g_sel >= 0 && g_sel < static_cast<int>(g_book.size())) {
						const auto* r = g_book[g_sel];
						if (Recipes::CanMake(*r) > 0) {
							StartCooking(r);
						} else {
							Toast("You don't have what it takes");
						}
					}
					break;
				case Act::QuickCook:
					// the game's own menu, as it was: its dishes are Plain
					g_quick = true;
					spdlog::info("Quick Cook: the game's own menu");
					Audio::StopAll();
					Scene::Close();
					g_phase = Phase::Idle;
					ShowCrafting(true);
					break;
				case Act::Leave:
					Leave();
					return;
				case Act::Abandon:
					if (g_phase == Phase::Cooking) {
						g_confirm = true;
					}
					break;
				case Act::AbandonYes:
					g_confirm = false;
					spdlog::info("Put the dish aside (nothing used up)");
					Audio::StopAll();
					ShowStation();
					g_phase = Phase::Book;
					g_book = Recipes::ForStation(g_ovenStation);
					break;
				case Act::AbandonNo:
					g_confirm = false;
					break;
				case Act::Again:
					Audio::StopAll();
					ShowStation();
					g_phase = Phase::Book;
					g_book = Recipes::ForStation(g_ovenStation);
					g_sel = std::clamp(g_sel, 0, (std::max)(0, static_cast<int>(g_book.size()) - 1));
					break;
				case Act::Done:
					Leave();
					return;
				case Act::Help:
					g_help = !g_help;
					break;
				case Act::Mute:
					Audio::SetMuted(!Audio::Muted());
					break;
				}
			}
		}

		void HandleKeys()
		{
			for (int key : Overlay::TakeKeys()) {
				if (key == 0x01) {  // Esc
					if (g_help) {
						g_help = false;
					} else if (g_confirm) {
						g_confirm = false;
					} else if (g_hold != Hold::None) {
						Cancel();
					} else if (g_phase == Phase::Book || g_phase == Phase::Result) {
						Post(Act::Leave);
					} else if (g_phase == Phase::Cooking) {
						g_confirm = true;
					}
				} else if (key == 0x0F) {  // Tab
					if (g_phase == Phase::Cooking) {
						g_confirm = !g_confirm;
					} else if (g_phase == Phase::Book) {
						Post(Act::Leave);
					}
				} else if (key == 0x13 && g_phase == Phase::Cooking && (g_flow == hc::Flow::Pot || g_flow == hc::Flow::Oven)) {  // R
					g_knifeAlong = !g_knifeAlong;
				} else if (key == 0x23) {  // H
					g_help = !g_help;
				} else if (key == 0x32 || key == Overlay::kPadSoundKey) {  // M
					Audio::SetMuted(!Audio::Muted());
				} else if (g_phase == Phase::Book && (key == 0xC8 || key == 0x11)) {  // up / W
					g_sel = (std::max)(0, g_sel - 1);
				} else if (g_phase == Phase::Book && (key == 0xD0 || key == 0x1F)) {  // down / S
					g_sel = (std::min)(static_cast<int>(g_book.size()) - 1, g_sel + 1);
				} else if (g_phase == Phase::Book && (key == 0x1C || key == 0x12)) {  // Enter / E
					Post(Act::CookByHand);
				}
			}
		}

		// ---------------------------------------------------------------- the interface
		struct CardLine
		{
			std::string text;
			bool        done = false;
		};

		std::vector<CardLine> Steps()
		{
			std::vector<CardLine> s;
			if (!g_recipe) {
				return s;
			}
			auto unitsDone = [&](Role a_role, std::initializer_list<Unit::At> a_where, const char* a_pre, const char* a_post) {
				std::map<std::string, std::pair<int, int>> by;
				for (const auto& u : g_units) {
					if (u.role != a_role) {
						continue;
					}
					auto& e = by[u.name];
					++e.first;
					for (auto w : a_where) {
						if (u.at == w) {
							++e.second;
							break;
						}
					}
				}
				for (const auto& [name, c] : by) {
					s.push_back({ fmt::format("{}{}{}{}", a_pre, name, a_post, c.first > 1 ? fmt::format(" ({})", c.first) : ""), c.second >= c.first });
				}
			};
			if (g_flow == hc::Flow::Pot) {
				bool anyChop = false;
				for (const auto& u : g_units) {
					anyChop |= u.role == Role::Chop;
				}
				if (anyChop) {
					std::map<std::string, std::pair<int, int>> by;
					for (const auto& u : g_units) {
						if (u.role == Role::Chop) {
							auto& e = by[u.name];
							++e.first;
							e.second += u.cut.cutCount > 0 || u.at == Unit::At::Pot;
						}
					}
					for (const auto& [name, c] : by) {
						s.push_back({ fmt::format("Dice the {}{}", name, c.first > 1 ? fmt::format(" ({})", c.first) : ""), c.second >= c.first });
					}
					unitsDone(Role::Chop, { Unit::At::Pot }, "Tip in the ", "");
				}
				unitsDone(Role::Toss, { Unit::At::Pot }, "Add the ", "");
				if (g_recipe->salted) {
					s.push_back({ "A pinch of salt", g_pot && g_pot->Seasoning().amount >= 0.7 });
				}
				s.push_back({ "Keep it at a simmer, and stir", g_pot && g_pot->Cooked() >= 0.92 });
				s.push_back({ "Ladle it into the bowl", g_served });
			} else if (g_flow == hc::Flow::Spit) {
				unitsDone(Role::Meat, { Unit::At::Spit }, "Put the ", " on the spit");
				unitsDone(Role::Toss, { Unit::At::Gone }, "Rub in the ", "");
				if (g_recipe->salted) {
					s.push_back({ "Salt it", g_spit && g_spit->Seasoning().amount >= 0.7 });
				}
				s.push_back({ "Turn it over the fire", g_spit && g_spit->Mean() >= 0.9 });
				s.push_back({ "Take it off onto the plate", g_served });
			} else {
				// a pie's fruit is cut first and waits in a bowl
				bool anyDice = false, allInBowl = true, allOnDough = true;
				std::map<std::string, std::pair<int, int>> diced;
				for (const auto& u : g_units) {
					if (!u.dice) {
						continue;
					}
					anyDice = true;
					auto& e = diced[u.name];
					++e.first;
					e.second += u.cut.cutCount + u.cut.alongCount > 0 || u.at == Unit::At::Pot || u.at == Unit::At::Dough;
					allInBowl &= u.at == Unit::At::Pot || u.at == Unit::At::Dough;
					allOnDough &= u.at == Unit::At::Dough;
				}
				for (const auto& [name, c] : diced) {
					s.push_back({ fmt::format("Dice the {}{}", name, c.first > 1 ? fmt::format(" ({})", c.first) : ""), c.second >= c.first });
				}
				if (anyDice) {
					s.push_back({ "Sweep it into the bowl", allInBowl });
				}
				if (g_doughNeeded > 0) {
					unitsDone(Role::Dough, { Unit::At::Dough }, "Into the water: the ", "");
				}
				if (g_recipe->salted) {
					s.push_back({ "A pinch of salt", g_oven && g_oven->Seasoning().amount >= 0.7 });
				}
				if (g_doughNeeded > 0) {
					s.push_back({ "Stir it into a dough", g_ball });
					s.push_back({ "Onto the board", g_doughOnBoard });
					s.push_back({ "Knead the dough", g_oven && g_oven->Dough() >= 0.9 });
				}
				if (g_recipe->pie) {
					if (anyDice) {
						s.push_back({ "Heap the filling on the dough", allOnDough });
					}
					bool other = false;
					for (const auto& u : g_units) {
						other |= u.role == Role::Filling && !u.dice;
					}
					if (other) {
						for (const auto& u : g_units) {
							if (u.role == Role::Filling && !u.dice) {
								s.push_back({ fmt::format("Add the {}", u.name), u.at == Unit::At::Dough });
							}
						}
						if (!anyDice) {
							s.push_back({ "Spread the filling", g_oven && g_oven->Coverage() >= 0.8 });
						}
					}
					s.push_back({ "Fold the edge up over it", g_oven && g_oven->Crimped() >= 0.9 });
				} else {
					unitsDone(Role::Filling, { Unit::At::Dough }, "Work in the ", "");
				}
				s.push_back({ "Into the oven", g_oven && (g_oven->InOven() || g_oven->Out()) });
				s.push_back({ "Bake it golden", g_oven && g_oven->Brown() >= 0.85 });
				s.push_back({ "Out onto the plate", g_served });
			}
			return s;
		}

		std::string Hint()
		{
			if (!Config::Get().hints) {
				return {};
			}
			switch (g_hold) {
			case Hold::Ingredient:
				return g_flow == hc::Flow::Pot ? "Onto the board to chop it, or into the pot as it is" : g_flow == hc::Flow::Spit ? "Onto the spit, or rub it on what's there" : "Onto the board";
			case Hold::Log:
				return "Into the fire for more heat";
			case Hold::FireLog:
				return "Drop it anywhere to take the heat down (back on the fire to keep it)";
			case Hold::Board:
				return g_flow == hc::Flow::Oven ? "Over the bowl behind the board, and let go" : "Drag right to tip it into the pot; up and down to line it up";
			case Hold::Ladle:
				return "Round and round. Lift it out to the bowl when it's done";
			case Hold::LadleOut:
				return "Over the bowl to serve, or back into the pot";
			case Hold::Salt:
				return "Hold for a bigger pinch. Let go over the dish";
			case Hold::Crank:
				return "Turn it round and round";
			case Hold::Meat:
				return "Onto the plate to serve, or back on the spit";
			case Hold::Dough:
				return "A long push to knead";
			case Hold::Mix:
				return "Round and round until it comes together";
			case Hold::Ball:
				return "Onto the board";
			case Hold::Apples:
				return "Over the kneaded dough";
			case Hold::Peel:
				return g_peelBread ? "Onto the plate" : g_peelLoaded ? "Into the open oven" : g_oven && g_oven->InOven() ? "Into the open oven to take it out" : "Slide it under the dough";
			case Hold::Spread:
				return "Spread it out to the edge";
			case Hold::Crimp:
				return "Pinch your way all round the edge";
			case Hold::Door:
				return "Left to open, right to shut";
			case Hold::Bread:
				return "Onto the plate";
			default:
				break;
			}
			if (g_flow == hc::Flow::Pot && g_pot && !g_served && g_hold == Hold::None && g_pot->Cooked() >= 0.92) {
				return "It's ready: drag the ladle out of the pot and let go over the bowl";
			}
			if (g_flow == hc::Flow::Oven && g_hold == Hold::None && AnyOnBoard()) {
				bool cut = true;
				for (const auto& u : g_units) {
					cut &= u.at != Unit::At::Board || u.cut.cutCount + u.cut.alongCount > 0;
				}
				if (cut && !g_knifeOn) {
					return "Grab the board by its left end and drag it over the bowl behind it";
				}
				if (cut) {
					return g_knifeAlong ? "Cut more, or grab the board's left end and drag it over the bowl behind"
					                    : "Cut more, or grab the board's left end and drag it over the bowl behind";
				}
			}
			if (g_knifeOn) {
				return g_knifeAlong ? "Click to cut along it. Right-click (or R) turns the knife back across"
				                    : "Click to chop across. Right-click (or R) turns the knife to cut along: both ways makes dice";
			}
			for (const auto& st : Steps()) {
				if (!st.done) {
					return st.text;
				}
			}
			return {};
		}

		void DrawBook(float W, float H)
		{
			auto*       fg = ImGui::GetBackgroundDrawList();
			Vignette(fg, W, H);
			const float pw = S(470), ph = (std::min)(H - S(160), S(760));
			const ImVec2 lp(W * 0.5f - pw - S(18), (H - ph) * 0.5f - S(20)), rp(W * 0.5f + S(18), lp.y);
			// the left page: what can be made here
			BeginArea("##bookL", lp, ImVec2(pw, ph));
			auto* dl = ImGui::GetWindowDrawList();
			Sheet(dl, lp, ImVec2(pw, ph), 0);
			float y = lp.y + S(28);
			TextC(dl, F.bookSC, lp.x + pw * 0.5f, y, kInk, g_ovenStation ? "The Oven" : "The Cooking Pot");
			y += S(42);
			FadeRule(dl, lp.x + S(40), lp.x + pw - S(40), y, 0.5f, kInkSoft);
			y += S(12);
			const float rowH = S(30), listTop = y, listBot = lp.y + ph - S(36);
			const int   visible = (std::max)(1, static_cast<int>((listBot - listTop) / rowH));
			static int  s_first = 0;
			if (ImGui::IsWindowHovered()) {
				s_first -= static_cast<int>(ImGui::GetIO().MouseWheel * 2);
			}
			if (g_sel < s_first) {
				s_first = g_sel;
			}
			if (g_sel >= s_first + visible) {
				s_first = g_sel - visible + 1;
			}
			s_first = std::clamp(s_first, 0, (std::max)(0, static_cast<int>(g_book.size()) - visible));
			if (g_book.empty()) {
				Para(dl, F.bookIt, lp.x + S(40), y + S(10), pw - S(80), kInkSoft, "You know no recipes for this, yet.");
			}
			for (int i = s_first; i < static_cast<int>(g_book.size()) && i < s_first + visible; ++i) {
				const auto*  r = g_book[i];
				const ImVec2 a(lp.x + S(34), y), b(lp.x + pw - S(34), y + rowH);
				bool         hov = false;
				ImGui::PushID(i);
				if (Clicked("row", a, b, &hov)) {
					Post(Act::Select, i);
				}
				ImGui::PopID();
				const bool sel = i == g_sel, can = Recipes::CanMake(*r) > 0;
				if (sel || hov) {
					dl->AddRectFilledMultiColor(a, b, IM_COL32(142, 47, 27, sel ? 60 : 30), IM_COL32(142, 47, 27, 8), IM_COL32(142, 47, 27, 8), IM_COL32(142, 47, 27, sel ? 60 : 30));
				}
				if (sel) {
					Diamond(dl, ImVec2(a.x + S(4), a.y + rowH * 0.5f), S(3.5f), kRubric, 0);
				}
				Text(dl, F.book, ImVec2(a.x + S(14), a.y + (rowH - F.book->FontSize) * 0.5f), can ? kInk : Alpha(kInkSoft, 0.6f), r->name);
				bool              any = false;
				const auto        best = Recipes::Best(*r, any);
				const char*       mark = r->flow == hc::Flow::Pot ? "pot" : r->flow == hc::Flow::Spit ? "spit" : "oven";
				const std::string tag = any ? fmt::format("{} - {}", mark, hc::TierName(best)) : mark;
				Text(dl, F.bookIt, ImVec2(b.x - Measure(F.bookIt, tag, 0) - S(4), a.y + (rowH - F.bookIt->FontSize) * 0.5f), Alpha(any && best == hc::Tier::Masterwork ? kRubric : kInkSoft, can ? 1.0f : 0.6f), tag);
				y += rowH;
			}
			ImGui::End();

			// the right page: the chosen recipe
			BeginArea("##bookR", rp, ImVec2(pw, ph));
			dl = ImGui::GetWindowDrawList();
			Sheet(dl, rp, ImVec2(pw, ph), 1);
			if (g_sel >= 0 && g_sel < static_cast<int>(g_book.size())) {
				const auto* r = g_book[g_sel];
				float       ry = rp.y + S(30);
				TextC(dl, F.bookSC, rp.x + pw * 0.5f, ry, kInk, r->name);
				ry += S(40);
				const char* how = r->flow == hc::Flow::Pot ? "In the pot: chopped, simmered and stirred" : r->flow == hc::Flow::Spit ? "On the spit, turned over the fire"
				                : r->pie                    ? "A pie: dough, filling, a crimped edge, baked"
				                                            : "Kneaded and baked in the oven";
				TextC(dl, F.bookIt, rp.x + pw * 0.5f, ry, kInkSoft, how);
				ry += S(34);
				FadeRule(dl, rp.x + S(40), rp.x + pw - S(40), ry, 0.5f, kInkSoft);
				ry += S(18);
				Text(dl, F.bookSCsm, ImVec2(rp.x + S(44), ry), kRubric, "You will need", 0.06f);
				ry += S(28);
				for (const auto& in : r->ings) {
					const int   have = Recipes::Held(in.form);
					const bool  ok = have >= in.count;
					const auto  t = in.count > 1 ? fmt::format("{} x{}", in.name, in.count) : in.name;
					Text(dl, F.book, ImVec2(rp.x + S(52), ry), ok ? kInk : kRubric, t);
					const auto h = fmt::format("{} carried", have);
					Text(dl, F.bookIt, ImVec2(rp.x + pw - S(44) - Measure(F.bookIt, h, 0), ry + S(2)), ok ? kGreenInk : kRubric, h);
					ry += S(27);
				}
				ry += S(10);
				if (r->makes > 1) {
					Text(dl, F.bookIt, ImVec2(rp.x + S(52), ry), kInkSoft, fmt::format("Makes {}", r->makes));
					ry += S(26);
				}
				bool       any = false;
				const auto best = Recipes::Best(*r, any);
				Text(dl, F.bookIt, ImVec2(rp.x + S(52), ry), kInkSoft, any ? fmt::format("Your best: {}", hc::TierName(best)) : "Not made by hand yet");
				const bool can = Recipes::CanMake(*r) > 0;
				const float by = rp.y + ph - S(118);
				if (MenuBtnInk("cook", "Cook by hand", rp.x + pw * 0.5f, by, can)) {
					Post(Act::CookByHand);
				}
				if (MenuBtnInk("quick", "Quick cook (plain)", rp.x + pw * 0.5f, by + S(44), true)) {
					Post(Act::QuickCook);
				}
			}
			ImGui::End();
			// leave, bottom centre
			BeginArea("##leave", ImVec2(W * 0.5f - S(120), H - S(70)), ImVec2(S(240), S(50)));
			if (MenuButton("leave", "Leave", W * 0.5f, H - S(62))) {
				Post(Act::Leave);
			}
			ImGui::End();
		}

		// A button in ink on the parchment
		bool MenuBtnInk(const char* a_id, const std::string& a_label, float a_cx, float a_y, bool a_enabled)
		{
			auto*       dl = ImGui::GetWindowDrawList();
			const float w = Measure(F.bookSC, a_label, 0.04f) + S(40), h = F.bookSC->FontSize + S(8);
			bool        hov = false;
			const bool  r = Clicked(a_id, ImVec2(a_cx - w * 0.5f, a_y), ImVec2(a_cx + w * 0.5f, a_y + h), &hov, a_enabled);
			const ImU32 c = !a_enabled ? Alpha(kInkSoft, 0.45f) : hov ? kRubric : kInk;
			if (hov) {
				Diamond(dl, ImVec2(a_cx - w * 0.5f + S(6), a_y + h * 0.5f), S(4), kRubric, 0);
				Diamond(dl, ImVec2(a_cx + w * 0.5f - S(6), a_y + h * 0.5f), S(4), kRubric, 0);
			}
			TextC(dl, F.bookSC, a_cx, a_y + S(3), c, a_label, 0.04f);
			return r;
		}

		void RecipeCard(float W, float H)
		{
			(void)H;
			(void)W;
			const auto steps = Steps();
			const float w = S(380), rowH = S(26);
			const float h = S(30) + S(44) + S(14) + rowH * static_cast<float>(steps.size()) + S(26);
			const ImVec2 pos(S(22), S(22));
			BeginArea("##card", pos, ImVec2(w, h));
			auto* dl = ImGui::GetWindowDrawList();
			Sheet(dl, pos, ImVec2(w, h), 0);
			float y = pos.y + S(24);
			Text(dl, F.bookSC, ImVec2(pos.x + S(30), y), kInk, g_recipe ? g_recipe->name : "", 0.02f);
			y += S(42);
			FadeRule(dl, pos.x + S(26), pos.x + w - S(26), y, 0.45f, kInkSoft);
			y += S(12);
			bool first = true;
			for (const auto& st : steps) {
				const bool now = !st.done && first;
				if (!st.done) {
					first = false;
				}
				const ImVec2 c(pos.x + S(38), y + rowH * 0.5f);
				if (st.done) {
					dl->AddLine(ImVec2(c.x - S(5), c.y), ImVec2(c.x - S(1), c.y + S(4)), kGreenInk, S(2));
					dl->AddLine(ImVec2(c.x - S(1), c.y + S(4)), ImVec2(c.x + S(6), c.y - S(5)), kGreenInk, S(2));
				} else {
					Diamond(dl, c, S(3.5f), now ? kRubric : 0, Alpha(kInkSoft, 0.8f));
				}
				Text(dl, now ? F.book : F.bookIt, ImVec2(pos.x + S(54), y + (rowH - F.book->FontSize) * 0.5f), st.done ? Alpha(kInkSoft, 0.7f) : now ? kInk : kInkSoft, st.text);
				y += rowH;
			}
			ImGui::End();
		}

		void KeyBar(float W, float H)
		{
			struct Key
			{
				const char* key;
				const char* label;
				Act         act;
			};
			const bool pad = Overlay::UsingPad();
			const Key  keys[3] = { { pad ? "X" : "H", "How to cook", Act::Help }, { pad ? "Back" : "M", Audio::Muted() ? "Muted" : "Sound", Act::Mute },
				 { pad ? "Y" : "Tab", g_phase == Phase::Cooking ? "Put it aside" : "Leave", g_phase == Phase::Cooking ? Act::Abandon : Act::Leave } };
			float widths[3], total = 0;
			for (int k = 0; k < 3; ++k) {
				widths[k] = (std::max)(S(26), Measure(F.sansSemi, keys[k].key, 0.04f) + S(10)) + S(9) + Measure(F.sans, Upper(keys[k].label), 0.14f);
				total += widths[k];
			}
			total += S(26) * 2;
			const float  right = W - S(30), bottom = H - S(22), h = S(24);
			const ImVec2 area(right - total - S(120), bottom - h - S(26));
			BeginArea("##keys", area, ImVec2(W - area.x, H - area.y));
			auto* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilledMultiColor(area, ImVec2(area.x + (W - area.x) * 0.3f, H), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 158), IM_COL32(0, 0, 0, 158), IM_COL32(0, 0, 0, 0));
			dl->AddRectFilled(ImVec2(area.x + (W - area.x) * 0.3f, area.y), ImVec2(W, H), IM_COL32(0, 0, 0, 158));
			float x = right - total;
			for (int k = 0; k < 3; ++k) {
				bool hov = false;
				ImGui::PushID(k);
				if (Clicked("key", ImVec2(x, bottom - h - S(2)), ImVec2(x + widths[k], bottom + S(2)), &hov)) {
					if (keys[k].act == Act::Help) {
						g_help = !g_help;
					} else {
						Post(keys[k].act);
					}
				}
				ImGui::PopID();
				const ImU32  col = hov ? kWhite : kGrey;
				const float  kw = (std::max)(S(26), Measure(F.sansSemi, keys[k].key, 0.04f) + S(10));
				const ImVec2 ka(x, bottom - h), kb(x + kw, bottom);
				dl->AddRectFilled(ka, kb, IM_COL32(0, 0, 0, 90), S(4));
				dl->AddRect(ka, kb, col, S(4), 0, 1.5f * g_ui);
				TextC(dl, F.sansSemi, x + kw * 0.5f + S(0.5f), ka.y + (h - F.sansSemi->FontSize) * 0.5f, col, keys[k].key, 0.04f);
				Text(dl, F.sans, ImVec2(kb.x + S(9), ka.y + (h - F.sans->FontSize) * 0.5f), col, Upper(keys[k].label), 0.14f, 0.7f);
				x += widths[k] + S(26);
			}
			ImGui::End();
		}

		void HintLine(float W, float H)
		{
			const auto hint = Hint();
			if (hint.empty()) {
				return;
			}
			auto*       dl = ImGui::GetForegroundDrawList();
			const float tw = Measure(F.sans, hint, 0.07f);
			const float y = H - S(26) - S(25);
			dl->AddRectFilledMultiColor(ImVec2(W * 0.5f - tw * 0.5f - S(60), y - S(6)), ImVec2(W * 0.5f, y + S(31)), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120),
				IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 0));
			dl->AddRectFilledMultiColor(ImVec2(W * 0.5f, y - S(6)), ImVec2(W * 0.5f + tw * 0.5f + S(60), y + S(31)), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 0),
				IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120));
			Text(dl, F.sans, ImVec2(W * 0.5f - tw * 0.5f, y + (S(25) - F.sans->FontSize) * 0.5f), kWhite, hint, 0.07f, 0.9f);
		}

		// How done it is: raw, nearly, ready (the window that grades best), over; the bowl or plate lights up when it's ready
		void Meter()
		{
			double v = -1.0, lo = 0.9, hi = 1.3, top = 2.0;
			Vec3   at;
			const char* warn = nullptr;
			if (g_flow == hc::Flow::Pot && g_pot && g_chunks.CountIn(hc::Chunks::State::Pot) + g_tossedIn > 0) {
				v = g_pot->Cooked(), lo = 0.92, hi = 1.3, top = 2.0;
				at = { L::kPotX, L::kPotY + L::kPotR + 1.5, L::kPotRim + 3.0 };
				if (g_pot->Burn() > 0.25) {
					warn = "Catching on the bottom: stir!";
				}
			} else if (g_flow == hc::Flow::Spit && g_spit) {
				bool on = false;
				for (const auto& u : g_units) {
					on |= u.at == Unit::At::Spit;
				}
				if (on) {
					v = g_spit->Mean(), lo = 0.9, hi = 1.3, top = 1.9;
					at = { L::kFireX, L::kFireY + 4.0, L::kSpitZ + 7.0 };
					if (g_spit->Spread() > 0.25) {
						warn = "One side's browner: keep turning";
					}
				}
			} else if (g_flow == hc::Flow::Oven && g_oven && g_oven->InOven()) {
				v = g_oven->Brown(), lo = 0.85, hi = 1.2, top = 1.8;
				at = { L::kOvenX, L::kOvenY + 4.0, L::kOvenFloorZ + 14.0 };
			}
			if (v < 0.0 || g_served) {
				return;
			}
			float sx = 0, sy = 0;
			if (!Scene::Project(at, sx, sy)) {
				return;
			}
			auto*       dl = ImGui::GetForegroundDrawList();
			const float w = S(240), h = S(12);
			const float x0 = sx - w * 0.5f, y0 = sy - S(40);
			auto        X = [&](double a_v) { return x0 + w * static_cast<float>(Clamp(a_v / top, 0.0, 1.0)); };
			dl->AddRectFilled(ImVec2(x0 - S(10), y0 - S(30)), ImVec2(x0 + w + S(10), y0 + h + S(10)), IM_COL32(0, 0, 0, 120), S(6));
			dl->AddRectFilled(ImVec2(x0, y0), ImVec2(X(lo), y0 + h), IM_COL32(120, 110, 95, 200), S(3), ImDrawFlags_RoundCornersLeft);
			dl->AddRectFilled(ImVec2(X(lo), y0), ImVec2(X(hi), y0 + h), IM_COL32(205, 168, 72, 235));
			dl->AddRectFilled(ImVec2(X(hi), y0), ImVec2(x0 + w, y0 + h), IM_COL32(120, 52, 34, 210), S(3), ImDrawFlags_RoundCornersRight);
			dl->AddRectFilled(ImVec2(x0, y0), ImVec2(X(v), y0 + h), IM_COL32(255, 255, 255, 45), S(3));
			const float mx = X(v);
			dl->AddTriangleFilled(ImVec2(mx - S(7), y0 - S(9)), ImVec2(mx + S(7), y0 - S(9)), ImVec2(mx, y0 + S(1)), IM_COL32(250, 240, 220, 250));
			dl->AddLine(ImVec2(mx, y0), ImVec2(mx, y0 + h), IM_COL32(250, 240, 220, 250), S(2));
			const bool  ready = v >= lo && v <= hi;
			const char* word = v < lo * 0.65 ? "Raw" : v < lo ? "Nearly there" : v <= hi ? "Ready" : "Overdone";
			const std::string label = warn ? fmt::format("{} - {}", word, warn) : std::string(word);
			Text(dl, F.sans, ImVec2(x0, y0 - S(28)), ready ? IM_COL32(255, 220, 130, 255) : kWhite, label, 0.05f, 0.95f);
			// where it goes, glowing once it's ready
			if (ready || g_hold == Hold::LadleOut || g_hold == Hold::Meat || g_hold == Hold::Bread) {
				float bx = 0, by = 0;
				if (Scene::Project({ L::kServeX, L::kServeY, 2.0 }, bx, by)) {
					const float p = 0.5f + 0.5f * static_cast<float>(std::sin(g_clock * 4.0));
					const float r = (std::max)(S(30), Scene::PixelsPerUnit({ L::kServeX, L::kServeY, 2.0 }) * 5.5f);
					dl->AddCircle(ImVec2(bx, by), r + S(6) * p, IM_COL32(255, 214, 120, static_cast<int>(120 + 100 * p)), 48, S(3));
				}
			}
		}

		void ToastLine(float W, float H)
		{
			const double t = g_clock - g_toastAt;
			if (g_toast.empty() || t < 0 || t > 2.4) {
				return;
			}
			auto*       dl = ImGui::GetForegroundDrawList();
			const float a = static_cast<float>(t < 0.3 ? t / 0.3 : t > 2.0 ? (2.4 - t) / 0.4 : 1.0);
			const auto  text = Upper(g_toast);
			const float w = Measure(F.toastLong, text, 0.16f) + S(100);
			const float y = H * 0.2f;
			FadeRule(dl, W * 0.5f - w * 0.5f, W * 0.5f + w * 0.5f, y - S(10), 0.62f * a);
			FadeRule(dl, W * 0.5f - w * 0.5f, W * 0.5f + w * 0.5f, y + F.toastLong->FontSize + S(10), 0.62f * a);
			TextC(dl, F.toastLong, W * 0.5f, y, Alpha(kWhite, a), text, 0.16f, 0.9f * a);
		}

		void HelpBox(float W)
		{
			const float w = S(520), x = W * 0.5f - w * 0.5f, y = S(110);
			std::vector<std::string> lines;
			lines.push_back("Hold the left button to pick things up; let go to put them down. The right button puts back what you're holding.");
			if (g_flow == hc::Flow::Pot) {
				lines.push_back("Drag ingredients onto the board and click to chop across them; right-click (or R) turns the knife to cut along them. Both ways makes dice: small and even scores best.");
				lines.push_back("Grab the board by its left end and drag right to tip it into the pot. Line it up with the pot first.");
				lines.push_back("Hold the ladle in the pot and stir round. When the meter says it's ready, drag the ladle out of the pot and let go over the bowl to serve.");
			} else if (g_flow == hc::Flow::Spit) {
				lines.push_back("Drag the meat onto the spit. Hold the crank's handle and turn it round so every side faces the fire.");
				lines.push_back("Drag the roast onto the plate when it's brown all over.");
			} else {
				lines.push_back("A pie's fruit first: dice it on the board, then drag the board by its left end over the bowl behind it to sweep it in.");
				lines.push_back("Drag the flour, butter and the rest into the bowl of water and stir round until it's a ball; lift it onto the board and knead: long pushes, in a steady rhythm.");
				if (g_recipe && g_recipe->pie) {
					lines.push_back("Drag the bowl of filling over the dough to heap it on, then pinch round the edge to fold it up over the filling.");
				}
				lines.push_back("Open the door (drag it left). Drag the peel under the dough, carry it into the mouth, shut the door. Peek, but each peek lets the heat out.");
			}
			lines.push_back("The meter shows how done it is; there's no gauge for the heat: watch the flames. Logs from the pile make it hotter; drag a burning one out to cool it.");
			float h = S(80);
			for (const auto& l : lines) {
				h += F.sans->CalcTextSizeA(F.sans->FontSize, FLT_MAX, w - S(64), l.c_str()).y + S(10);
			}
			BeginArea("##help", ImVec2(x, y), ImVec2(w, h));
			auto* dl = ImGui::GetWindowDrawList();
			MsgBox(dl, ImVec2(x, y), ImVec2(x + w, y + h));
			TextC(dl, F.menu, W * 0.5f, y + S(22), kWhite, "HOW TO COOK", 0.26f);
			float yy = y + S(64);
			for (const auto& l : lines) {
				yy += Para(dl, F.sans, x + S(32), yy, w - S(64), kGrey, l) + S(10);
			}
			ImGui::End();
		}

		void ConfirmBox(float W, float H)
		{
			const float w = S(480), h = S(170), x = W * 0.5f - w * 0.5f, y = H * 0.38f;
			BeginArea("##confirm", ImVec2(x, y), ImVec2(w, h));
			auto* dl = ImGui::GetWindowDrawList();
			MsgBox(dl, ImVec2(x, y), ImVec2(x + w, y + h));
			TextC(dl, F.menu, W * 0.5f, y + S(26), kWhite, "PUT THIS DISH ASIDE?", 0.2f);
			TextC(dl, F.sans, W * 0.5f, y + S(66), kGrey, "Nothing is used up. You can start again.");
			switch (MenuButtons("confirm", { { "Put it aside", true }, { "Keep cooking", false } }, W * 0.5f, y + h - S(56))) {
			case 0:
				Post(Act::AbandonYes);
				break;
			case 1:
				Post(Act::AbandonNo);
				break;
			default:
				break;
			}
			ImGui::End();
		}

		void DrawResult(float W, float H)
		{
			auto* bg = ImGui::GetBackgroundDrawList();
			Vignette(bg, W, H);
			const double t = g_clock - g_serveAt;
			const float  a = static_cast<float>(Clamp(t / 0.6, 0.0, 1.0));
			const ImU32  tierCol = g_result.tier == hc::Tier::Masterwork ? kGold : g_result.tier == hc::Tier::Burnt ? IM_COL32(170, 150, 140, 255) : kWhite;
			// the tier, big, as the game announces a level up
			const auto title = Upper(hc::TierName(g_result.tier));
			const float ty = S(70);
			if (g_result.tier == hc::Tier::Masterwork) {
				Glow(bg, F.title, ImVec2(W * 0.5f - Measure(F.title, title, 0.12f) * 0.5f, ty), Alpha(kGold, 0.08f * a), title, 0.12f, S(8));
			}
			TextC(bg, F.title, W * 0.5f, ty, Alpha(tierCol, a), title, 0.12f, 0.9f * a);
			DiamondRule(bg, W * 0.5f, ty + F.title->FontSize + S(12), S(520));
			TextC(bg, F.menu, W * 0.5f, ty + F.title->FontSize + S(26), Alpha(kGrey, a), Upper(g_givenName + (g_recipe && g_recipe->makes > 1 ? fmt::format(" x{}", g_recipe->makes) : "")), 0.2f, 0.8f);
			if (g_result.tier == hc::Tier::Burnt && *g_result.burntWhy) {
				TextC(bg, F.sans, W * 0.5f, ty + F.title->FontSize + S(64), Alpha(IM_COL32(230, 170, 150, 255), a), g_result.burntWhy, 0.05f, 0.8f);
			}
			// how it went, on a card
			const float  w = S(430), rowH = S(30);
			const float  h = S(30) + S(40) + rowH * static_cast<float>(g_result.count) + S(28);
			const ImVec2 pos(W - w - S(40), H * 0.5f - h * 0.5f);
			BeginArea("##verdict", pos, ImVec2(w, h));
			auto* dl = ImGui::GetWindowDrawList();
			Sheet(dl, pos, ImVec2(w, h), 1);
			float y = pos.y + S(24);
			TextC(dl, F.bookSC, pos.x + w * 0.5f, y, kInk, "How it went");
			y += S(42);
			for (int i = 0; i < g_result.count; ++i) {
				const auto& l = g_result.lines[i];
				Text(dl, F.bookSCsm, ImVec2(pos.x + S(30), y + S(4)), kRubric, l.label, 0.04f);
				const ImU32 c = l.score >= 0.8 ? kGreenInk : l.score >= 0.5 ? kInk : kRubric;
				Text(dl, F.hand, ImVec2(pos.x + w - S(30) - Measure(F.hand, l.verdict, 0), y), c, l.verdict);
				y += rowH;
			}
			ImGui::End();
			BeginArea("##again", ImVec2(W * 0.5f - S(300), H - S(90)), ImVec2(S(600), S(60)));
			switch (MenuButtons("result", { { "Cook another", false }, { "Done", false } }, W * 0.5f, H - S(80))) {
			case 0:
				Post(Act::Again);
				break;
			case 1:
				Post(Act::Done);
				break;
			default:
				break;
			}
			ImGui::End();
		}

		void DebugMarks()
		{
			if (!Config::Get().debugOverlay || g_phase != Phase::Cooking) {
				return;
			}
			auto* dl = ImGui::GetForegroundDrawList();
			auto  mark = [&](const Vec3& p, ImU32 c) {
				float sx = 0, sy = 0;
				if (Scene::Project(p, sx, sy)) {
					dl->AddCircle(ImVec2(sx, sy), S(10), c, 16, 2.0f);
				}
			};
			for (const auto& u : g_units) {
				mark(u.clothAt + Vec3{ 0, 0, 1.0 }, IM_COL32(120, 255, 120, 200));
			}
			mark({ L::kSaltX, L::kSaltY, 1.5 }, IM_COL32(255, 255, 255, 200));
			mark({ L::kServeX, L::kServeY, 1.0 }, IM_COL32(255, 220, 120, 200));
			mark({ L::kPileX, L::kPileY, 1.5 }, IM_COL32(255, 140, 80, 200));
			if (g_flow == hc::Flow::Spit) {
				mark(CrankGrip(), IM_COL32(120, 180, 255, 200));
			}
			if (g_flow == hc::Flow::Oven) {
				mark(DoorCentre(), IM_COL32(120, 180, 255, 200));
				mark(MouthAt(), IM_COL32(255, 120, 120, 200));
			}
		}

		// ---------------------------------------------------------------- the station: the game's crafting menu at a cooking pot or oven
		bool IsCookingStation(RE::TESFurniture* a_f, bool& a_oven)
		{
			if (!a_f) {
				return false;
			}
			a_oven = a_f->HasKeywordString("BYOHCraftingOven");
			return a_oven || a_f->HasKeywordString("CraftingCookpot");
		}

		void OnCraftingOpened()
		{
			if (!Config::Get().enabled || Scene::IsOpen()) {
				return;
			}
			if (g_quick) {
				g_quick = false;  // they asked for the game's own menu
				return;
			}
			auto menu = Crafting();
			auto* sub = menu ? menu->GetCraftingSubMenu() : nullptr;
			bool  oven = false;
			if (!sub || !IsCookingStation(sub->furniture, oven)) {
				return;
			}
			std::lock_guard lk(g_mtx);
			g_ovenStation = oven;
			g_book = Recipes::ForStation(oven);
			g_sel = 0;
			ShowCrafting(false);
			Scene::Open();
			ShowStation();
			g_phase = Phase::Book;
			g_clock = 0;
			Audio::Ready();
			spdlog::info("At the {}: {} recipes", oven ? "oven" : "cooking pot", g_book.size());
		}

		class MenuWatch final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_e, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_e || std::string_view(a_e->menuName.c_str()) != RE::CraftingMenu::MENU_NAME) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (a_e->opening) {
					SKSE::GetTaskInterface()->AddTask([] { OnCraftingOpened(); });
				} else {
					SKSE::GetTaskInterface()->AddTask([] {
						g_quick = false;
						if (Scene::IsOpen()) {
							std::lock_guard lk(g_mtx);
							CloseKitchen(false);  // the station went away under us
						}
					});
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void OnDataLoaded()
	{
		Kit::Resolve();
		Recipes::OnDataLoaded();
		Recipes::InstallEatWatch();
		Scene::Register();
		static MenuWatch s_watch;
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&s_watch);
		}
	}

	void OnGameLoaded()
	{
		std::lock_guard lk(g_mtx);
		g_quick = false;
		if (Scene::IsOpen()) {
			CloseKitchen(false);
		}
	}

	void Tick(float a_dt)
	{
		std::lock_guard lk(g_mtx);
		const double    dt = std::clamp(static_cast<double>(a_dt), 0.001, 0.05);
		g_clock += dt;
		ProcessActs();
		if (g_phase == Phase::Idle) {
			return;
		}
		HandleKeys();
		if (g_phase == Phase::Cooking && !g_confirm && !g_help) {
			HandleInput(dt);
		} else {
			Overlay::TakePresses();  // the buttons are ImGui's
			if (g_hold != Hold::None) {
				Cancel();
			}
		}
		if (g_phase != Phase::Result) {
			Step(dt);
		} else {
			g_fire.Step(dt);
			Audio::SetLoop("loop_fire", static_cast<float>(Clamp(g_fire.Heat() * 0.5, 0.0, 0.6)));
			Audio::SetLoop("loop_simmer", 0.0f);
			Audio::SetLoop("loop_boil", 0.0f);
			Audio::SetLoop("loop_sizzle", 0.0f);
		}
		Look3D();
	}

	void Abort()
	{
		std::lock_guard lk(g_mtx);
		spdlog::error("The kitchen hit a problem and was closed; nothing was used up");
		CloseKitchen(true);
	}

	void SetupUi(float a_screenH) { Ui::Setup(a_screenH); }

	void DrawUi()
	{
		std::lock_guard lk(g_mtx);
		if (!Scene::IsOpen() || g_phase == Phase::Idle) {
			return;
		}
		Ui::LoadArt();
		auto& io = ImGui::GetIO();
		io.MouseDrawCursor = false;
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		switch (g_phase) {
		case Phase::Book:
			DrawBook(W, H);
			break;
		case Phase::Cooking:
			Vignette(ImGui::GetBackgroundDrawList(), W, H);
			RecipeCard(W, H);
			Meter();
			HintLine(W, H);
			if (g_confirm) {
				ConfirmBox(W, H);
			}
			break;
		case Phase::Result:
			DrawResult(W, H);
			break;
		default:
			break;
		}
		KeyBar(W, H);
		if (g_help && g_phase == Phase::Cooking) {
			HelpBox(W);
		}
		ToastLine(W, H);
		DebugMarks();
		// the pointer: a hand-held look while carrying
		Ui::Cursor(g_hold != Hold::None ? IM_COL32(255, 226, 170, 245) : IM_COL32(241, 238, 232, 245));
	}

	void Save(SKSE::SerializationInterface* a_intfc) { Recipes::Save(a_intfc); }
	void Load(SKSE::SerializationInterface* a_intfc) { Recipes::Load(a_intfc); }
	void Revert(SKSE::SerializationInterface*) { Recipes::Revert(); }
}
