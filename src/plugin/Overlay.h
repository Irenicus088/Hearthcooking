// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#pragma once

struct IDXGISwapChain;

// The ImGui layer drawn over the game (Present hook), and the mouse and keys while the kitchen is open. The same plumbing
// as Hearthsworn and Hearthframe: input arrives as the game's own events, is queued here, and is handed both to ImGui
// (the recipe book, the buttons) and to the kitchen (the knife, the ladle, the logs).
namespace Overlay
{
	struct Press
	{
		float x = 0.0f, y = 0.0f;
		int   button = 0;
		bool  down = false;
		bool  onUi = false;  // over an ImGui window when it happened: the kitchen ignores it
	};

	void SetOpen(bool a_open);  // the kitchen opened or closed
	bool IsOpen();

	// For the kitchen, on the main thread
	void               MousePos(float& a_x, float& a_y);
	std::vector<Press> TakePresses();
	std::vector<int>   TakeKeys();  // DirectInput scan codes pressed
	bool               ButtonHeld(int a_button);
	bool               UsingPad();
	void               ScreenSize(float& a_w, float& a_h);  // the back buffer's size: the mouse's and the drawing's
	inline constexpr int kPadSoundKey = 0x1000;  // the controller's Back: the sound panel

	// An RGBA texture for ImGui (on the render thread, from DrawUi); null if it couldn't be made
	void* CreateTexture(int a_w, int a_h, const void* a_rgba);

	bool InstallInputSink();
	void OnPresent(IDXGISwapChain* a_swapChain);
	int  SehFilter(EXCEPTION_POINTERS* a_info, const char* a_where);
	std::string ToUtf8(const std::wstring& a_w);
}
