#include "AxLog.h"

#include "RustInterface.h"

#include <chrono>
#include <cstdio>

namespace
{
	std::FILE* g_log = nullptr;

	std::chrono::steady_clock::time_point launchTime()
	{
		static const auto t = std::chrono::steady_clock::now();
		return t;
	}
}

bool Ax_LogInit(void)
{
	if (g_log) return true;
	RustPointer<char> path(Logger_getFilePath("ja2-ax.log"));
	if (!path) return false;
	g_log = std::fopen(path.get(), "w");
	if (!g_log) return false;
	(void)launchTime(); // anchor t0 for elapsed timestamps
	std::fprintf(g_log, "[    0.000] ja2-ax.log opened\n");
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
