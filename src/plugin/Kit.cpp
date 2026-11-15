// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Kit.h"

namespace Kit
{
	namespace
	{
		constexpr const char* kIni = "Data\\SKSE\\Plugins\\Hearthcooking.ini";
		constexpr const char* kNames[] = { "Table", "Pot", "Ladle", "Knife", "Board", "Bowl", "Plate", "Log", "SaltBowl", "Salt", "FirePit", "Spit", "Oven", "Peel" };
		constexpr const char* kOurs[] = { "Worktop", "Pot", "Ladle", "Knife", "Board", "Bowl", "Plate", "Log", "SaltBowl", "", "FirePit", "SpitPosts", "OvenDome", "Peel" };

		std::array<Mesh, static_cast<int>(Role::Count)> g_mesh;
		std::vector<std::string>                       g_decor;

		struct Form
		{
			std::string path, file, name;  // the mesh's path (as the game has it), its file name and the form's name, lowered
			std::string shown;             // the path as it is
			char        type = 'M';        // M misc, S static, F furniture, A food, I ingredient
			bool        cookpot = false, oven = false;
			std::string plugin;
		};

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}
		bool In(const std::string& a_s, const char* a_w) { return a_s.find(a_w) != std::string::npos; }
		bool Any(const std::string& a_s, std::initializer_list<const char*> a_ws)
		{
			for (const char* w : a_ws) {
				if (In(a_s, w)) {
					return true;
				}
			}
			return false;
		}

		std::vector<Form> Gather()
		{
			std::vector<Form> out;
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) {
				return out;
			}
			auto add = [&](RE::TESForm* a_f, char a_type) {
				auto* m = a_f ? a_f->As<RE::TESModel>() : nullptr;
				const char* p = m ? m->GetModel() : nullptr;
				if (!p || !*p) {
					return;
				}
				Form f;
				f.shown = p;
				f.path = Lower(p);
				const auto slash = f.path.find_last_of("\\/");
				f.file = slash == std::string::npos ? f.path : f.path.substr(slash + 1);
				f.name = Lower(a_f->GetName() ? a_f->GetName() : "");
				f.type = a_type;
				if (auto* file = a_f->GetFile(0)) {
					f.plugin = file->GetFilename();
				}
				if (a_type == 'F') {
					if (auto* kw = a_f->As<RE::BGSKeywordForm>()) {
						f.cookpot = kw->HasKeywordString("CraftingCookpot");
						f.oven = kw->HasKeywordString("BYOHCraftingOven");
					}
				}
				out.push_back(std::move(f));
			};
			for (auto* f : dh->GetFormArray<RE::TESObjectMISC>()) {
				add(f, 'M');
			}
			for (auto* f : dh->GetFormArray<RE::TESObjectSTAT>()) {
				add(f, 'S');
			}
			for (auto* f : dh->GetFormArray<RE::TESFurniture>()) {
				add(f, 'F');
			}
			for (auto* f : dh->GetFormArray<RE::AlchemyItem>()) {
				add(f, 'A');
			}
			for (auto* f : dh->GetFormArray<RE::IngredientItem>()) {
				add(f, 'I');
			}
			return out;
		}

		// How well a form suits a role (0: not at all)
		int Score(Role a_r, const Form& f)
		{
			const auto& n = f.name;
			const auto& x = f.file;
			if (In(x, "marker")) {
				return 0;  // a furniture marker has nothing to see
			}
			switch (a_r) {
			case Role::Table:
				if ((f.type != 'S' && f.type != 'F') || !In(x, "table") ||
					Any(x, { "alchemy", "enchant", "workbench", "tanning", "smith", "throne", "end", "night", "map", "dwe", "ruin", "broken", "chair", "bench" })) {
					return 0;
				}
				return In(x, "commontable") ? 10 : In(x, "kitchen") ? 9 : In(x, "uppertable") || In(x, "noble") ? 7 : 3;
			case Role::Pot:
				// not the cooking station (also called "Cooking Pot"): that's the whole fire pit with a pot built in
				if (f.type == 'F' || Any(x, { "furniture", "crafting" })) {
					return 0;
				}
				if (Any(x, { "potato", "potion", "pottery", "spot", "teapot", "potema", "pothook", "flowerpot", "chamber" }) || Any(n, { "potato", "potion" })) {
					return 0;
				}
				if (n == "cooking pot" || n == "iron pot") {
					return 12;
				}
				if (In(x, "cookingpot") || In(x, "cookpot")) {
					return f.type == 'F' ? 0 : 10;
				}
				if (In(x, "ironpot")) {
					return 9;
				}
				if (In(x, "cauldron") && f.type != 'F') {
					return 6;
				}
				return f.type == 'M' && In(x, "pot") ? 3 : 0;
			case Role::Ladle:
				return In(n, "ladle") ? 10 : In(x, "ladle") ? 8 : 0;
			case Role::Knife:
				if (Any(x, { "dagger", "sheath", "rack", "dwe", "silver", "gold", "ebony", "glass", "elven", "daedric" })) {
					return 0;
				}
				if (Any(x, { "kitchen", "butcher", "cleaver", "carving" })) {
					return 10;
				}
				return In(n, "knife") ? 9 : In(x, "knife") ? 7 : 0;
			case Role::Board:
				return In(x, "cuttingboard") ? 10 : In(x, "breadboard") ? 8 : In(n, "cutting board") ? 9 : 0;
			case Role::Bowl:
			case Role::SaltBowl:
				if (Any(x, { "salt", "silver", "gold", "soup", "stew", "mammoth", "cheese", "dwe", "pot" }) || Any(n, { "bowl of", "soup", "stew" })) {
					return 0;
				}
				return n == "wooden bowl" ? 10 : In(x, "woodenbowl") ? 9 : In(x, "woodbowl") ? 8 : (f.type == 'M' && In(x, "bowl")) ? 3 : 0;
			case Role::Plate:
				if (Any(x, { "armor", "cuirass", "gauntlet", "boot", "helmet", "silver", "gold", "dwe", "plated" })) {
					return 0;
				}
				return n == "wooden plate" ? 10 : In(x, "woodenplate") ? 9 : In(x, "woodplate") ? 8 : (f.type == 'M' && In(x, "plate")) ? 2 : 0;
			case Role::Log:
				return n == "firewood" ? 10 : In(x, "firewood") ? 9 : 0;
			case Role::Salt:
				return n == "salt pile" ? 10 : (f.type == 'I' || f.type == 'A') && In(x, "salt") ? 5 : 0;
			case Role::FirePit:
				if (f.type != 'S' || Any(x, { "fx", "smoke", "light", "brazier", "dwe" })) {
					return 0;
				}
				return In(x, "firepit") ? 9 : In(x, "campfire") ? 8 : 0;
			case Role::Spit:
				if (f.type == 'F' && f.cookpot && In(x, "spit")) {
					return 10;
				}
				return f.type == 'S' && In(x, "spit") && !In(x, "spittoon") ? 5 : 0;
			case Role::Oven:
				// the oven itself (a static), never its furniture marker (invisible: only where you stand to use it)
				if (In(x, "marker")) {
					return 0;
				}
				if (f.type == 'S' && In(x, "oven") && !Any(x, { "stove", "dwe" })) {
					return In(x, "byoh") ? 10 : 8;
				}
				return f.type == 'F' && f.oven ? 6 : 0;
			case Role::Peel:
				return In(x, "peel") && In(x, "bread") ? 8 : 0;
			default:
				return 0;
			}
		}

		std::string Ini(const char* a_key)
		{
			char buf[512]{};
			::GetPrivateProfileStringA("Meshes", a_key, "", buf, sizeof(buf), kIni);
			std::string s = buf;
			if (const auto c = s.find_first_of(";#"); c != std::string::npos) {
				s.resize(c);
			}
			while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
				s.pop_back();
			}
			while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
				s.erase(s.begin());
			}
			return s;
		}
		double IniNum(const char* a_key, double a_def)
		{
			const auto s = Ini(a_key);
			try {
				return s.empty() ? a_def : std::stod(s);
			} catch (...) {
				return a_def;
			}
		}
	}

	const char* Name(Role a_role) { return kNames[static_cast<int>(a_role)]; }

	const Mesh& Get(Role a_role) { return g_mesh[static_cast<int>(a_role)]; }

	std::string Ours(Role a_role)
	{
		const char* o = kOurs[static_cast<int>(a_role)];
		return *o ? std::string("Hearthcooking\\") + o + ".nif" : std::string();
	}

	const std::vector<std::string>& Decor() { return g_decor; }

	void Resolve()
	{
		const auto forms = Gather();
		spdlog::info("Kitchen: looking through {} meshes for the kitchen's things", forms.size());
		for (int k = 0; k < static_cast<int>(Role::Count); ++k) {
			const auto role = static_cast<Role>(k);
			auto&      m = g_mesh[k];
			m = Mesh{};
			// the best the game has
			const Form* best = nullptr;
			int         bestScore = 0;
			for (const auto& f : forms) {
				const int s = Score(role == Role::SaltBowl ? Role::Bowl : role, f);
				if (s > bestScore || (s == bestScore && s > 0 && best && f.path.size() < best->path.size())) {
					best = &f;
					bestScore = s;
				}
			}
			if (best) {
				m.path = best->shown;
				m.vanilla = true;
			}
			// what the INI says ("ours" for Hearthcooking's own)
			const std::string key = kNames[k];
			const auto        set = Ini(key.c_str());
			if (Lower(set) == "ours") {
				m = Mesh{};
			} else if (!set.empty()) {
				m.path = set;
				m.vanilla = true;
			}
			m.turn = IniNum((key + "Turn").c_str(), 0.0);
			m.tilt = IniNum((key + "Tilt").c_str(), 0.0);
			if (!m.vanilla) {
				m.path = Ours(role);
			}
			spdlog::info("Kitchen: {:<8} {} {}{}", key, m.vanilla ? "the game's" : "ours      ", m.path.empty() ? "(none)" : m.path,
				best && m.vanilla && m.path == best->shown ? fmt::format(" ({}, \"{}\", from {})", best->type == 'F' ? "furniture" : best->type == 'S' ? "static" : "item", best->name, best->plugin) : "");
		}
		// the clutter: the first of each that the game has, in this order
		g_decor.clear();
		// (no vegetables or fruit: they'd be taken for the recipe's own)
		static constexpr const char* kWant[] = { "jug", "basket", "tankard", "candlestick", "pitcher", "sack" };
		for (const char* want : kWant) {
			const Form* pick = nullptr;
			for (const auto& f : forms) {
				if (f.type == 'M' && (f.name == want || (f.name.find(want) == 0 && f.name.size() < std::strlen(want) + 8))) {
					pick = &f;
					break;
				}
			}
			if (pick && std::find(g_decor.begin(), g_decor.end(), pick->shown) == g_decor.end()) {
				g_decor.push_back(pick->shown);
				spdlog::info("Kitchen: clutter   {} (\"{}\")", pick->shown, pick->name);
			}
			if (g_decor.size() >= 9) {
				break;
			}
		}
		for (int k = 1; k <= 9; ++k) {  // or the INI's own list
			const auto v = Ini(fmt::format("Clutter{}", k).c_str());
			if (k == 1 && !v.empty()) {
				g_decor.clear();
			}
			if (!v.empty() && Lower(v) != "none") {
				g_decor.push_back(v);
			}
		}
	}
}
