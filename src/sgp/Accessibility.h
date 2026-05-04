#ifndef SGP_ACCESSIBILITY_H_
#define SGP_ACCESSIBILITY_H_

#include <string_theory/string>

/* Tolk-backed text-to-speech wrapper for screen-reader output.
 *
 * The wrapper loads Tolk.dll at runtime via LoadLibrary; if the DLL is
 * missing or fails to initialize (or on non-Windows builds), every entry
 * point becomes a silent no-op. Callers do not need to guard their calls.
 *
 * Tolk auto-detects a running screen reader (NVDA, JAWS, etc.) and falls
 * back to Microsoft SAPI when none is active. */

/** Initialize the accessibility layer. Returns true if a speech channel
 * is available (Tolk loaded and a screen reader / SAPI is reachable).
 * Safe to call more than once. */
bool AX_Init(void);

/** Shut down the accessibility layer. Safe to call more than once and
 * safe to call when never initialized. */
void AX_Shutdown(void);

/** True if AX_Say will produce audible output. */
bool AX_IsAvailable(void);

/** Speak text. When interrupt is true, any in-progress utterance is
 * cancelled first; this is the right default for focus-change narration.
 * No-op when AX_IsAvailable is false. */
void AX_Say(const ST::string& text, bool interrupt = true);

/** Cancel any in-progress utterance. */
void AX_Silence(void);

#endif // SGP_ACCESSIBILITY_H_
