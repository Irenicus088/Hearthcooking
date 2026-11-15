// Hearthcooking - cook by hand in Skyrim Special Edition.
// Copyright (C) 2026 evodube. GPL-3.0 (LICENSE.txt).
#include "Audio.h"
#include "Config.h"

// CommonLib targets Windows 7; XAudio2 2.8+ needs 8 or later in its header. We load the DLL ourselves (see Init).
#pragma push_macro("_WIN32_WINNT")
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <xaudio2.h>
#pragma pop_macro("_WIN32_WINNT")

namespace Audio
{
	namespace
	{
		constexpr UINT32 kRate = 48000;
		constexpr int    kVoices = 24;
		constexpr float  kMaster = 1.6f;

		IXAudio2*               g_xa = nullptr;
		IXAudio2MasteringVoice* g_master = nullptr;
		UINT32                  g_outChannels = 2;
		IXAudio2SourceVoice*    g_pool[kVoices]{};
		bool                    g_tried = false, g_ok = false;
		std::atomic<bool>       g_muted = false;
		std::recursive_mutex    g_mtx;
		std::unordered_map<std::string, std::vector<std::int16_t>> g_bank;

		struct Loop
		{
			IXAudio2SourceVoice* voice = nullptr;
			float                vol = 0.0f;
		};
		std::unordered_map<std::string, Loop> g_loops;

		bool ReadWav(const std::filesystem::path& a_path, std::vector<std::int16_t>& a_out)
		{
			std::ifstream f(a_path, std::ios::binary);
			if (!f) {
				return false;
			}
			std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
				return false;
			}
			std::size_t p = 12;
			while (p + 8 <= b.size()) {
				std::uint32_t len = 0;
				std::memcpy(&len, b.data() + p + 4, 4);
				if (std::memcmp(b.data() + p, "data", 4) == 0) {
					const std::size_t n = (std::min<std::size_t>)(len, b.size() - p - 8) / 2;
					a_out.resize(n);
					std::memcpy(a_out.data(), b.data() + p + 8, n * 2);
					return true;
				}
				p += 8 + len + (len & 1);
			}
			return false;
		}

		WAVEFORMATEX Format()
		{
			WAVEFORMATEX wf{};
			wf.wFormatTag = WAVE_FORMAT_PCM;
			wf.nChannels = 1;
			wf.nSamplesPerSec = kRate;
			wf.wBitsPerSample = 16;
			wf.nBlockAlign = 2;
			wf.nAvgBytesPerSec = kRate * 2;
			return wf;
		}

		void SetPan(IXAudio2SourceVoice* a_v, float a_pan)
		{
			const float a = (std::clamp(a_pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
			float       m[8]{};
			m[0] = std::cos(a);
			if (g_outChannels > 1) {
				m[1] = std::sin(a);
			} else {
				m[0] = 1.0f;
			}
			a_v->SetOutputMatrix(g_master, 1, (std::min)(g_outChannels, 8u), m);
		}

		bool Init()
		{
			if (g_tried) {
				return g_ok;
			}
			g_tried = true;
			const std::filesystem::path dir = "Data\\SKSE\\Plugins\\Hearthcooking\\Sounds";
			std::error_code             ec;
			for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
				if (e.path().extension() == ".wav") {
					std::vector<std::int16_t> pcm;
					if (ReadWav(e.path(), pcm)) {
						g_bank.emplace(e.path().stem().string(), std::move(pcm));
					}
				}
			}
			if (g_bank.empty()) {
				spdlog::warn("Sounds: none found in {} - the kitchen will be silent", dir.string());
				return false;
			}
			::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			// XAudio2 2.9 ships with Windows 10 and 11 (2.8 with Windows 8); loaded by hand so either works
			using create_t = HRESULT(__stdcall*)(IXAudio2**, UINT32, UINT32);
			create_t create = nullptr;
			for (const char* dll : { "xaudio2_9.dll", "xaudio2_8.dll" }) {
				if (HMODULE h = ::LoadLibraryA(dll)) {
					create = reinterpret_cast<create_t>(::GetProcAddress(h, "XAudio2Create"));
					if (create) {
						spdlog::info("Sounds: using {}", dll);
						break;
					}
				}
			}
			if (!create || FAILED(create(&g_xa, 0, 0x00000001)) || !g_xa) {
				spdlog::warn("Sounds: XAudio2 couldn't start - the kitchen will be silent");
				return false;
			}
			if (FAILED(g_xa->CreateMasteringVoice(&g_master)) || !g_master) {
				spdlog::warn("Sounds: no audio device - the kitchen will be silent");
				return false;
			}
			XAUDIO2_VOICE_DETAILS md{};
			g_master->GetVoiceDetails(&md);
			g_outChannels = md.InputChannels;
			const auto wf = Format();
			for (auto& v : g_pool) {
				g_xa->CreateSourceVoice(&v, &wf, XAUDIO2_VOICE_USEFILTER, 2.0f);
			}
			g_ok = true;
			spdlog::info("Sounds: {} loaded, {} output channels", g_bank.size(), g_outChannels);
			return true;
		}

		float Level(const std::string& a_name)
		{
			const auto& cfg = Config::Get();
			if (a_name.rfind("ui_", 0) == 0) {
				return cfg.effectsVolume;
			}
			if (a_name.rfind("loop_", 0) == 0) {
				return cfg.ambience;
			}
			return cfg.volume;
		}
	}

	bool Ready()
	{
		std::lock_guard lk(g_mtx);
		return Init();
	}

	double Now()
	{
		static const auto t0 = std::chrono::steady_clock::now();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	}

	int Count(const std::string& a_prefix)
	{
		std::lock_guard lk(g_mtx);
		Init();
		int n = 0;
		while (g_bank.contains(a_prefix + std::to_string(n))) {
			++n;
		}
		return n;
	}

	void Play(const Voice& a_voice)
	{
		std::lock_guard lk(g_mtx);
		if (!Init()) {
			return;
		}
		const float vol = g_muted ? 0.0f : a_voice.volume * kMaster * Level(a_voice.name);
		if (vol <= 0.0001f) {
			return;
		}
		auto it = g_bank.find(a_voice.name);
		if (it == g_bank.end()) {
			return;
		}
		IXAudio2SourceVoice* voice = nullptr;
		for (auto* v : g_pool) {
			XAUDIO2_VOICE_STATE st{};
			if (v) {
				v->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
				if (st.BuffersQueued == 0) {
					voice = v;
					break;
				}
			}
		}
		if (!voice) {
			return;  // all busy: one sound fewer is never heard
		}
		voice->Stop();
		voice->FlushSourceBuffers();
		voice->SetVolume(vol);
		voice->SetFrequencyRatio(std::clamp(a_voice.pitch, 0.25f, 2.0f));
		XAUDIO2_FILTER_PARAMETERS fp{ LowPassFilter, 1.0f, 1.0f };
		if (a_voice.lowpass > 0.0f) {
			fp.Frequency = (std::min)(1.0f, 2.0f * std::sin(3.14159265f * a_voice.lowpass / kRate));
			fp.OneOverQ = 1.4142f;
		}
		voice->SetFilterParameters(&fp);
		SetPan(voice, a_voice.pan);
		XAUDIO2_BUFFER buf{};
		buf.AudioBytes = static_cast<UINT32>(it->second.size() * 2);
		buf.pAudioData = reinterpret_cast<const BYTE*>(it->second.data());
		buf.Flags = XAUDIO2_END_OF_STREAM;
		voice->SubmitSourceBuffer(&buf);
		voice->Start();
	}

	void Take(const std::string& a_prefix, float a_volume, float a_pitchJitter, float a_pan)
	{
		const int n = Count(a_prefix);
		const std::string name = n > 0 ? a_prefix + std::to_string(std::rand() % n) : a_prefix;
		const float j = (static_cast<float>(std::rand()) / RAND_MAX - 0.5f) * 2.0f * a_pitchJitter;
		Play({ name, a_volume, 1.0f + j, 0.0f, a_pan });
	}

	void SetLoop(const std::string& a_name, float a_volume)
	{
		std::lock_guard lk(g_mtx);
		if (!Init()) {
			return;
		}
		auto& l = g_loops[a_name];
		if (!l.voice) {
			auto it = g_bank.find(a_name);
			if (it == g_bank.end() || a_volume <= 0.0f) {
				return;
			}
			const auto wf = Format();
			if (FAILED(g_xa->CreateSourceVoice(&l.voice, &wf, 0, 2.0f)) || !l.voice) {
				l.voice = nullptr;
				return;
			}
			XAUDIO2_BUFFER buf{};
			buf.AudioBytes = static_cast<UINT32>(it->second.size() * 2);
			buf.pAudioData = reinterpret_cast<const BYTE*>(it->second.data());
			buf.LoopCount = XAUDIO2_LOOP_INFINITE;
			l.voice->SetVolume(0.0f);
			l.voice->SubmitSourceBuffer(&buf);
			l.voice->Start();
		}
		l.vol += (a_volume - l.vol) * 0.12f;
		l.voice->SetVolume(g_muted ? 0.0f : l.vol * kMaster * Level(a_name));
	}

	void StopAll()
	{
		std::lock_guard lk(g_mtx);
		if (!g_ok) {
			return;
		}
		for (auto* v : g_pool) {
			if (v) {
				v->Stop();
				v->FlushSourceBuffers();
			}
		}
		for (auto& [name, l] : g_loops) {
			l.vol = 0.0f;
			if (l.voice) {
				l.voice->SetVolume(0.0f);
			}
		}
	}

	void SetMuted(bool a_muted) { g_muted = a_muted; }
	bool Muted() { return g_muted; }
}
