#ifndef SGP_TEXT_CAPTURE_H_
#define SGP_TEXT_CAPTURE_H_

#include "Types.h"

#include <string_theory/string>
#include <vector>

/* Running snapshot of text drawn through the Font module.
 *
 * MPrint feeds (text, rect) here as the engine paints; the console queries
 * it to enumerate clickable rows on screens that aren't built from buttons
 * (laptop list rows, save game slots, etc.) — channels where
 * GUI_BUTTON-walking misses the row entirely.
 *
 * JA2 paints with dirty rects: a screen draws its full contents once on
 * entry (or on a state change) and afterwards only repaints the rects
 * that actually changed. Per-frame buttons like Ok/Cancel are repainted
 * every frame, but the surrounding labels are not. So instead of a
 * per-frame buffer we keep a running snapshot, and treat each MPrint
 * call as "this rect was just overdrawn" — we drop any stored entries
 * whose rects overlap with it, then record the new one. That way
 * self-repainting buttons just refresh their own row while untouched
 * labels persist as long as they're still on screen.
 *
 * TextCapture_Reset is called on screen change so a new screen starts
 * clean rather than inheriting the prior screen's text. BeginFrame is
 * kept as a hook point but is currently a no-op.
 *
 * Threading: writes happen from the game thread (Font.cc) during rendering;
 * reads happen from the game thread (Console dispatcher). Same thread,
 * no locking. */

struct TextCaptureEntry
{
	INT16      x;
	INT16      y;
	INT16      w;
	INT16      h;
	ST::string text;
};

void TextCapture_BeginFrame(void);
void TextCapture_Reset(void);
void TextCapture_Append(INT16 x, INT16 y, INT16 w, INT16 h, ST::string text);
const std::vector<TextCaptureEntry>& TextCapture_Snapshot(void);

#endif // SGP_TEXT_CAPTURE_H_
