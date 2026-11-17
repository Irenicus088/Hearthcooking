# Hearthcooking

Cook by hand in Skyrim SE, AE and GOG. Sit down at a cooking pot or a Hearthfire oven and a recipe book opens over
the game's own crafting menu:

- **Quick Cook** hands you back to the game's menu. Its dishes are always **Plain** (the game's own).
- **Cook by Hand** opens a kitchen drawn over the paused game, where you make the dish yourself. It comes out
  **Burnt**, **Plain**, **Hearty** or **Masterwork**, and only cooking by hand gets you past Plain.

Every cooking recipe in your game is used as it is (the game's own and any mod's), with the same ingredients and
conditions. Each one is cooked one of three ways:

| Station | Recipes | How |
| --- | --- | --- |
| Cooking pot | soups and stews | Chop on the board (small, even pieces), tip the board into the pot, keep it at a simmer, stir so it doesn't catch, salt it, ladle it into the bowl |
| Spit (cooking pot) | roasts, steaks, grilled things | Put the meat on the spit, turn the crank so every side browns evenly, salt it, take it off onto the plate |
| Hearthfire oven | breads, pies, sweet rolls | Add the dough's ingredients, knead with long, steady strokes, fill and crimp (pies), bake with the door shut, take it out golden |

There is no heat gauge: you read the fire from its flames, the pot from its bubbles and steam, and the bake by
opening the oven door (which lets the heat out). Logs from the pile make the fire hotter, burn down over about two
minutes, and can be pulled back out.

## The kitchen's things

The kitchen uses the game's own meshes wherever your load order has them: the table, the cooking pot, the ladle, the
knife, wooden bowls and plates, firewood, the salt pile, the campfire stones, the cooking spit and the Hearthfire oven
(found among your game's furniture), and clutter set about (jugs, baskets, sacks, cheese, bread). They're found when
the game starts, by item name and mesh file name, and fitted into the space the kitchen gives each one. Your own food
is always drawn from its own meshes. Anything not found uses Hearthcooking's own mesh. `Hearthcooking.log` lists what
was picked; `[Meshes]` in the INI sets any of them by hand, or turns one that faces the wrong way.

## The tiers

| Tier | Effects | Price |
| --- | --- | --- |
| Burnt | half as strong, half as long; eating it costs 20 stamina | a quarter |
| Plain | the game's own dish | as the game's |
| Hearty | 1.5x as strong and as long | 1.5x |
| Masterwork | twice as strong and as long | 2.5x |

`Hearthcooking.esp` holds 300 empty dishes. The first time you cook a dish to Burnt, Hearty or Masterwork, one is made
into a copy of the game's dish (name, mesh, effects, keywords) and the SKSE co-save remembers which is which, so they
stay what they are across saves, and merchants buy them as food.

## How it's built

- `src/sim/Kitchen` - the rules, plain C++ with no game code: the fire and its logs, cutting, the pieces sliding off
  the board into the pot (a small physics of their own), the pot, the spit, the oven, and the grading. Tested by
  `tests/kitchen_test.cpp`, which plays careful and careless cooks by script.
- `src/plugin` - the SKSE plugin. `Cook` runs the kitchen and its interface, `Recipes` reads the game's recipes and makes
  the tiered dishes, `Scene` and `Render3D` draw the kitchen (our own meshes, and the food's own meshes from your game)
  with Skyrim Yacht's renderer, extended with browning, cut planes, fitting meshes into a space, and fire, steam and salt
  sprites. `Kit` finds the game's meshes for the kitchen's things.
- `tools/make_meshes.py` makes Hearthcooking's own meshes and textures, the fallbacks (nothing of Bethesda's ships), `tools/make_sounds.py`
  synthesizes the sounds, `tools/make_esp.py` writes the plugin. `tools/preview_kitchen.py` draws the layout to a PNG.

Built by CI on Windows: every push compiles and runs the tests; running the workflow by hand publishes the package to
the `dist` branch.

```
g++ -std=c++20 -O2 -o build/kitchen_test tests/kitchen_test.cpp src/sim/Kitchen.cpp && ./build/kitchen_test
```

Requires SKSE64, Address Library for SKSE Plugins. SKSE Menu Framework is optional (settings page); everything is also
in `Hearthcooking.ini`.

Built for the latest Skyrim, 1.7.104 with SKSE 2.3.x (Address Library format 5: the DLL declares AddressLibraryV5 and no
struct use, as SKSE 2.3 asks), and still one DLL for SE 1.5.97 and AE 1.6.x on Steam and GOG. Install the Address Library
that matches your game. `Hearthcooking.esp` is
ESL-flagged with a 1.70 header and its records in 0x800-0xFFF, so it loads as a light plugin on all of them without
Backported Extended ESL Support. Not for Skyrim VR.

## Credits

- Interface fonts: Barlow Condensed, IM Fell English, Fondamento (SIL Open Font License, see `assets/fonts`).
- Settings page: SKSE Menu Framework header (MIT).
- Sounds: "Egg crack" by OpenWindow_2023_Group_14 (Liora Caplan), freesound.org/s/708312, CC BY 4.0, trimmed and
  converted. Campfire loops by HECKFRICKER (freesound.org/s/729395, /729396, CC0). Cutting board and log boiler
  recordings from BigSoundBank.com (CC0). Pot boiling by TinyWorlds (OpenGameArt, CC0). Firewood: "Wood Logs 02" from SoundJay.com.
- The renderer, overlay and input plumbing come from Skyrim Yacht.

GPL-3.0 (LICENSE.txt).
