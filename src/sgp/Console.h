#ifndef SGP_CONSOLE_H_
#define SGP_CONSOLE_H_

#include <cstddef>
#include <functional>
#include <string>
#include <string_theory/string>

/* Screen-reader command console.
 *
 * Owns a worker thread that blocks on stdin and pushes each command line
 * onto a thread-safe inbox queue. The game thread drains the queue once
 * per frame via Console_Drain; replies are written back to stdout from
 * the game thread while it processes a command. Engine state is touched
 * only from the game thread; this thread parses, never mutates.
 *
 * Output is plain UTF-8 to stdout. NVDA reads new console lines as they
 * appear; the user can review with the screen reader's review cursor. */

void   Console_Init(void);
void   Console_Shutdown(void);

/** Drain pending command lines, invoking handler for each. Call once per
 *  frame from the game thread. Returns the number of lines handled. */
std::size_t Console_Drain(const std::function<void(const std::string&)>& handler);

/** Print one line to stdout (newline appended). Thread-safe. */
void Console_Println(const ST::string& line);

/* Strategic-message ring buffer for the `log` verb. The engine's own
 * gMapScreenMessageList is file-static in Message.cc, so we capture each
 * MapScreenMessage call as it happens and serve from our ring. */

void Console_CaptureMapMessage(const ST::string& str);

void Console_ForEachRecentMapMessage(std::size_t n,
	const std::function<void(const ST::string&)>& visit);

#endif // SGP_CONSOLE_H_
