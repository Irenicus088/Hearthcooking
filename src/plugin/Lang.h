#pragma once

// Spanish support (traduccion al espanol neutro).
//  - Canon(): a dish's or an ingredient's name, lowercased, with its Spanish words turned into the English words the
//    kitchen's rules look for ("Pan de patata" -> "potato bread", "Puerro" -> "leek"). English names come out as
//    plain lowercase, as before.
//  - Es(): the Spanish for the kitchen's verdicts, lines and tiers (src/sim keeps them in English: the rules compare them).

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hc
{
	namespace lang_detail
	{
		// Latin-1 letters (U+00C0..U+00FF) without their accents, lowercase
		inline const char* FoldLatin1(unsigned a_cp)
		{
			static const char* const kFold[64] = {
				"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
				"d", "n", "o", "o", "o", "o", "o", "x", "o", "u", "u", "u", "u", "y", "th", "ss",
				"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
				"d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y"
			};
			return a_cp >= 0xC0 && a_cp <= 0xFF ? kFold[a_cp - 0xC0] : nullptr;
		}

		// lowercase, accents off (UTF-8, or a stray Windows-1252 byte)
		inline std::string Fold(std::string_view a_s)
		{
			std::string out;
			out.reserve(a_s.size());
			for (std::size_t i = 0; i < a_s.size(); ++i) {
				const auto c = static_cast<unsigned char>(a_s[i]);
				if (c < 0x80) {
					out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : static_cast<char>(c);
					continue;
				}
				if (c == 0xC3 && i + 1 < a_s.size()) {
					const auto n = static_cast<unsigned char>(a_s[i + 1]);
					if (n >= 0x80 && n <= 0xBF) {
						out += FoldLatin1(0xC0u + (n - 0x80u));
						++i;
						continue;
					}
				}
				if (c >= 0xC0) {
					const bool cont = i + 1 < a_s.size() && (static_cast<unsigned char>(a_s[i + 1]) & 0xC0) == 0x80;
					if (!cont) {
						out += FoldLatin1(c);  // Windows-1252
						continue;
					}
				}
				out += static_cast<char>(c);
			}
			return out;
		}

		inline bool IsWord(char a_c) { return (a_c >= 'a' && a_c <= 'z') || (a_c >= '0' && a_c <= '9'); }

		// several words that mean one thing (checked before the single words)
		inline const std::unordered_map<std::string, std::string>& Phrases()
		{
			static const std::unordered_map<std::string, std::string> k{
				{ "saltador de cenizas", "ash hopper" }, { "saltador", "hopper" }, { "pan entero", "bread" }, { "pan de patata", "potato bread" }, { "pan de papa", "potato bread" },
				{ "pan trenzado", "braided bread" }, { "pan de ajo", "garlic bread" }, { "bollo de canela", "sweet roll" },
				{ "bollo dulce", "sweet roll" }, { "pastel de manzana", "apple pie" }, { "tarta de manzana", "apple pie" },
				{ "pasteles de cangrejo", "crab cakes" }, { "pastel de cangrejo", "crab cake" }, { "cangrejo del barro", "mudcrab" },
				{ "cangrejos del barro", "mudcrab" }, { "pez asesino", "slaughterfish" }, { "peces asesinos", "slaughterfish" },
				{ "boniato de ceniza", "ash yam" }, { "frutos de enebro", "juniper berries" }, { "bayas de enebro", "juniper berries" },
				{ "uvas de jazbay", "jazbay grapes" }, { "azucar lunar", "moon sugar" }, { "col rizada", "kale" },
				{ "a la parrilla", "grilled" }, { "a la brasa", "grilled" }, { "al vapor", "steamed" }, { "sopa de patata", "potato soup" },
				{ "sopa de papa", "potato soup" }, { "pata de", "leg of" }, { "pierna de", "leg of" }, { "muslo de", "leg of" },
				{ "sales de fuego", "fire salts" }, { "sales de hielo", "frost salts" }
			};
			return k;
		}

		inline const std::unordered_map<std::string, std::string>& Words()
		{
			static const std::unordered_map<std::string, std::string> k{
				// meat and fish
				{ "ternera", "beef" }, { "vaca", "beef" }, { "res", "beef" }, { "buey", "beef" }, { "venado", "venison" },
				{ "ciervo", "venison" }, { "mamut", "mammoth" }, { "cabra", "goat" }, { "equino", "horse" }, { "caballo", "horse" },
				{ "pollo", "chicken" }, { "gallina", "chicken" }, { "faisan", "pheasant" }, { "conejo", "rabbit" }, { "liebre", "rabbit" },
				{ "perro", "dog" }, { "jabali", "boar" }, { "cangrejo", "crab" }, { "almeja", "clam" }, { "salmon", "salmon" },
				{ "pez", "fish" }, { "peces", "fish" }, { "pescado", "fish" }, { "trucha", "trout fish" }, { "bacalao", "cod fish" },
				{ "carpa", "carp fish" }, { "lubina", "bass fish" }, { "salvelino", "char fish" }, { "pejesapo", "angler fish" },
				{ "angelote", "angelfish" }, { "carne", "meat" }, { "pata", "leg" }, { "pierna", "leg" }, { "muslo", "leg" },
				{ "pechuga", "breast" }, { "pecho", "breast" }, { "filete", "steak" }, { "chuleta", "chop" }, { "grupa", "haunch" },
				{ "anca", "haunch" }, { "brocheta", "skewer" }, { "pincho", "skewer" }, { "espeton", "spit" }, { "larva", "larva" },
				// vegetables, fruit and herbs
				{ "puerro", "leek" }, { "patata", "potato" }, { "papa", "potato" }, { "col", "cabbage" }, { "repollo", "cabbage" },
				{ "zanahoria", "carrot" }, { "tomate", "tomato" }, { "calabaza", "gourd pumpkin" }, { "calabacin", "gourd" },
				{ "manzana", "apple" }, { "pera", "pear" }, { "ajo", "garlic" }, { "cebolla", "onion" }, { "boniato", "yam" },
				{ "batata", "yam" }, { "name", "yam" }, { "berenjena", "eggplant" }, { "nabo", "turnip" }, { "rabano", "radish" },
				{ "maiz", "corn" }, { "elote", "corn" }, { "raiz", "root" }, { "tallo", "stalk" }, { "lavanda", "lavender" },
				{ "enebro", "juniper" }, { "sinforicarpos", "snowberries" }, { "uva", "grape" }, { "baya", "berries" },
				{ "verdura", "vegetable" }, { "verduras", "vegetable" }, { "hortaliza", "vegetable" },
				// drink and the pantry
				{ "cerveza", "ale" }, { "hidromiel", "mead" }, { "aguamiel", "mead" }, { "vino", "wine" }, { "leche", "milk" },
				{ "miel", "honey" }, { "vinagre", "vinegar" }, { "ron", "rum" }, { "sidra", "cider" }, { "aceite", "oil" },
				{ "jarra", "jug" }, { "botella", "bottle" }, { "zumo", "juice" }, { "jugo", "juice" }, { "caldo", "broth" },
				{ "agua", "water" }, { "harina", "flour" }, { "mantequilla", "butter" }, { "manteca", "lard" }, { "levadura", "yeast" },
				{ "azucar", "sugar" }, { "sal", "salt" }, { "huevo", "egg" }, { "queso", "cheese" }, { "saco", "sack" },
				{ "crema", "creamy bisque" }, { "nata", "cream" },
				// dishes and how they're made
				{ "pan", "bread" }, { "bollo", "dumpling" }, { "empanadilla", "dumpling" }, { "tarta", "crostata" },
				{ "pastel", "pie" }, { "empanada", "pie" }, { "hojaldre", "pastry" }, { "galleta", "cookie" }, { "magdalena", "muffin" },
				{ "sopa", "soup" }, { "guiso", "stew" }, { "estofado", "stew" }, { "cocido", "stew" }, { "salsa", "sauce" },
				{ "cocinado", "cooked" }, { "asado", "roast" }, { "parrilla", "grilled" }, { "carbonizado", "charred" },
				{ "horneado", "baked" }, { "sellado", "seared" }, { "chamuscado", "charred" }
			};
			return k;
		}

		inline const std::string* Word(const std::string& a_w)
		{
			const auto& w = Words();
			if (auto it = w.find(a_w); it != w.end()) {
				return &it->second;
			}
			// plurals and the feminine of a participle: "patatas", "sales", "asadas", "cocinada"
			std::vector<std::string> tries;
			if (a_w.size() > 3 && a_w.ends_with("es")) {
				tries.push_back(a_w.substr(0, a_w.size() - 2));
			}
			if (a_w.size() > 2 && a_w.ends_with("s")) {
				tries.push_back(a_w.substr(0, a_w.size() - 1));
			}
			tries.push_back(a_w);
			for (const auto& t : tries) {
				if (auto it = w.find(t); it != w.end()) {
					return &it->second;
				}
				if (t.size() > 3 && t.ends_with("ada")) {
					if (auto it2 = w.find(t.substr(0, t.size() - 1) + "o"); it2 != w.end()) {
						return &it2->second;
					}
				}
			}
			return nullptr;
		}

		inline std::string Translate(const std::string& a_low)
		{
			struct Span
			{
				std::size_t a, b;
			};
			std::vector<Span> words;
			for (std::size_t i = 0; i < a_low.size();) {
				if (IsWord(a_low[i])) {
					std::size_t j = i;
					while (j < a_low.size() && IsWord(a_low[j])) {
						++j;
					}
					words.push_back({ i, j });
					i = j;
				} else {
					++i;
				}
			}
			std::string out;
			std::size_t at = 0;  // copied up to here
			for (std::size_t k = 0; k < words.size();) {
				out.append(a_low, at, words[k].a - at);
				bool done = false;
				for (std::size_t len = (std::min)(words.size() - k, std::size_t{ 4 }); len >= 2 && !done; --len) {
					std::string key;
					for (std::size_t q = 0; q < len; ++q) {
						key += (q ? " " : "") + a_low.substr(words[k + q].a, words[k + q].b - words[k + q].a);
					}
					if (auto it = Phrases().find(key); it != Phrases().end()) {
						out += it->second;
						at = words[k + len - 1].b;
						k += len;
						done = true;
					}
				}
				if (done) {
					continue;
				}
				const std::string w = a_low.substr(words[k].a, words[k].b - words[k].a);
				const auto* m = Word(w);
				out += m ? *m : w;
				at = words[k].b;
				++k;
			}
			out.append(a_low, at, std::string::npos);
			return out;
		}
	}

	// A name as the kitchen's rules read it: lowercase, Spanish words in English
	inline std::string Canon(std::string_view a_name)
	{
		thread_local std::unordered_map<std::string, std::string> s_seen;
		std::string key(a_name);
		if (auto it = s_seen.find(key); it != s_seen.end()) {
			return it->second;
		}
		auto out = lang_detail::Translate(lang_detail::Fold(a_name));
		if (s_seen.size() > 4096) {
			s_seen.clear();
		}
		s_seen.emplace(std::move(key), out);
		return out;
	}

	// The Spanish for one of the kitchen's own words (anything else comes back as it is)
	inline const char* Es(const char* a_en)
	{
		if (!a_en) {
			return "";
		}
		static const std::unordered_map<std::string_view, const char*> k{
			// tiers
			{ "Burnt", "Quemado" }, { "Plain", "Común" }, { "Hearty", "Abundante" }, { "Masterwork", "Magistral" },
			// what was looked at
			{ "The cuts", "Los cortes" }, { "Into the pot", "A la olla" }, { "The heat", "El calor" }, { "The stirring", "Al revolver" },
			{ "The salt", "La sal" }, { "Done", "Cocción" }, { "All the way round", "Vuelta completa" }, { "The fire", "El fuego" },
			{ "The dough", "La masa" }, { "The filling", "El relleno" }, { "The oven", "El horno" }, { "The bake", "El horneado" },
			// the heat
			{ "Steady all the way", "Constante todo el tiempo" }, { "Ran a little hot", "Un poco demasiado fuerte" },
			{ "Dipped a little cool", "Se enfrió un poco" }, { "Far too hot", "Demasiado fuerte" },
			{ "Too cool for too long", "Muy bajo durante mucho tiempo" },
			// the salt
			{ "Forgot the salt", "Olvidaste la sal" }, { "Could use more salt", "Le falta sal" }, { "Far too salty", "Demasiado salado" },
			{ "A touch salty", "Un poco salado" }, { "Seasoned just right", "Sazonado en su punto" },
			// the pot
			{ "Even and neat", "Uniformes y limpios" }, { "A little uneven", "Algo irregulares" }, { "Ragged", "Muy irregulares" },
			{ "Every piece went in", "Entró todo" }, { "A few went astray", "Se cayeron algunos" },
			{ "Half of it on the hearth", "La mitad fuera de la olla" }, { "Caught on the bottom", "Se pegó al fondo" },
			{ "Never left it", "Sin descuidarlo" }, { "Now and then", "De vez en cuando" }, { "Hardly touched it", "Casi sin revolver" },
			{ "Still raw", "Todavía crudo" }, { "Not quite done", "Le faltó un poco" }, { "Cooked through, just right", "Cocido en su punto" },
			{ "Cooked a bit long", "Algo pasado" }, { "Boiled to mush", "Deshecho de tanto hervir" },
			{ "It caught on the bottom and burnt", "Se pegó al fondo y se quemó" },
			// the spit
			{ "Evenly browned", "Dorado uniforme" }, { "Patchy", "Dorado irregular" }, { "One side black, one side raw", "Un lado negro y el otro crudo" },
			{ "Still bloody", "Todavía sangrante" }, { "Underdone", "Poco hecho" }, { "Done to a turn", "En su punto justo" },
			{ "Overdone", "Demasiado hecho" }, { "One side went black", "Un lado se quemó" },
			{ "Left over the fire too long", "Demasiado tiempo sobre el fuego" },
			// the oven
			{ "Barely worked", "Apenas trabajada" }, { "Underworked", "Poco amasada" }, { "Smooth and springy", "Lisa y elástica" },
			{ "Smooth, worked in fits", "Lisa, pero amasada a tirones" }, { "Overworked and tough", "Demasiado amasada y dura" },
			{ "The edge left open", "El borde quedó abierto" }, { "Full to the edge, well sealed", "Llena hasta el borde y bien sellada" },
			{ "A bit thin in places", "Algo escasa en partes" }, { "Sparse", "Escasa" }, { "Pale and doughy", "Pálido y crudo" },
			{ "A shade underbaked", "Le faltó un poco de horno" }, { "Golden brown", "Bien dorado" }, { "Dark on top", "Oscuro por arriba" },
			{ "Left in the oven too long", "Demasiado tiempo en el horno" }
		};
		if (auto it = k.find(a_en); it != k.end()) {
			return it->second;
		}
		return a_en;
	}

	inline std::string Es(const std::string& a_en) { return Es(a_en.c_str()); }
}
