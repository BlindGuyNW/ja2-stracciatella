#include "Accessibility.h"

#include "AxLog.h"
#include "Logger.h"

#include <string_theory/string>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

namespace
{

#ifdef _WIN32

	using Tolk_Load_t                 = void          (*)(void);
	using Tolk_Unload_t               = void          (*)(void);
	using Tolk_IsLoaded_t             = bool          (*)(void);
	using Tolk_TrySAPI_t              = void          (*)(bool);
	using Tolk_Output_t               = bool          (*)(const wchar_t*, bool);
	using Tolk_Silence_t              = bool          (*)(void);
	using Tolk_DetectScreenReader_t   = const wchar_t*(*)(void);

	HMODULE                     g_tolk     = nullptr;
	bool                        g_attempted = false;

	Tolk_Load_t                 p_Load     = nullptr;
	Tolk_Unload_t               p_Unload   = nullptr;
	Tolk_IsLoaded_t             p_IsLoaded = nullptr;
	Tolk_TrySAPI_t              p_TrySAPI  = nullptr;
	Tolk_Output_t               p_Output   = nullptr;
	Tolk_Silence_t              p_Silence  = nullptr;
	Tolk_DetectScreenReader_t   p_Detect   = nullptr;

	template <typename Fn>
	bool resolve(Fn& fn, const char* name)
	{
		fn = reinterpret_cast<Fn>(GetProcAddress(g_tolk, name));
		return fn != nullptr;
	}

	void release()
	{
		if (g_tolk)
		{
			if (p_Unload) p_Unload();
			FreeLibrary(g_tolk);
			g_tolk = nullptr;
		}
		p_Load = nullptr;
		p_Unload = nullptr;
		p_IsLoaded = nullptr;
		p_TrySAPI = nullptr;
		p_Output = nullptr;
		p_Silence = nullptr;
		p_Detect = nullptr;
	}

	std::wstring widen(const char* utf8)
	{
		if (!utf8 || !*utf8) return {};
		int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
		if (n <= 1) return {};
		std::wstring out(static_cast<size_t>(n - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), n);
		return out;
	}

#endif // _WIN32

} // anonymous namespace

bool AX_Init(void)
{
#ifdef _WIN32
	if (g_attempted) return AX_IsAvailable();
	g_attempted = true;

	g_tolk = LoadLibraryW(L"Tolk.dll");
	if (!g_tolk)
	{
		SLOGI("Tolk.dll not found; screen-reader output disabled.");
		return false;
	}

	const bool resolved =
		resolve(p_Load,     "Tolk_Load")               &&
		resolve(p_Unload,   "Tolk_Unload")             &&
		resolve(p_IsLoaded, "Tolk_IsLoaded")           &&
		resolve(p_TrySAPI,  "Tolk_TrySAPI")            &&
		resolve(p_Output,   "Tolk_Output")             &&
		resolve(p_Silence,  "Tolk_Silence")            &&
		resolve(p_Detect,   "Tolk_DetectScreenReader");

	if (!resolved)
	{
		SLOGW("Tolk.dll loaded but is missing expected symbols; disabling.");
		release();
		return false;
	}

	p_TrySAPI(true); // fall back to Microsoft SAPI when no screen reader is running
	p_Load();

	if (!p_IsLoaded())
	{
		SLOGW("Tolk failed to initialize; screen-reader output disabled.");
		release();
		return false;
	}

	const wchar_t* sr = p_Detect();
	if (sr && *sr)
	{
		const int n = WideCharToMultiByte(CP_UTF8, 0, sr, -1, nullptr, 0, nullptr, nullptr);
		if (n > 1)
		{
			std::string narrow(static_cast<size_t>(n - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, sr, -1, narrow.data(), n, nullptr, nullptr);
			SLOGI("Accessibility output ready (screen reader: {}).", narrow);
		}
		else
		{
			SLOGI("Accessibility output ready (screen reader detected).");
		}
	}
	else
	{
		SLOGI("Accessibility output ready (using SAPI fallback).");
	}
	return true;
#else
	return false;
#endif
}

void AX_Shutdown(void)
{
#ifdef _WIN32
	release();
	g_attempted = false;
#endif
}

bool AX_IsAvailable(void)
{
#ifdef _WIN32
	return g_tolk && p_IsLoaded && p_IsLoaded();
#else
	return false;
#endif
}

void AX_Say(const ST::string& text, bool interrupt)
{
	// Log even when Tolk isn't loaded — the "engine wanted to say X but
	// nothing was heard" case is the more diagnostic one. Bounded by
	// speech rate, so the volume is sane.
	AX_LOG("[say{}] {}", interrupt ? " interrupt" : "", text);
#ifdef _WIN32
	if (!AX_IsAvailable()) return;
	const std::wstring wide = widen(text.c_str());
	if (wide.empty()) return;
	p_Output(wide.c_str(), interrupt);
#else
	(void)text; (void)interrupt;
#endif
}

void AX_Silence(void)
{
	AX_LOG("[silence]");
#ifdef _WIN32
	if (!AX_IsAvailable()) return;
	p_Silence();
#endif
}
