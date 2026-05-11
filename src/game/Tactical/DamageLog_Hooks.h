#ifndef GAME_TACTICAL_DAMAGE_LOG_HOOKS_H_
#define GAME_TACTICAL_DAMAGE_LOG_HOOKS_H_

#include "DamageLog.h"
#include "JA2Types.h"
#include "Structure.h"
#include "Types.h"

/* Game-side push wrappers for DamageLog.
 *
 * The pure ring buffer lives in src/sgp/DamageLog.{h,cc} and knows
 * nothing about structures, soldiers, or visibility. Everything that
 * marshals engine state into a Record lives here:
 *
 *   - noun classification from STRUCTURE_* flags + material
 *   - visibility lookup (ConsoleVis::IsKnownTile, public opplist)
 *   - per-kind dedup window (multi-tile structures, burst-fire repeats)
 *
 * Each push site in the engine code calls one of these wrappers with
 * exactly the arguments it has in scope. No engine file constructs
 * Record directly. */

namespace DamageLog
{
	// Soldier hit -- attack-driven (EVENT_SoldierGotHit) or non-attack
	// (bleed / fall / gas / etc. via SoldierTakeDamage).
	void PushSoldierHit(
		const SOLDIERTYPE* tgt, INT8 lifeBefore, INT8 breathBefore,
		const SOLDIERTYPE* att, UINT16 weapon, UINT8 reason,
		UINT8 hitLoc, UINT8 special);

	// DamageStructure damaged-or-destroyed return path. Pass the actual
	// result; the wrapper picks the right disposition and dedups
	// repeat hits on the same structure within a short window.
	void PushStructure(
		STRUCTURE* s, GridNo gridno, INT8 level,
		StructureDamageReason reason, StructureDamageResult result,
		const SOLDIERTYPE* owner, UINT16 weapon_or_item);

	// STRUCTURE_EXPLOSIVE branch inside DamageStructure (gunfire). The
	// prop's HP is zeroed and IgniteExplosionXY is queued before
	// DamageStructure returns NOT_DAMAGED; we record this as IGNITED
	// even though the function's return claims no damage.
	void PushStructureIgnited(
		STRUCTURE* s, GridNo gridno, INT8 level,
		const SOLDIERTYPE* owner, UINT16 weapon);

	// WindowHit after a successful SwapStructureForPartner. shattered=true
	// when bPartnerDelta == NO_PARTNER_STRUCTURE post-swap (final stage);
	// false for cracked (intermediate stage).
	void PushWindow(
		GridNo gridno, INT8 level, bool shattered,
		Cause cause, const SOLDIERTYPE* actor, UINT16 weapon_or_item);

	// Lock destruction by kick (AttemptToSmashDoor success). Bullets
	// and shaped charges already fire ScreenMsg lines our AX_Say hook
	// narrates; we still log them for review.
	void PushLockEvent(
		GridNo gridno, INT8 level, Disposition disp,
		Cause cause, const SOLDIERTYPE* actor);
}

#endif // GAME_TACTICAL_DAMAGE_LOG_HOOKS_H_
