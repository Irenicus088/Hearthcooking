// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Render3D.h"
#include "Overlay.h"

#include "Config.h"
#include "Cook.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_dx11.h>

namespace Overlay
{
	namespace
	{
		struct InputMsg
		{
			enum class Kind
			{
				MouseMove,
				MouseButton,
				Wheel,
				Key
			} kind;
			float x = 0.0f, y = 0.0f;
			int   button = 0;
			bool  down = false;
		};
		std::mutex            g_inputMtx;
		std::vector<InputMsg> g_input;   // for ImGui (drained in Present)
		std::vector<Press>    g_presses;  // for the kitchen
		std::vector<int>      g_keys;

		std::atomic<bool>  g_open{ false };
		std::atomic<bool>  g_inputDisabled{ false };
		std::atomic<bool>  g_uiHover{ false };
		std::atomic<bool>  g_held[3]{};
		// a controller: the left stick moves the pointer (the right one finely), A is the left button
		std::atomic<float> g_stickL[2]{}, g_stickR[2]{};
		std::atomic<bool>  g_padHeld{ false }, g_usingPad{ false };
		// stirring or turning with the right stick: the pointer goes round a centre on the screen, where the stick points
		std::atomic<bool>  g_orbit{ false };
		std::atomic<float> g_orbitC[2]{}, g_orbitR{ 0.0f };
		std::atomic<bool>  g_ltHeld{ false };
		std::atomic<float> g_mx{ 0.0f }, g_my{ 0.0f };
		// the game's own pointer (vanilla or a cursor mod's) is the one shown: ours follows it while the mouse is in use
		std::atomic<bool> g_gameCur{ false };
		float             g_lastGame[2]{ -1.0f, -1.0f };
		bool              g_drewInMenu = false;  // drawn under the game's cursor this frame (from the menu), not at Present
		std::atomic<float> g_screenW{ 1920.0f }, g_screenH{ 1080.0f };
		std::atomic<bool>  g_center{ true };
		HWND               g_hwnd = nullptr;

		bool                 g_imguiReady = false;
		bool                 g_imguiFailed = false;
		IDXGISwapChain*      g_sc = nullptr;
		ID3D11Device*        g_dev = nullptr;
		ID3D11DeviceContext* g_ctx = nullptr;
		std::chrono::steady_clock::time_point g_lastFrame{};

		void Push(const InputMsg& a_m)
		{
			std::lock_guard lk(g_inputMtx);
			if (g_input.size() < 1024) {
				g_input.push_back(a_m);
			}
		}

		void HandleInput(RE::InputEvent* const* a_event)
		{
			if (!a_event || !g_open) {
				return;
			}
			for (auto* e = *a_event; e; e = e->next) {
				if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
					auto*       mm = static_cast<RE::MouseMoveEvent*>(e);
					const float s = Config::Get().mouseSpeed;
					g_usingPad = false;
					if (g_gameCur.load()) {
						continue;  // the game moves its cursor; the frame picks up where it went
					}
					// the pointer itself lives here, so the kitchen and ImGui agree on where it is
					g_mx = std::clamp(g_mx.load() + mm->mouseInputX * s, 0.0f, g_screenW.load() - 1.0f);
					g_my = std::clamp(g_my.load() + mm->mouseInputY * s, 0.0f, g_screenH.load() - 1.0f);
					Push({ InputMsg::Kind::MouseMove, g_mx.load(), g_my.load() });
					g_usingPad = false;
					continue;
				}
				if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
					auto* ts = static_cast<RE::ThumbstickEvent*>(e);
					auto& st = ts->IsLeft() ? g_stickL : g_stickR;
					st[0] = ts->xValue;
					st[1] = ts->yValue;
					if (std::abs(ts->xValue) + std::abs(ts->yValue) > 0.2f) {
						g_usingPad = true;
					}
					continue;
				}
				auto* btn = e->AsButtonEvent();
				if (!btn) {
					continue;
				}
				const auto code = btn->GetIDCode();
				const bool down = btn->IsDown();
				const bool up = btn->IsUp();
				if (!down && !up) {
					continue;
				}
				if (e->GetDevice() == RE::INPUT_DEVICE::kMouse) {
					using MouseKey = RE::BSWin32MouseDevice::Key;
					if (code == MouseKey::kWheelUp || code == MouseKey::kWheelDown) {
						if (down) {
							Push({ InputMsg::Kind::Wheel, 0.0f, code == MouseKey::kWheelUp ? 1.0f : -1.0f });
						}
					} else if (code <= 2) {
						g_held[code] = down;
						Push({ InputMsg::Kind::MouseButton, g_mx.load(), g_my.load(), static_cast<int>(code), down });
						std::lock_guard lk(g_inputMtx);
						g_presses.push_back({ g_mx.load(), g_my.load(), static_cast<int>(code), down, down && g_uiHover.load() });
					}
				} else if (e->GetDevice() == RE::INPUT_DEVICE::kKeyboard && down) {
					std::lock_guard lk(g_inputMtx);
					g_keys.push_back(static_cast<int>(code));
				} else if (e->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
					using Pad = RE::BSWin32GamepadDevice::Key;
					g_usingPad = true;
					if (code == Pad::kLeftTrigger) {  // the right mouse button: put back what's held
						g_held[1] = down;
						g_ltHeld = down;
						Push({ InputMsg::Kind::MouseButton, g_mx.load(), g_my.load(), 1, down });
						std::lock_guard lk(g_inputMtx);
						g_presses.push_back({ g_mx.load(), g_my.load(), 1, down, down && g_uiHover.load() });
					} else if (code == Pad::kA || code == Pad::kRightTrigger) {  // the left mouse button
						g_held[0] = down;
						g_padHeld = down;
						Push({ InputMsg::Kind::MouseButton, g_mx.load(), g_my.load(), 0, down });
						std::lock_guard lk(g_inputMtx);
						g_presses.push_back({ g_mx.load(), g_my.load(), 0, down, down && g_uiHover.load() });
					} else if (down) {
						// the keyboard's keys: B / Start Esc, Y Tab (put it down / leave), X help (H), Back the sound panel
						int key = 0;
						if (code == Pad::kB || code == Pad::kStart) {
							key = 0x01;
						} else if (code == Pad::kY) {
							key = 0x0F;
						} else if (code == Pad::kX) {
							key = 0x23;
						} else if (code == Pad::kBack) {
							key = kPadSoundKey;
						} else if (code == Pad::kLeftShoulder) {
							key = 0x31;  // N: the next knife
						} else if (code == Pad::kRightShoulder) {
							key = 0x13;  // R: the knife turned to cut along, or back across
						} else if (code == Pad::kUp) {
							key = 0xC8;  // the recipe book's list
						} else if (code == Pad::kDown) {
							key = 0xD0;
						}
						if (key) {
							std::lock_guard lk(g_inputMtx);
							g_keys.push_back(key);
						}
					}
				}
				if (Config::Get().logInput) {
					spdlog::info("[input] device {} code {:X} {}", static_cast<int>(e->GetDevice()), code, down ? "down" : "up");
				}
			}
		}

		void SafeHandleInput(RE::InputEvent* const* a_event)
		{
			__try {
				HandleInput(a_event);
			} __except (SehFilter(GetExceptionInformation(), "HandleInput")) {
				g_inputDisabled = true;
			}
		}

		// Hook on the call that hands each frame's input to the game and to every other mod: while the kitchen is open
		// nothing gets through, so the mouse can't turn the camera and keys can't open other menus.
		using DispatchFn = void (*)(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
		DispatchFn g_origDispatch = nullptr;

		// When the hook last carried real input: if it never does (another game version where that call is something
		// else), the plain event sink reads the input instead, so the pointer still moves.
		std::atomic<std::int64_t> g_hookInputAt{ 0 };
		std::int64_t              NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		void DispatchHook(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
		{
			// only the game's input manager sending its input: in a game version where this call is something else,
			// pass it straight on and leave the input to the event sink
			static auto* s_mgr = static_cast<RE::BSTEventSource<RE::InputEvent*>*>(RE::BSInputDeviceManager::GetSingleton());
			if (a_dispatcher != s_mgr) {
				static bool s_warned = false;
				if (!s_warned) {
					s_warned = true;
					spdlog::warn("Input hook: this call isn't the input manager's in this game version; using the event sink");
				}
				g_origDispatch(a_dispatcher, a_events);
				return;
			}
			if (a_events && *a_events) {
				g_hookInputAt = NowMs();
			}
			if (!g_inputDisabled) {
				SafeHandleInput(a_events);
			}
			if (g_open) {
				// only the mouse's movement gets through, so the game's own cursor (the one shown) moves; clicks, keys
				// and the controller stay ours. The events are relinked for the call and put back as they were.
				std::array<std::pair<RE::InputEvent*, RE::InputEvent*>, 16> saved{};
				std::size_t                                                 n = 0;
				RE::InputEvent*                                             first = nullptr;
				RE::InputEvent*                                             last = nullptr;
				for (auto* e = a_events ? *a_events : nullptr; e && n < saved.size(); e = e->next) {
					saved[n++] = { e, e->next };
				}
				for (std::size_t i = 0; i < n; ++i) {
					auto* e = saved[i].first;
					if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
						(last ? last->next : first) = e;
						last = e;
					}
				}
				if (last) {
					last->next = nullptr;
				}
				static RE::InputEvent* s_list[1] = { nullptr };
				s_list[0] = first;
				g_origDispatch(a_dispatcher, s_list);
				for (std::size_t i = 0; i < n; ++i) {
					saved[i].first->next = saved[i].second;
				}
				return;
			}
			g_origDispatch(a_dispatcher, a_events);
		}

		bool InstallDispatchHook()
		{
			// BSInputDeviceManager: the SendEvent call inside the input polling function (SE 67315, AE 68617)
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(67315, 68617), 0x7B };
			const auto*                     op = reinterpret_cast<const std::uint8_t*>(target.address());
			if (*op != 0xE8) {
				spdlog::warn("Input hook: expected a call at {:X}, found {:02X}", target.address(), *op);
				return false;
			}
			g_origDispatch = reinterpret_cast<DispatchFn>(SKSE::GetTrampoline().write_call<5>(target.address(), &DispatchHook));
			return g_origDispatch != nullptr;
		}

		bool SafeInstallDispatchHook()
		{
			__try {
				return InstallDispatchHook();
			} __except (SehFilter(GetExceptionInformation(), "InstallDispatchHook")) {
				return false;
			}
		}

		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				// the hook reads the input when it works; this is the fallback
				if (!g_inputDisabled && NowMs() - g_hookInputAt.load() > 500) {
					SafeHandleInput(a_event);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void DrainInput()
		{
			std::vector<InputMsg> msgs;
			{
				std::lock_guard lk(g_inputMtx);
				msgs.swap(g_input);
			}
			auto& io = ImGui::GetIO();
			for (const auto& m : msgs) {
				switch (m.kind) {
				case InputMsg::Kind::MouseMove:
					io.AddMousePosEvent(m.x, m.y);
					break;
				case InputMsg::Kind::MouseButton:
					io.AddMousePosEvent(m.x, m.y);
					io.AddMouseButtonEvent(m.button, m.down);
					break;
				case InputMsg::Kind::Wheel:
					io.AddMouseWheelEvent(0.0f, m.y);
					break;
				default:
					break;
				}
			}
			// a release the game never saw (alt-tab, the Steam overlay): let go
			static constexpr int kVk[3] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
			for (int b = 0; b < 3; ++b) {
				if (io.MouseDown[b] && !(b == 0 && g_padHeld) && (::GetAsyncKeyState(kVk[b]) & 0x8000) == 0) {
					io.AddMouseButtonEvent(b, false);
					g_held[b] = false;
				}
			}
		}

		bool InitImGui()
		{
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			Cook::SetupUi(g_screenH.load());
			return ImGui_ImplDX11_Init(g_dev, g_ctx);
		}

		// The game's cursor: where it is (in back-buffer pixels), if it's up and showing
		bool ReadGameCursor(float a_w, float a_h, float& a_x, float& a_y)
		{
			auto* ui = RE::UI::GetSingleton();
			auto* mc = RE::MenuCursor::GetSingleton();
			if (!ui || !mc) {
				return false;
			}
			auto m = ui->GetMenu(RE::CursorMenu::MENU_NAME);
			if (!m || !m->uiMovie || !m->uiMovie->GetVisible() || !ui->IsMenuOpen(RE::CursorMenu::MENU_NAME)) {
				return false;
			}
			const auto& d = mc->GetRuntimeData();
			const float sw = d.screenWidthX > 1.0f ? d.screenWidthX : a_w;
			const float sh = d.screenWidthY > 1.0f ? d.screenWidthY : a_h;
			static bool s_logged = false;
			if (!s_logged) {
				s_logged = true;
				spdlog::info("Game cursor at {:.0f},{:.0f} of {:.0f}x{:.0f} (shown {}), back buffer {:.0f}x{:.0f}", d.cursorPosX, d.cursorPosY, d.screenWidthX,
					d.screenWidthY, d.showCursorCount, a_w, a_h);
			}
			a_x = std::clamp(d.cursorPosX / sw * a_w, 0.0f, a_w - 1.0f);
			a_y = std::clamp(d.cursorPosY / sh * a_h, 0.0f, a_h - 1.0f);
			return true;
		}

		void WriteGameCursor(float a_w, float a_h, float a_x, float a_y)
		{
			if (auto* mc = RE::MenuCursor::GetSingleton()) {
				auto&       d = mc->GetRuntimeData();
				const float sw = d.screenWidthX > 1.0f ? d.screenWidthX : a_w;
				const float sh = d.screenWidthY > 1.0f ? d.screenWidthY : a_h;
				d.cursorPosX = a_x / a_w * sw;
				d.cursorPosY = a_y / a_h * sh;
			}
		}

		// The device state, kept across our drawing when we draw in the middle of the game's interface
		struct StateBackup
		{
			static constexpr UINT kSrv = 16, kCb = 14, kSmp = 16, kVb = 4;
			ID3D11RenderTargetView*   rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			ID3D11DepthStencilView*   dsv = nullptr;
			UINT                      nVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, nSc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			D3D11_VIEWPORT            vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			D3D11_RECT                sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			ID3D11RasterizerState*    rs = nullptr;
			ID3D11BlendState*         bs = nullptr;
			FLOAT                     bf[4]{};
			UINT                      mask = 0;
			ID3D11DepthStencilState*  ds = nullptr;
			UINT                      sref = 0;
			ID3D11ShaderResourceView* psSrv[kSrv]{}, *vsSrv[kSrv]{};
			ID3D11SamplerState*       psSmp[kSmp]{}, *vsSmp[kSmp]{};
			ID3D11Buffer*             psCb[kCb]{}, *vsCb[kCb]{};
			ID3D11PixelShader*        ps = nullptr;
			ID3D11VertexShader*       vs = nullptr;
			ID3D11GeometryShader*     gs = nullptr;
			ID3D11HullShader*         hs = nullptr;
			ID3D11DomainShader*       dsh = nullptr;
			D3D11_PRIMITIVE_TOPOLOGY  topo{};
			ID3D11InputLayout*        il = nullptr;
			ID3D11Buffer*             vb[kVb]{};
			UINT                      stride[kVb]{}, offset[kVb]{};
			ID3D11Buffer*             ib = nullptr;
			DXGI_FORMAT               ibFmt{};
			UINT                      ibOff = 0;

			void Save(ID3D11DeviceContext* c)
			{
				c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
				c->RSGetViewports(&nVp, vp);
				c->RSGetScissorRects(&nSc, sc);
				c->RSGetState(&rs);
				c->OMGetBlendState(&bs, bf, &mask);
				c->OMGetDepthStencilState(&ds, &sref);
				c->PSGetShaderResources(0, kSrv, psSrv);
				c->VSGetShaderResources(0, kSrv, vsSrv);
				c->PSGetSamplers(0, kSmp, psSmp);
				c->VSGetSamplers(0, kSmp, vsSmp);
				c->PSGetConstantBuffers(0, kCb, psCb);
				c->VSGetConstantBuffers(0, kCb, vsCb);
				c->PSGetShader(&ps, nullptr, nullptr);
				c->VSGetShader(&vs, nullptr, nullptr);
				c->GSGetShader(&gs, nullptr, nullptr);
				c->HSGetShader(&hs, nullptr, nullptr);
				c->DSGetShader(&dsh, nullptr, nullptr);
				c->IAGetPrimitiveTopology(&topo);
				c->IAGetInputLayout(&il);
				c->IAGetVertexBuffers(0, kVb, vb, stride, offset);
				c->IAGetIndexBuffer(&ib, &ibFmt, &ibOff);
			}

			template <class T>
			static void Rel(T*& a_p)
			{
				if (a_p) {
					a_p->Release();
					a_p = nullptr;
				}
			}

			void Restore(ID3D11DeviceContext* c)
			{
				c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
				c->RSSetViewports(nVp, vp);
				c->RSSetScissorRects(nSc, sc);
				c->RSSetState(rs);
				c->OMSetBlendState(bs, bf, mask);
				c->OMSetDepthStencilState(ds, sref);
				c->PSSetShaderResources(0, kSrv, psSrv);
				c->VSSetShaderResources(0, kSrv, vsSrv);
				c->PSSetSamplers(0, kSmp, psSmp);
				c->VSSetSamplers(0, kSmp, vsSmp);
				c->PSSetConstantBuffers(0, kCb, psCb);
				c->VSSetConstantBuffers(0, kCb, vsCb);
				c->PSSetShader(ps, nullptr, 0);
				c->VSSetShader(vs, nullptr, 0);
				c->GSSetShader(gs, nullptr, 0);
				c->HSSetShader(hs, nullptr, 0);
				c->DSSetShader(dsh, nullptr, 0);
				c->IASetPrimitiveTopology(topo);
				c->IASetInputLayout(il);
				c->IASetVertexBuffers(0, kVb, vb, stride, offset);
				c->IASetIndexBuffer(ib, ibFmt, ibOff);
				for (auto*& p : rtv) Rel(p);
				Rel(dsv);
				Rel(rs);
				Rel(bs);
				Rel(ds);
				for (auto*& p : psSrv) Rel(p);
				for (auto*& p : vsSrv) Rel(p);
				for (auto*& p : psSmp) Rel(p);
				for (auto*& p : vsSmp) Rel(p);
				for (auto*& p : psCb) Rel(p);
				for (auto*& p : vsCb) Rel(p);
				Rel(ps);
				Rel(vs);
				Rel(gs);
				Rel(hs);
				Rel(dsh);
				Rel(il);
				for (auto*& p : vb) Rel(p);
				Rel(ib);
			}
		};

		void Frame();
	}

	void* CreateTexture(int a_w, int a_h, const void* a_rgba)
	{
		if (!g_dev || a_w <= 0 || a_h <= 0 || !a_rgba) {
			return nullptr;
		}
		D3D11_TEXTURE2D_DESC td{};
		td.Width = static_cast<UINT>(a_w);
		td.Height = static_cast<UINT>(a_h);
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA sd{ a_rgba, static_cast<UINT>(a_w * 4), 0 };
		ID3D11Texture2D* tex = nullptr;
		if (FAILED(g_dev->CreateTexture2D(&td, &sd, &tex)) || !tex) {
			return nullptr;
		}
		ID3D11ShaderResourceView* srv = nullptr;
		g_dev->CreateShaderResourceView(tex, nullptr, &srv);
		tex->Release();
		return srv;
	}

	void SetOpen(bool a_open)
	{
		if (g_open == a_open) {
			return;
		}
		g_open = a_open;
		for (auto& h : g_held) {
			h = false;
		}
		{
			std::lock_guard lk(g_inputMtx);
			g_input.clear();
			g_presses.clear();
			g_keys.clear();
		}
		if (a_open) {
			g_center = true;
		}
	}

	bool IsOpen() { return g_open.load(); }

	void MousePos(float& a_x, float& a_y)
	{
		a_x = g_mx.load();
		a_y = g_my.load();
	}

	std::vector<Press> TakePresses()
	{
		std::lock_guard    lk(g_inputMtx);
		std::vector<Press> out;
		out.swap(g_presses);
		return out;
	}

	std::vector<int> TakeKeys()
	{
		std::lock_guard  lk(g_inputMtx);
		std::vector<int> out;
		out.swap(g_keys);
		return out;
	}

	bool UsingPad() { return g_usingPad; }
	void SetStickOrbit(bool a_on, float a_cx, float a_cy, float a_r)
	{
		g_orbitC[0] = a_cx;
		g_orbitC[1] = a_cy;
		g_orbitR = a_r;
		g_orbit = a_on;
	}

	void ScreenSize(float& a_w, float& a_h)
	{
		a_w = g_screenW.load();
		a_h = g_screenH.load();
	}

	bool ButtonHeld(int a_button) { return a_button >= 0 && a_button < 3 && g_held[a_button].load(); }

	std::string ToUtf8(const std::wstring& a_w)
	{
		if (a_w.empty()) {
			return {};
		}
		const int   n = ::WideCharToMultiByte(CP_UTF8, 0, a_w.c_str(), static_cast<int>(a_w.size()), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, a_w.c_str(), static_cast<int>(a_w.size()), s.data(), n, nullptr, nullptr);
		return s;
	}

	int SehFilter(EXCEPTION_POINTERS* a_info, const char* a_where)
	{
		const auto* rec = a_info ? a_info->ExceptionRecord : nullptr;
		const auto  addr = rec ? reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress) : 0;
		HMODULE     mod = nullptr;
		wchar_t     modPath[MAX_PATH]{};
		if (addr && ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						reinterpret_cast<LPCWSTR>(addr), &mod)) {
			::GetModuleFileNameW(mod, modPath, MAX_PATH);
		}
		const auto offset = mod ? addr - reinterpret_cast<std::uintptr_t>(mod) : addr;
		spdlog::critical("CRASH CAUGHT in {}: exception 0x{:08X} at {}+0x{:X} - this part is now switched off so the game keeps running",
			a_where, rec ? static_cast<std::uint32_t>(rec->ExceptionCode) : 0u, ToUtf8(std::filesystem::path(modPath).filename().wstring()), offset);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	bool InstallInputSink()
	{
		// The hook (on the game's own input call) reads the input and keeps it from the game while the kitchen is open.
		// The event sink is always there too: if the hook's call turns out to be something else in this game version,
		// the sink reads the input instead, so the kitchen still works.
		if (SafeInstallDispatchHook()) {
			spdlog::info("Input hook installed: the game and other mods get no input while the kitchen is open");
		} else {
			spdlog::warn("Input hook not installed: reading input through an event sink");
		}
		static InputSink sink;
		auto*            mgr = RE::BSInputDeviceManager::GetSingleton();
		if (!mgr) {
			return false;
		}
		mgr->AddEventSink<RE::InputEvent*>(&sink);
		return true;
	}

	void OnPresent(IDXGISwapChain* sc)
	{
		if (!g_sc) {
			DXGI_SWAP_CHAIN_DESC desc{};
			if (FAILED(sc->GetDesc(&desc)) || !desc.OutputWindow) {
				return;
			}
			if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_dev))) || !g_dev) {
				return;
			}
			g_dev->GetImmediateContext(&g_ctx);
			g_sc = sc;
			g_hwnd = desc.OutputWindow;
			g_screenW = static_cast<float>(desc.BufferDesc.Width);
			g_screenH = static_cast<float>(desc.BufferDesc.Height);
			spdlog::info("Attached to the game's swap chain ({}x{})", desc.BufferDesc.Width, desc.BufferDesc.Height);
		}
		if (sc != g_sc) {
			return;
		}
		if (g_drewInMenu) {
			g_drewInMenu = false;  // already drawn this frame, under the game's cursor
			return;
		}
		Frame();
	}

	bool GameCursorShown() { return g_gameCur.load(); }

	void OnMenuDisplay()
	{
		// drawn from our menu, in the middle of the game's interface, so the menus above ours (the game's cursor) draw
		// over the kitchen; only when the interface is drawing straight into the back buffer, else at Present as before
		if (!g_sc || !g_ctx || !g_open || g_imguiFailed) {
			return;
		}
		ID3D11RenderTargetView* bound = nullptr;
		g_ctx->OMGetRenderTargets(1, &bound, nullptr);
		if (!bound) {
			return;
		}
		ID3D11Resource*  res = nullptr;
		ID3D11Texture2D* back = nullptr;
		bound->GetResource(&res);
		bound->Release();
		g_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
		const bool same = res && back && res == back;
		if (res) {
			res->Release();
		}
		if (back) {
			back->Release();
		}
		static int s_logged = -1;
		if (s_logged != static_cast<int>(same)) {
			s_logged = static_cast<int>(same);
			spdlog::info(same ? "Drawing the kitchen inside the interface (under the game's cursor)" :
			                    "The interface draws off the back buffer: the kitchen is drawn at Present");
		}
		if (!same) {
			return;
		}
		StateBackup st;
		st.Save(g_ctx);
		Frame();
		st.Restore(g_ctx);
		g_drewInMenu = true;
	}

	namespace
	{
	void Frame()
	{
		IDXGISwapChain* sc = g_sc;
		if (!sc || !g_open) {
			return;
		}
		if (!g_imguiReady && !g_imguiFailed) {
			if (!InitImGui() || !ImGui_ImplDX11_CreateDeviceObjects()) {
				g_imguiFailed = true;
				spdlog::error("ImGui failed to initialise");
				return;
			}
			g_imguiReady = true;
		}
		if (g_imguiFailed) {
			return;
		}

		ID3D11Texture2D* back = nullptr;
		if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) {
			return;
		}
		D3D11_TEXTURE2D_DESC bd{};
		back->GetDesc(&bd);
		g_screenW = static_cast<float>(bd.Width);
		g_screenH = static_cast<float>(bd.Height);

		auto& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(static_cast<float>(bd.Width), static_cast<float>(bd.Height));
		const auto  now = std::chrono::steady_clock::now();
		const float dt = g_lastFrame.time_since_epoch().count() ? std::chrono::duration<float>(now - g_lastFrame).count() : 1.0f / 60.0f;
		g_lastFrame = now;
		io.DeltaTime = std::clamp(dt, 1.0f / 1000.0f, 0.5f);
		// the game's cursor: shown while the mouse is in use, and ours goes where it is (with a controller, ours shows)
		float      gx = 0.0f, gy = 0.0f;
		const bool game = ReadGameCursor(io.DisplaySize.x, io.DisplaySize.y, gx, gy);
		const bool follow = game && !g_usingPad.load();
		if (follow && (!g_gameCur.load() || gx != g_lastGame[0] || gy != g_lastGame[1])) {
			g_mx = gx;
			g_my = gy;
			Push({ InputMsg::Kind::MouseMove, gx, gy });
			io.AddMousePosEvent(gx, gy);
		}
		g_gameCur = follow;
		g_lastGame[0] = gx;
		g_lastGame[1] = gy;
		if (g_center.exchange(false) && !follow) {
			g_mx = io.DisplaySize.x * 0.5f;
			g_my = io.DisplaySize.y * 0.6f;
			io.AddMousePosEvent(g_mx.load(), g_my.load());
		}
		// the sticks move the pointer: faster the further they're pushed
		{
			const float k = 1500.0f * (io.DisplaySize.y / 1080.0f) * Config::Get().mouseSpeed * io.DeltaTime;
			float       dx = 0.0f, dy = 0.0f;
			// stirring, turning: the right stick points where round the centre the pointer goes
			bool orbited = false;
			if (g_orbit.load()) {
				const float x = g_stickR[0], y = g_stickR[1], m = std::sqrt(x * x + y * y);
				if (m > 0.35f) {
					const float r = g_orbitR.load() * (std::min)(1.0f, m);
					g_mx = std::clamp(g_orbitC[0].load() + x / m * r, 0.0f, io.DisplaySize.x - 1.0f);
					g_my = std::clamp(g_orbitC[1].load() - y / m * r, 0.0f, io.DisplaySize.y - 1.0f);
					Push({ InputMsg::Kind::MouseMove, g_mx.load(), g_my.load() });
					orbited = true;
				}
			}
			for (auto* st : { &g_stickL, &g_stickR }) {
				if (orbited && st == &g_stickR) {
					continue;
				}
				const float x = (*st)[0], y = (*st)[1], m = std::sqrt(x * x + y * y);
				if (m > 0.15f) {
					const float g = (m - 0.15f) / 0.85f * (st == &g_stickL ? 1.0f : 0.3f);
					dx += x / m * g * g * k * 1.4f;
					dy -= y / m * g * g * k * 1.4f;
				}
			}
			if (dx != 0.0f || dy != 0.0f) {
				g_mx = std::clamp(g_mx.load() + dx, 0.0f, io.DisplaySize.x - 1.0f);
				g_my = std::clamp(g_my.load() + dy, 0.0f, io.DisplaySize.y - 1.0f);
				Push({ InputMsg::Kind::MouseMove, g_mx.load(), g_my.load() });
			}
			if (game) {
				// the game's cursor kept where ours is, so picking the mouse back up carries on from here
				WriteGameCursor(io.DisplaySize.x, io.DisplaySize.y, g_mx.load(), g_my.load());
				g_lastGame[0] = g_mx.load();
				g_lastGame[1] = g_my.load();
			}
		}
		DrainInput();
		io.MouseDrawCursor = true;

		// the interface size changed in the settings: rebuild the fonts at the new size (once it's held still a moment)
		{
			static float s_applied = Config::Get().uiScale, s_seen = s_applied;
			static auto  s_seenAt = std::chrono::steady_clock::now();
			const float  want = Config::Get().uiScale;
			if (want != s_seen) {
				s_seen = want;
				s_seenAt = now;
			}
			if (want != s_applied && now - s_seenAt > std::chrono::milliseconds(250)) {
				s_applied = want;
				io.Fonts->Clear();
				Cook::SetupUi(g_screenH.load());
				ImGui_ImplDX11_InvalidateDeviceObjects();
				spdlog::info("Interface size {:.2f}: fonts rebuilt", want);
			}
		}
		ImGui_ImplDX11_NewFrame();
		ImGui::NewFrame();
		Cook::DrawUi();
		g_uiHover = io.WantCaptureMouse;
		ImGui::Render();

		Render3D::Draw(sc, g_dev, g_ctx);  // the kitchen, under the interface
		ID3D11RenderTargetView* rtv = nullptr;
		g_dev->CreateRenderTargetView(back, nullptr, &rtv);
		back->Release();
		if (rtv) {
			g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			rtv->Release();
		}
	}
	}
}
