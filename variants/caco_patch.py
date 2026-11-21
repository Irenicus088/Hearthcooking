# -*- coding: utf-8 -*-
# Hearthcooking - CACO compatibility (Complete Alchemy & Cooking Overhaul)
# Uso: python caco_patch.py <carpeta src\plugin>   (modifica Recipes.cpp, Cook.cpp, Scene.h y Scene.cpp; si ya esta aplicado no hace nada)
# Solo actua en el juego si "Complete Alchemy & Cooking Overhaul.esp" esta cargado y Hearthcooking.ini [CACO] Patch no es 0.
#  1. Lo que no es cocinar queda para el menu del juego (Quick Cook): recetas cuyo resultado no es comida (agua, separar
#     leche, vinos, vendas, cataplasmas, sebo, juntar sal, panal, huevas) o es comida cruda (cortar carne en porciones, masa).
#  2. La masa de CACO ("dough") entra al horno como masa, no como relleno.
#  3. Un ingrediente cuya malla la cocina no puede dibujar (formato viejo, p. ej. la jarra de leche de CACO) usa el trozo
#     generico en vez de quedar invisible (esto vale con o sin CACO).
#  4. Porciones de CACO (necesita caco_porciones.cpp.txt en la misma carpeta): ver el bloque 4 mas abajo.
#  5. Recetas repetidas (una con saco/sal/botella entera y otra con porciones): queda la de porciones. Bloque 5.
import sys, os
d = sys.argv[1]
def rd(f): return open(os.path.join(d, f), encoding='utf-8', newline='').read()
def wr(f, s): open(os.path.join(d, f), 'w', encoding='utf-8', newline='').write(s)
def rep(s, old, new, what):
    if s.count(old) != 1:
        print('NO encontre', what, '- el autor cambio el codigo, hay que revisarlo a mano'); sys.exit(1)
    return s.replace(old, new)

R = rd('Recipes.cpp')
if 'CacoPatch(' in R:
    print('ya aplicado'); sys.exit(0)
HELP = '''		// CACO (Complete Alchemy & Cooking Overhaul) loaded and the patch on (Hearthcooking.ini [CACO] Patch, 1 by default)
		bool CacoPatch()
		{
			static const bool on = [] {
				auto*      dh = RE::TESDataHandler::GetSingleton();
				const bool caco = dh && dh->LookupModByName("Complete Alchemy & Cooking Overhaul.esp");
				const bool b = caco && ::GetPrivateProfileIntA("CACO", "Patch", 1, "Data\\\\SKSE\\\\Plugins\\\\Hearthcooking.ini") != 0;
				spdlog::info("CACO patch: {}", b ? "on" : caco ? "off (Hearthcooking.ini [CACO] Patch = 0)" : "off (CACO isn't loaded)");
				return b;
			}();
			return on;
		}

		Role RoleOf('''
R = rep(R, '\t\tRole RoleOf(', HELP, 'RoleOf')
R = rep(R, 'if (Has(low, { "flour", "butter", "milk", "egg", "yeast", "water", "lard", "sack" })) {',
           'if (Has(low, { "flour", "butter", "milk", "egg", "yeast", "water", "lard", "sack" }) || (CacoPatch() && Has(low, { "dough" }))) {',
           'masa')
R = rep(R, '''				if (!result || !result->GetName() || !*result->GetName()) {
					continue;
				}
''', '''				if (!result || !result->GetName() || !*result->GetName()) {
					continue;
				}
				// (CACO) what isn't cooking stays with the game's own menu (Quick Cook): what isn't food (water, splitting
				// milk, wines, bandages, poultices, tallow, joining salt...) and raw food (meat cut into portions, dough)
				if (CacoPatch()) {
					auto*      food = result->As<RE::AlchemyItem>();
					auto*      kw = result->As<RE::BGSKeywordForm>();
					const bool raw = kw && kw->HasKeywordString("VendorItemFoodRaw");
					if (!food || !food->IsFood() || raw) {
						spdlog::info("CACO: {} left to the game's own menu ({})", result->GetName(), raw ? "raw food" : "not food");
						continue;
					}
				}
''', 'Build')
# 4. Porciones: CACO divide sal, agua, harinas, vinos, aceite y vinagre en porciones al sentarte y las junta al levantarte.
#    La cocina cuenta todo en porciones (un saco lleno = 20, monton de sal = 5, botella de vino = 4: el lugar en la lista
#    de CACO), asi una receta que pide el saco ve tus porciones y una que pide porciones ve tus sacos; al gastar, primero
#    lo justo y si hay que abrir un saco devuelve el vuelto en porciones. Con SCIE cuenta tambien los contenedores.
COUNT = ('return ScieCount(a_pc, a_form);' if 'ScieCount(' in R else
         'const auto counts = a_pc->GetInventoryCounts([a_form](RE::TESBoundObject& a_o) { return &a_o == a_form; });\n'
         '\t\t\tconst auto it = counts.find(a_form);\n'
         '\t\t\treturn it == counts.end() ? 0 : (std::max)(0, it->second);')
PORTIONS = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'caco_porciones.cpp.txt'), encoding='utf-8', newline='').read().replace('@COUNT@', COUNT)
R = rep(R, '\tint Held(RE::TESBoundObject* a_form)\n', PORTIONS + '\tint Held(RE::TESBoundObject* a_form)\n', 'Held (porciones)')
HELD_A = '\tint Held(RE::TESBoundObject* a_form)\n\t{\n\t\tauto* pc = RE::PlayerCharacter::GetSingleton();\n\t\tif (!pc || !a_form) {\n\t\t\treturn 0;\n\t\t}\n'
R = rep(R, HELD_A, HELD_A + '\t\tif (const auto* p = PortionOf(a_form)) {\n\t\t\treturn GroupPortions(pc, g_groups[p->group]) / p->worth;\n\t\t}\n', 'Held')
CAN_A = '\t\tfor (const auto& in : a_r.ings) {\n\t\t\tn = (std::min)(n, Held(in.form) / (std::max)(1, in.count));\n\t\t}\n'
CAN_B = ('\t\tstd::map<int, int> need;  // (CACO) a group of portions -> what one dish takes of it, all together\n'
         '\t\tfor (const auto& in : a_r.ings) {\n'
         '\t\t\tif (const auto* p = PortionOf(in.form)) {\n'
         '\t\t\t\tneed[p->group] += p->worth * (std::max)(1, in.count);\n'
         '\t\t\t\tcontinue;\n'
         '\t\t\t}\n'
         '\t\t\tn = (std::min)(n, Held(in.form) / (std::max)(1, in.count));\n'
         '\t\t}\n'
         '\t\tif (auto* pc = RE::PlayerCharacter::GetSingleton()) {\n'
         '\t\t\tfor (const auto& [g, per] : need) {\n'
         '\t\t\t\tn = (std::min)(n, GroupPortions(pc, g_groups[g]) / per);\n'
         '\t\t\t}\n'
         '\t\t}\n')
R = rep(R, CAN_A, CAN_B, 'CanMake')
CON_A = '\t\tfor (const auto& in : a_r.ings) {\n\t\t\t// the thing itself first, then our dishes of it, plainest first\n'
CON_B = ('\t\tfor (const auto& in : a_r.ings) {\n'
         '\t\t\tif (const auto* p = PortionOf(in.form)) {\n'
         '\t\t\t\tif (in.count > 0) {\n'
         '\t\t\t\t\tTakePortions(pc, g_groups[p->group], p->worth * in.count, in.form, p->worth);\n'
         '\t\t\t\t}\n'
         '\t\t\t\tcontinue;\n'
         '\t\t\t}\n'
         '\t\t\t// the thing itself first, then our dishes of it, plainest first\n')
R = rep(R, CON_A, CON_B, 'Consume')
# 5. Recetas repetidas: con CACO, si el mismo plato (mismo nombre y malla) se hace dos veces en el mismo lugar, una con algo
#    entero que CACO divide (saco, monton de sal, botella) y otra sin eso, queda solo la segunda (el libro mas parejo).
R = rep(R, '\t\tvoid Build()\n', '\t\tbool CacoWhole(RE::TESBoundObject* a_form);  // (CACO) below, with the portions\n\n\t\tvoid Build()\n', 'Build (declaracion)')
DEDUP = '''			// (CACO) the same dish twice in the same place, once with a whole thing CACO splits (a sack of flour, a salt
			// pile, a bottle) and once without: only the second, so the book stays even
			if (CacoPatch()) {
				auto whole = [](const Recipe& a_r) {
					return std::any_of(a_r.ings.begin(), a_r.ings.end(), [](const Ingredient& a_i) { return CacoWhole(a_i.form); });
				};
				std::map<std::pair<std::string, bool>, int> plain;  // (dish, oven) -> its recipes without a whole thing
				for (const auto& r : g_recipes) {
					if (!whole(*r)) {
						++plain[{ r->key, r->oven }];
					}
				}
				const auto before = g_recipes.size();
				std::erase_if(g_recipes, [&](const std::unique_ptr<Recipe>& a_r) {
					if (whole(*a_r) && plain[{ a_r->key, a_r->oven }] > 0) {
						spdlog::info("CACO: {} ({}) left out, another recipe makes it with CACO's portions", a_r->name, a_r->source);
						return true;
					}
					return false;
				});
				spdlog::info("CACO: {} recipe(s) made twice left out", before - g_recipes.size());
			}
'''
LOGR = '\t\t\tspdlog::info("Recipes: {} for the pot, {} for the spit, {} for the oven", pot, spit, oven);\n'
R = rep(R, LOGR, DEDUP + LOGR, 'Build (repetidas)')
wr('Recipes.cpp', R)

C = rd('Cook.cpp')
C = rep(C, '''			const auto id = Scene::Add(a_u.model);
			if (g_recipe && a_u.ing >= 0''', '''			const auto id = Scene::Add(a_u.model);
			if (id >= 0 && !Scene::Drawable(id)) {
				// a mesh the kitchen can't draw (an old-format one): the stand-in, not an invisible ingredient
				spdlog::warn("Kitchen: {} ({}) has nothing the kitchen can draw; a stand-in instead", a_u.name, a_u.model);
				Scene::Remove(id);
				return -1;
			}
			if (g_recipe && a_u.ing >= 0''', 'AddUnitMesh')
wr('Cook.cpp', C)

H = rd('Scene.h')
H = rep(H, '\tbool  Valid(Id a_id);\n', '\tbool  Valid(Id a_id);\n\tbool  Drawable(Id a_id);  // it has a shape the kitchen can draw (lit, of the game\'s own kind)\n', 'Scene.h')
wr('Scene.h', H)
S = rd('Scene.cpp')
S = rep(S, '\tbool Valid(Id a_id) { return a_id >= 0 && a_id < static_cast<int>(g_props.size()) && g_props[a_id].used; }\n',
'''\tbool Valid(Id a_id) { return a_id >= 0 && a_id < static_cast<int>(g_props.size()) && g_props[a_id].used; }

	bool Drawable(Id a_id)
	{
		if (!Valid(a_id) || !g_props[a_id].node) {
			return false;
		}
		bool any = false;
		RE::BSVisit::TraverseScenegraphGeometries(g_props[a_id].node.get(), [&](RE::BSGeometry* a_geo) {
			auto* tri = a_geo ? a_geo->AsTriShape() : nullptr;
			auto* prop = tri ? a_geo->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
			auto* lit = prop ? netimmerse_cast<RE::BSLightingShaderProperty*>(prop) : nullptr;
			if (lit && lit->material) {
				any = true;
				return RE::BSVisit::BSVisitControl::kStop;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return any;
	}
''', 'Scene.cpp')
wr('Scene.cpp', S)
print('aplicado')
