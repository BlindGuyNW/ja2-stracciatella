#ifndef GAME_CONSOLE_IMP_H_
#define GAME_CONSOLE_IMP_H_

#include <string>
#include <vector>

/* IMP (Institute for Mercenary Profiling) creation flow.
 *
 * The IMP web page has 13 sub-screens with text inputs, radio buttons,
 * sliders, and a confirmation chain — none of whose controls carry
 * SetFastHelpText, so the hover-narration hook buys nothing here. This
 * verb covers the screens the generic g/r/text verbs can't address
 * cleanly: gender, attribute slider, trait toggles, voice/portrait
 * indicator, quiz answer correlation.
 *
 * All subverbs gate on guiCurrentLaptopMode == LAPTOP_MODE_CHAR_PROFILE.
 * Per-screen subverbs additionally check iCurrentImpPage. */

void Cmd_Imp(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_IMP_H_
