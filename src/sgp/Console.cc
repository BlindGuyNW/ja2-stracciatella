#include "Console.h"

#include "AxLog.h"
#include "Logger.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

namespace
{
	std::mutex              g_inbox_mutex;
	std::queue<std::string> g_inbox;

	std::mutex              g_out_mutex;

	std::atomic<bool>       g_running{false};
	std::thread             g_reader;

	// Strategic-message ring. Cap at 64 — enough for the user to scan
	// recent activity, small enough that growth never matters. Dropped
	// entries are the oldest. Mutex shared with stdout to keep the order
	// of capture / replay coherent if both fire from the game thread.
	constexpr std::size_t   kMapMsgRingMax = 64;
	std::mutex              g_msg_mutex;
	std::deque<ST::string>  g_msg_ring;

	void readerLoop()
	{
		std::string line;
		// Block on stdin. std::getline returns false on EOF (stdin closed,
		// e.g. console killed, ja2 shutting down). Either way the thread
		// exits cleanly.
		while (std::getline(std::cin, line))
		{
			if (!g_running.load(std::memory_order_relaxed)) break;
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty()) continue;
			std::lock_guard<std::mutex> lock(g_inbox_mutex);
			g_inbox.push(std::move(line));
			line.clear();
		}
	}
}

void Console_Init(void)
{
	if (g_running.exchange(true)) return;

#ifdef _WIN32
	// InitGlobalLocale earlier set ENABLE_EXTENDED_FLAGS only on the
	// console input handle, which disables line-mode reads. Restore the
	// flags getline expects, while keeping quick-edit off (selecting text
	// in quick-edit mode would block the engine on stdin reads).
	HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
	if (hIn != NULL && hIn != INVALID_HANDLE_VALUE)
	{
		SetConsoleMode(hIn,
			ENABLE_LINE_INPUT      |
			ENABLE_ECHO_INPUT      |
			ENABLE_PROCESSED_INPUT |
			ENABLE_EXTENDED_FLAGS);
	}
	SetConsoleTitleW(L"Jagged Alliance 2 \xE2\x80\x94 accessibility console");
#endif

	{
		std::lock_guard<std::mutex> lock(g_out_mutex);
		std::cout
			<< "JA2 accessibility console. Type 'help' for commands."
			<< std::endl;
	}

	SLOGI("Accessibility console started.");
	g_reader = std::thread(readerLoop);
}

void Console_Shutdown(void)
{
	if (!g_running.exchange(false)) return;
	// The reader is blocked on stdin and will exit when stdin closes
	// during process teardown. Detach so we don't deadlock waiting for
	// it; static mutexes outlive the thread by virtue of being static.
	if (g_reader.joinable()) g_reader.detach();
}

std::size_t Console_Drain(const std::function<void(const std::string&)>& handler)
{
	if (!handler) return 0;
	std::queue<std::string> local;
	{
		std::lock_guard<std::mutex> lock(g_inbox_mutex);
		std::swap(local, g_inbox);
	}
	std::size_t n = local.size();
	while (!local.empty())
	{
		handler(local.front());
		local.pop();
	}
	return n;
}

void Console_Println(const ST::string& line)
{
	std::lock_guard<std::mutex> lock(g_out_mutex);
	std::cout << line.c_str() << '\n';
	std::cout.flush();
	// Mirror the line into ja2-ax.log so a post-mortem (or a transcript
	// the user shares) doesn't depend on the console window still being
	// open. The dispatcher already echoes input as `> {line}`, so input
	// lines round-trip through here too — one funnel, one logged copy.
	AX_LOG("[console] {}", line);
}

void Console_CaptureMapMessage(const ST::string& str)
{
	if (str.empty()) return;
	std::lock_guard<std::mutex> lock(g_msg_mutex);
	g_msg_ring.push_back(str);
	if (g_msg_ring.size() > kMapMsgRingMax) g_msg_ring.pop_front();
}

void Console_ForEachRecentMapMessage(std::size_t n,
	const std::function<void(const ST::string&)>& visit)
{
	if (!visit) return;
	std::deque<ST::string> snapshot;
	{
		std::lock_guard<std::mutex> lock(g_msg_mutex);
		snapshot = g_msg_ring;
	}
	const std::size_t take = std::min(n, snapshot.size());
	for (std::size_t i = snapshot.size() - take; i < snapshot.size(); ++i)
	{
		visit(snapshot[i]);
	}
}
