#include "Console_Gui.h"

#include "Button_System.h"
#include "MouseSystem.h"

#include "Console.h"

#include <cstdlib>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	bool parseId(const std::string& s, int& out)
	{
		if (s.empty()) return false;
		char* end = nullptr;
		long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0') return false;
		out = static_cast<int>(v);
		return true;
	}

	// Hidden buttons clear MSYS_REGION_ENABLED on their region (see
	// GUI_BUTTON::Hide); a logically-disabled-but-visible button keeps
	// it set and clears BUTTON_ENABLED instead. So a button is on screen
	// iff its region is enabled, and is actionable iff BUTTON_ENABLED.
	bool isVisible(const GUI_BUTTON& b)
	{
		if (b.uiFlags & BUTTON_DELETION_PENDING) return false;
		return (b.Area.uiFlags & MSYS_REGION_ENABLED) != 0;
	}

	ST::string labelFor(const GUI_BUTTON& b)
	{
		if (!b.Area.FastHelpText.empty()) return ST::string(b.Area.FastHelpText);
		if (!b.codepoints.empty())        return ST::string(b.codepoints);
		return ST::format("@{},{}", b.X(), b.Y());
	}

	void cmdList()
	{
		std::size_t shown = 0;
		for (INT32 i = 0; i < MAX_BUTTONS; ++i)
		{
			GUI_BUTTON* const b = ButtonList[i];
			if (!b || !isVisible(*b)) continue;

			const char* tag = b->Enabled() ? "" : " (disabled)";
			Console_Println(ST::format(
				"  {} — {}{} ({}x{} at {},{})",
				b->IDNum, labelFor(*b), tag, b->W(), b->H(), b->X(), b->Y()));
			++shown;
		}
		if (shown == 0) Console_Println("(no visible buttons)");
	}

	void cmdClick(const std::vector<std::string>& args)
	{
		if (args.size() < 3)
		{
			Console_Println("usage: g click <id>");
			return;
		}
		int id;
		if (!parseId(args[2], id) || id < 0 || id >= MAX_BUTTONS)
		{
			Console_Println(ST::format("invalid button id: {}", args[2]));
			return;
		}
		GUI_BUTTON* const b = ButtonList[id];
		if (!b || (b->uiFlags & BUTTON_DELETION_PENDING))
		{
			Console_Println(ST::format("no button at id {}", id));
			return;
		}
		if (!isVisible(*b))
		{
			Console_Println(ST::format("button {} is hidden", id));
			return;
		}
		if (!b->Enabled())
		{
			Console_Println(ST::format("button {} is disabled", id));
			return;
		}
		if (!b->ClickCallback)
		{
			Console_Println(ST::format("button {} has no click handler", id));
			return;
		}

		// Position-sensitive ClickCallbacks read these.
		const INT16 cx = b->X() + b->W() / 2;
		const INT16 cy = b->Y() + b->H() / 2;
		b->Area.MouseXPos    = cx;
		b->Area.MouseYPos    = cy;
		b->Area.RelativeXPos = static_cast<INT16>(cx - b->X());
		b->Area.RelativeYPos = static_cast<INT16>(cy - b->Y());

		// Skips QuickButtonCallbackMButn (anchor/toggle/sound state) but
		// fires the user-supplied action. DWN-then-UP is required for
		// callbacks built via ButtonCallbackPrimarySecondary.
		const ST::string label = labelFor(*b);
		b->ClickCallback(b, MSYS_CALLBACK_REASON_LBUTTON_DWN);
		b->ClickCallback(b, MSYS_CALLBACK_REASON_LBUTTON_UP);
		Console_Println(ST::format("clicked {} ({})", id, label));
	}
}

void Cmd_Gui(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		Console_Println("usage: g list | g click <id>");
		return;
	}
	const std::string& sub = args[1];
	if      (sub == "list")  cmdList();
	else if (sub == "click") cmdClick(args);
	else Console_Println(ST::format("unknown subcommand: g {} (try 'g list')", sub));
}
