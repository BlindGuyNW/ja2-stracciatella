#ifndef __AIMMEMBERS_H_
#define __AIMMEMBERS_H_

#include "JA2Types.h"
#include <string_theory/string>


void EnterAIMMembers(void);
void ExitAIMMembers(void);
void HandleAIMMembers(void);
void RenderAIMMembers(void);


void DrawNumeralsToScreen(INT32 iNumber, INT8 bWidth, UINT16 usLocX, UINT16 usLocY, SGPFont, UINT8 ubColor);

void DisplayTextForMercFaceVideoPopUp(const ST::string& str);
void EnterInitAimMembers(void);
void RenderAIMMembersTopLevel(void);

// if merc is still annoyed, reset back to 0
void ResetMercAnnoyanceAtPlayer(ProfileID);

void DisableNewMailMessage(void);
void DisplayPopUpBoxExplainingMercArrivalLocationAndTime(void);


// enumerated types used for the Video Conferencing Display
enum AIMVideoMode
{
	AIM_VIDEO_NOT_DISPLAYED_MODE,          // The video popup is not displayed
	AIM_VIDEO_POPUP_MODE,                  // The title bar pops up out of the Contact button
	AIM_VIDEO_INIT_MODE,                   // When the player first tries to contact the merc, it will be snowy for a bit
	AIM_VIDEO_FIRST_CONTACT_MERC_MODE,     // The popup that is displayed when first contactinf the merc
	AIM_VIDEO_HIRE_MERC_MODE,              // The popup which deals with the contract length, and transfer funds
	AIM_VIDEO_MERC_ANSWERING_MACHINE_MODE, // The popup which will be instread of the AIM_VIDEO_FIRST_CONTACT_MERC_MODE if the merc is not there
	AIM_VIDEO_MERC_UNAVAILABLE_MODE,       // The popup which will be instread of the AIM_VIDEO_FIRST_CONTACT_MERC_MODE if the merc is unavailable
	AIM_VIDEO_POPDOWN_MODE,                // The title bars pops down to the contact button
};

// which mode are we in during video conferencing?..0 means no video conference
extern AIMVideoMode gubVideoConferencingMode;


// Console-verb bridges. Each does what the corresponding GUI click would
// do, with the same engine side effects (popup-box delete, redraw flag,
// video conferencing reset). The Members page does not need to be the
// active laptop page; if it is, the next frame will redraw.

// Replaces the runs of Previous/Next clicks needed to land on a given
// merc. Returns false if profileID is not in the live AimMercArray
// (i.e., the Members Sort page hasn't been visited or this profile
// isn't AIM-listed).
bool   AIMMembers_ShowProfile(UINT8 profileID);

// Equivalent to clicking the Contact button on the current profile.
// Triggers the standard video popup mode and animation.
void   AIMMembers_StartContact();

// Reads the merc currently selected on the Members page. Useful for
// `aim contact` to print the name of who's being contacted.
UINT8  AIMMembers_CurrentProfile();

// Snapshot of the contact-popup state, for read-only verbs like
// `aim status` that need to surface what the GUI's select-lights would
// show a sighted player.
struct AIMMembers_PopupState
{
	AIMVideoMode mode;             // gubVideoConferencingMode
	UINT8        contractLength;   // 0 = 1 day, 1 = 1 week, 2 = 2 weeks
	bool         buyEquipment;     // gfBuyEquipment
	bool         gearAvailable;    // usOptionalGearCost > 0
	INT32        contractAmount;   // giContractAmount (recomputed total)
	bool         mercTalking;      // gfMercIsTalking
};
AIMMembers_PopupState AIMMembers_GetPopupState();

// Pop-up driving — equivalent to clicking the matching button in the
// contact popup. Each returns false if the popup isn't in a state where
// the button exists (e.g., contract-length buttons exist only in
// HIRE_MERC_MODE).

// HIRE_MERC_MODE only. 0 = day, 1 = week, 2 = biweek. Updates
// gubContractLength and recomputes giContractAmount.
bool AIMMembers_SetContractLength(UINT8 length);

// HIRE_MERC_MODE only. Toggles the optional-equipment package.
// Returns false if the merc has no optional gear (the GUI disables
// the "yes" button in that case).
bool AIMMembers_SetBuyEquipment(bool buy);

// FIRST_CONTACT_MERC_MODE → advance to HIRE_MERC_MODE.
// HIRE_MERC_MODE → commit the hire (engine runs insufficient-funds /
// over-20-mercs / contract-acceptance branches and pops a result box).
bool AIMMembers_Authorize();

// FIRST_CONTACT_MERC_MODE → hang up (close popup).
// HIRE_MERC_MODE → cancel back to FIRST_CONTACT_MERC_MODE.
bool AIMMembers_CancelAuthorize();

#endif
