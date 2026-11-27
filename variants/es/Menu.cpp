// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
//
// The settings page in SKSE Menu Framework (if it's installed): every INI setting, changed live, saved to the INI.
#include "Menu.h"
#include "Config.h"
#include "Kit.h"

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "SKSEMenuFramework.h"

namespace Menu
{
	namespace
	{
		namespace UI = ImGuiMCP;

		bool g_dirty = false;

		void Hint(const char* a_text)
		{
			UI::TextDisabled("%s", a_text);
			UI::Spacing();
		}
		void Changed(bool a_changed) { g_dirty |= a_changed; }

		void __stdcall Settings()
		{
			auto& v = Config::Edit();
			UI::SeparatorText("Hearthcooking");
			Changed(UI::Checkbox("Cocinar a mano en ollas y hornos", &v.enabled));
			Hint("Desactivado: la olla y el horno abren el menú del juego.");
			UI::SeparatorText("La cocina");
			Changed(UI::SliderFloat("Inclinación", &v.tiltDeg, 30.0f, 80.0f, "%.0f grados"));
			Hint("Cuánto se mira desde arriba.");
			Changed(UI::SliderFloat("Zoom", &v.zoom, 0.6f, 1.6f, "%.2f"));
			{
				// the knife, from every one the game has (for trying them)
				const auto&  knives = Kit::Knives();
				const auto&  now = Kit::Get(Kit::Role::Knife);
				const std::string shown = now.vanilla ? now.path : std::string("El de Hearthcooking");
				if (UI::BeginCombo("Cuchillo", shown.c_str())) {
					if (UI::Selectable("El que encuentre automáticamente", false)) {
						Kit::SetMesh(Kit::Role::Knife, "");
					}
					if (UI::Selectable("El de Hearthcooking", !now.vanilla)) {
						Kit::SetMesh(Kit::Role::Knife, "ours");
					}
					for (const auto& k : knives) {
						if (UI::Selectable(k.c_str(), now.vanilla && _stricmp(now.path.c_str(), k.c_str()) == 0)) {
							Kit::SetMesh(Kit::Role::Knife, k);
						}
					}
					UI::EndCombo();
				}
				Hint("Se aplica la próxima vez que se abra la cocina; se guarda en Hearthcooking.ini.");
			}
			UI::SeparatorText("Gráficos");
			{
				static const char* kQ[] = { "Baja (más rápida)", "Media", "Alta" };
				if (UI::BeginCombo("Calidad de la cocina", kQ[std::clamp(v.quality, 0, 2)])) {
					for (int q = 2; q >= 0; --q) {
						if (UI::Selectable(kQ[q], v.quality == q)) {
							v.quality = q;
							Changed(true);
						}
					}
					UI::EndCombo();
				}
				Hint("Alta: suavizado 4x y sombras nítidas. Media: 2x. Baja: sin suavizado, para tarjetas más viejas. Las sombras cambian la próxima vez que inicies el juego.");
			}
			UI::SeparatorText("Controles y sonido");
			Changed(UI::SliderFloat("Velocidad del cursor", &v.mouseSpeed, 0.2f, 3.0f, "%.2f"));
			Changed(UI::SliderFloat("Tamaño de la interfaz", &v.uiScale, 0.6f, 1.6f, "%.2f"));
			Changed(UI::Checkbox("Sonido", &v.sound));
			Changed(UI::SliderFloat("Sonidos de la cocina", &v.volume, 0.0f, 2.0f, "%.2f"));
			Changed(UI::SliderFloat("Fuego y hervor", &v.ambience, 0.0f, 2.0f, "%.2f"));
			Changed(UI::SliderFloat("Clics del menú", &v.effectsVolume, 0.0f, 2.0f, "%.2f"));
			if (UI::CollapsingHeader("Cada sonido por separado")) {
				Hint("1 es el volumen original y 0 lo apaga. Se suma a los controles de arriba.");
				for (std::size_t k = 0; k < std::size(Config::kSoundNames); ++k) {
					const std::string name = Config::kSoundNames[k];
					float lv = v.soundLevels.count(name) ? v.soundLevels[name] : 1.0f;
					if (UI::SliderFloat(Config::kSoundLabels[k], &lv, 0.0f, 2.0f, "%.2f")) {
						v.soundLevels[name] = lv;
						g_dirty = true;
					}
				}
			}
			Changed(UI::Checkbox("Mostrar el siguiente paso", &v.hints));
			{
				const char* styles[] = { "Xbox", "PlayStation" };
				if (UI::BeginCombo("Botones del control", styles[v.padStyle == 1 ? 1 : 0])) {
					for (int k = 0; k < 2; ++k) {
						if (UI::Selectable(styles[k], v.padStyle == k)) {
							v.padStyle = k;
							g_dirty = true;
						}
					}
					UI::EndCombo();
				}
			}
			Changed(UI::Checkbox("Depuración: marcar lo que se puede tomar", &v.debugOverlay));
			UI::Spacing();
			UI::Separator();
			if (UI::Button("Guardar en Hearthcooking.ini")) {
				Config::Save();
				g_dirty = false;
			}
			UI::SameLine();
			if (UI::Button("Deshacer cambios")) {
				Config::Load();
				g_dirty = false;
			}
			if (g_dirty) {
				UI::SameLine();
				UI::TextDisabled("(sin guardar: los cambios duran hasta que salgas del juego)");
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			spdlog::info("SKSE Menu Framework not installed: settings are in Hearthcooking.ini");
			return;
		}
		SKSEMenuFramework::SetSection("Hearthcooking");
		SKSEMenuFramework::AddSectionItem("Opciones", Settings);
		spdlog::info("Settings page added to SKSE Menu Framework");
	}
}
