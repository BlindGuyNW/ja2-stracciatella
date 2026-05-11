#include "Console_Email.h"

#include "EMail.h"
#include "JAScreens.h"
#include "ScreenIDs.h"
#include "Text.h"
#include "WordWrap.h"

#include "Console.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	bool laptopGate()
	{
		if (guiCurrentScreen == LAPTOP_SCREEN) return true;
		Console_Println("Open your laptop first (mapscreen → laptop button).");
		return false;
	}

	bool parseId(const std::string& s, std::size_t& out)
	{
		if (s.empty()) return false;
		char* end = nullptr;
		long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str() || *end != '\0' || v < 0) return false;
		out = static_cast<std::size_t>(v);
		return true;
	}

	// Email text in email.edt embeds in-band formatting codes (177–181:
	// newline, bold, center, color). The GUI strips them via
	// CleanOutControlCodesFromString before drawing; we do the same here
	// or they show up as ±²³´µ on the console.
	ST::string cleanText(const ST::string& s)
	{
		return CleanOutControlCodesFromString(s);
	}

	// Subjects from email.edt have a leading space baked in by AddEmailMessage
	// (" {}"). Strip it (and any control codes) so list output lines up cleanly.
	ST::string trimmedSubject(const Email& m)
	{
		ST::string s = cleanText(m.pSubject);
		while (!s.empty() && std::isspace(static_cast<unsigned char>(s[0])))
		{
			s = s.substr(1);
		}
		return s;
	}

	const ST::string& senderName(UINT8 ubSender)
	{
		return pSenderNameList[ubSender];
	}

	INT32 dayOf(const Email& m) { return m.iDate / (24 * 60); }

	void cmdList(bool unreadOnly)
	{
		std::size_t shown = 0;
		std::size_t id    = 0;
		for (const Email* m = pEmailList; m; m = m->Next, ++id)
		{
			if (unreadOnly && m->fRead) continue;

			Console_Println(ST::format(
				"  {} — {} | {} | {} {}{}",
				id,
				senderName(m->ubSender),
				trimmedSubject(*m),
				pDayStrings, dayOf(*m),
				m->fRead ? "" : " [unread]"));
			++shown;
		}

		if (shown == 0)
		{
			Console_Println(unreadOnly ? "(no unread mail)" : "(inbox empty)");
		}
	}

	void cmdOpen(std::size_t id)
	{
		Email* m = pEmailList;
		for (std::size_t i = 0; m && i < id; ++i) m = m->Next;
		if (!m)
		{
			Console_Println(ST::format("no email at id {}", id));
			return;
		}

		Console_Println(ST::format("From: {}", senderName(m->ubSender)));
		Console_Println(ST::format("Date: {} {}", pDayStrings, dayOf(*m)));
		Console_Println(ST::format("Subject: {}", trimmedSubject(*m)));
		Console_Println("");

		if (m->usOffset == IMP_EMAIL_PROFILE_RESULTS)
		{
			// Body isn't in EMAIL.EDT — only the subject is. The actual
			// personality / attitude / skills prose is dynamically built
			// from IMP-results records keyed on the player IMP's profile.
			// Email_ReadIMPProfileResultsBody brackets the engine's
			// assembly so no GUI viewer state lingers.
			for (auto const& para : Email_ReadIMPProfileResultsBody())
			{
				Console_Println(cleanText(para));
			}
		}
		else
		{
			// Body records sit at usOffset+1 .. usOffset+usLength-1.
			const UINT32 first = m->usOffset + 1;
			const UINT32 last  = m->usOffset + m->usLength;
			for (UINT32 i = first; i < last; ++i)
			{
				Console_Println(cleanText(LoadEMailText(i)));
			}
		}

		// Match the GUI: opening an email marks it read AND triggers the
		// engine's side effects (bookmark IMP/MERC, refresh insurance
		// amounts). Without firing the side effects, reading the IMP
		// intro from the console would never bookmark IMP, so `web imp`
		// would refuse forever. The wrapper skips the GUI-only profile-
		// results branch.
		m->fRead = TRUE;
		Email_FireOpenSideEffects(m);
	}
}

void Cmd_Email(const std::vector<std::string>& args)
{
	if (!laptopGate()) return;

	if (args.size() < 2 || args[1] == "list") { cmdList(false); return; }
	if (args[1] == "unread")                  { cmdList(true);  return; }

	std::size_t id;
	if (parseId(args[1], id)) { cmdOpen(id); return; }

	Console_Println(ST::format(
		"unknown subcommand: email {} (try 'email', 'email unread', 'email <id>')",
		args[1]));
}
