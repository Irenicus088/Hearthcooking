# -*- coding: utf-8 -*-
# Hearthcooking + SCIE: que la olla/horno/asador cuente y gaste lo que hay en los contenedores que SCIE tiene activos.
# Uso: python scie_patch.py <Recipes.cpp>   (lo modifica en el lugar; si ya esta aplicado no hace nada)
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
if "ScieCount(" in s:
    print("ya aplicado"); sys.exit(0)

HELPER = '''	namespace
	{
		// SCIE (Skyrim Crafting Inventory Extender) intercepta esta funcion del juego (SE 15869 / AE 16109) mientras
		// estas sentado en una mesa: devuelve lo que llevas mas lo de los contenedores que tiene activos, y al gastar
		// (RemoveItem) saca primero de vos y despues de ellos. Sin SCIE contamos como siempre.
		struct AllEntries
		{
			virtual void Unused() {}
			virtual bool Accept(RE::InventoryEntryData*) { return true; }  // el juego llama a [vtabla + 8]
		};

		bool ScieLoaded()
		{
			static const bool on = [] {
				const bool b = !REL::Module::IsVR() && ::GetModuleHandleW(L"CraftingInventoryExtender.dll") != nullptr;
				spdlog::info("SCIE {}: {}", b ? "found" : "not found", b ? "the kitchen counts nearby containers" : "the inventory only");
				return b;
			}();
			return on;
		}

		int ScieCount(RE::PlayerCharacter* a_pc, RE::TESBoundObject* a_form)
		{
			if (ScieLoaded()) {
				if (auto* inv = a_pc->GetInventoryChanges()) {
					using Fn = std::int32_t (*)(RE::InventoryChanges*, RE::TESBoundObject*, void*);
					static REL::Relocation<Fn> fn{ RELOCATION_ID(15869, 16109) };
					AllEntries all;
					return (std::max)(0, fn(inv, a_form, &all));
				}
			}
			const auto counts = a_pc->GetInventoryCounts([a_form](RE::TESBoundObject& a_o) { return &a_o == a_form; });
			const auto it = counts.find(a_form);
			return it == counts.end() ? 0 : (std::max)(0, it->second);
		}
	}

'''

old_held = '''		const auto ins = StandIns(a_form);
		const auto counts = pc->GetInventoryCounts([&ins](RE::TESBoundObject& a_o) { return std::find(ins.begin(), ins.end(), &a_o) != ins.end(); });
		int n = 0;
		for (const auto& [f, c] : counts) {
			n += (std::max)(0, c);
		}
		return n;'''
new_held = '''		int n = 0;
		for (auto* f : StandIns(a_form)) {
			n += ScieCount(pc, f);
		}
		return n;'''
old_cons = '''				const auto counts = pc->GetInventoryCounts([f](RE::TESBoundObject& a_o) { return &a_o == f; });
				const auto it = counts.find(f);
				const int  have = it == counts.end() ? 0 : (std::max)(0, it->second);'''
new_cons = '''				const int  have = ScieCount(pc, f);'''
anchor = "	int Held(RE::TESBoundObject* a_form)\n"
for name, a in (("Held", old_held), ("Consume", old_cons), ("ancla", anchor)):
    if s.count(a) != 1:
        print("NO encontre el bloque", name, "- el autor cambio el codigo, hay que revisarlo a mano"); sys.exit(1)
s = s.replace(old_held, new_held).replace(old_cons, new_cons).replace(anchor, HELPER + anchor)
open(p, "w", encoding="utf-8", newline="").write(s)
print("aplicado")
