#ifndef SGP_AX_LOG_H_
#define SGP_AX_LOG_H_

#include <string_theory/format>
#include <string_theory/string>

/* Dedicated debug log channel for the screen-reader / accessibility
 * stack. Writes to ja2-ax.log next to ja2.log (same directory the Rust
 * Logger picks for ja2.log itself; %TEMP% on Windows by default).
 *
 * Kept separate from the engine logger so accessibility tracing —
 * per-frame text capture, narration events, region lookups — doesn't
 * drown ja2.log, and so the user can tail it without filtering. The
 * file is rewritten on every launch, matching ja2.log behavior.
 *
 * Single-threaded by convention: writes happen from the game thread
 * during rendering and dispatch, so no locking. Init early (right after
 * Logger_initialize) so any setup tracing has a route. */

bool Ax_LogInit(void);
void Ax_LogShutdown(void);
bool Ax_LogIsOpen(void);
void Ax_LogWrite(const ST::string& line);

template <typename... Args>
inline void Ax_Log(Args&&... args)
{
	if (Ax_LogIsOpen())
	{
		Ax_LogWrite(ST::format(std::forward<Args>(args)...));
	}
}

#define AX_LOG(...) Ax_Log(__VA_ARGS__)

#endif // SGP_AX_LOG_H_
