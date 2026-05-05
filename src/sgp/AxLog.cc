#include "AxLog.h"

#include "RustInterface.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace
{
	std::FILE* g_log = nullptr;

	// Keep this many previous sessions alongside the current ja2-ax.log:
	// ja2-ax.1.log (most recent prior), ja2-ax.2.log, ja2-ax.3.log. Pre-rotation
	// any ja2-ax.3.log is dropped. Sessions can be 10+ MB on busy IMP screens
	// (text-capture is chatty), so 3 keeps a few days of accessibility tracing
	// at most without ballooning %TEMP%.
	constexpr int kRotateKeep = 3;

	std::chrono::steady_clock::time_point launchTime()
	{
		static const auto t = std::chrono::steady_clock::now();
		return t;
	}

	std::filesystem::path slotPath(int i)
	{
		const std::string name = (i == 0)
			? std::string("ja2-ax.log")
			: "ja2-ax." + std::to_string(i) + ".log";
		RustPointer<char> p(Logger_getFilePath(name.c_str()));
		return p ? std::filesystem::path(p.get()) : std::filesystem::path();
	}

	// Shifts ja2-ax.{N-1}.log → ja2-ax.{N}.log for N=kRotateKeep…1, and the
	// live ja2-ax.log to ja2-ax.1.log. Anything in slot kRotateKeep at start
	// of rotation is dropped. Failures are non-fatal — losing a backup is
	// strictly better than refusing to start fresh logging.
	void rotate()
	{
		std::error_code ec;
		std::filesystem::remove(slotPath(kRotateKeep), ec);
		for (int i = kRotateKeep - 1; i >= 0; --i)
		{
			const auto from = slotPath(i);
			const auto to   = slotPath(i + 1);
			if (from.empty() || to.empty()) continue;
			if (!std::filesystem::exists(from, ec)) continue;
			std::filesystem::rename(from, to, ec);
		}
	}
}

bool Ax_LogInit(void)
{
	if (g_log) return true;
	rotate();
	RustPointer<char> path(Logger_getFilePath("ja2-ax.log"));
	if (!path) return false;
	g_log = std::fopen(path.get(), "w");
	if (!g_log) return false;
	(void)launchTime(); // anchor t0 for elapsed timestamps
	std::fprintf(g_log, "[    0.000] ja2-ax.log opened (rotation keeps %d previous sessions as ja2-ax.{1..%d}.log)\n",
		kRotateKeep, kRotateKeep);
	std::fflush(g_log);
	return true;
}

void Ax_LogShutdown(void)
{
	if (!g_log) return;
	std::fclose(g_log);
	g_log = nullptr;
}

bool Ax_LogIsOpen(void)
{
	return g_log != nullptr;
}

void Ax_LogWrite(const ST::string& line)
{
	if (!g_log) return;
	const auto elapsed = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - launchTime()).count();
	std::fprintf(g_log, "[%9.3f] %s\n", elapsed, line.c_str());
	std::fflush(g_log);
}
