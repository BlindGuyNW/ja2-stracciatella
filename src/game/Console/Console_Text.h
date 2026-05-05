#ifndef GAME_CONSOLE_TEXT_H_
#define GAME_CONSOLE_TEXT_H_

#include <string>
#include <vector>

/* Generic console interface to the engine's text-input widget
 * (Text_Input.{h,cc}). Lists active fields when called bare; reads or
 * writes a specific field by id. Useful wherever the engine puts up an
 * input field — IMP activation code / name / nickname today; save-game
 * name, AIM filter, Bobby Ray search box later. */

void Cmd_Text(const std::vector<std::string>& args);

#endif // GAME_CONSOLE_TEXT_H_
