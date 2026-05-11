#include "Console_Assign.h"

#include "Assignments.h"
#include "Campaign.h"
#include "Campaign_Types.h"
#include "JAScreens.h"
#include "MapScreen.h"
#include "Map_Screen_Interface.h"
#include "Overhead.h"
#include "Quests.h"
#include "ScreenIDs.h"
#include "Soldier_Control.h"
#include "Soldier_Profile_Type.h"
#include "Squads.h"
#include "StrategicMap.h"
#include "Strategic_Movement.h"
#include "Text.h"
#include "TownModel.h"
#include "Vehicles.h"

#include "Console.h"
#include "Console_Address.h"
#include "Console_Strategic.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_theory/format>
#include <string_theory/string>
#include <vector>

namespace
{
	// ---------------------------------------------------------------
	//   Localized labels
	// ---------------------------------------------------------------

	// Short attribute name ("Strength", "Marksmanship", ...). Index is the
	// STRENGTH..EXPLOSIVE_ASSIGN enum from Assignments.h (matches
	// pAttributeMenuStrings 0..8 by construction).
	ST::string statName(INT8 stat)
	{
		if (stat < 0 || stat >= NUM_TRAINABLE_STATS)
			return ST::string("?");
		return pAttributeMenuStrings[stat];
	}

	ST::string vehicleShortName(VEHICLETYPE const& v)
	{
		return pShortVehicleStrings[v.ubVehicleType];
	}

	// ---------------------------------------------------------------
	//   Stat parsing — accepts case-insensitive prefix of the localized
	//   attribute name. Also accepts bare digits 0..8 for the enum.
	// ---------------------------------------------------------------
	bool parseStat(std::string const& tokRaw, INT8& out, ST::string& err)
	{
		std::string const tok = lower(tokRaw);
		if (tok.empty())
		{
			err = ST::string("missing stat (try strength | marksmanship | mechanical | ...)");
			return false;
		}

		long n;
		if (parseInt(tok, n) && n >= 0 && n < NUM_TRAINABLE_STATS)
		{
			out = static_cast<INT8>(n);
			return true;
		}

		INT8 hit = -1;
		std::size_t hits = 0;
		for (INT8 i = 0; i < NUM_TRAINABLE_STATS; ++i)
		{
			std::string const label = lower(pAttributeMenuStrings[i].to_std_string());
			if (label.rfind(tok, 0) == 0) { hit = i; ++hits; }
		}
		if (hits == 1) { out = hit; return true; }
		if (hits > 1)
			err = ST::format("ambiguous stat: {} (e.g. 'mark' for marksmanship, 'mech' for mechanical)", tokRaw);
		else
			err = ST::format("unknown stat: {} (try strength | dexterity | agility | health | marksmanship | medical | mechanical | leadership | explosives)", tokRaw);
		return false;
	}

	// ---------------------------------------------------------------
	//   Vehicle lookup — by case-insensitive prefix on the localized
	//   short name. Filtered to vehicles the soldier could actually
	//   reach (in-sector, accessible per the engine's own predicate).
	//   Returns vehicle id (index into pVehicleList) or -1.
	// ---------------------------------------------------------------
	INT32 findAccessibleVehicleByName(SOLDIERTYPE const& s,
	                                  std::string const& wantRaw,
	                                  ST::string&        err)
	{
		std::string const want = lower(wantRaw);
		INT32 hit = -1;
		std::size_t hits = 0;
		FOR_EACH_VEHICLE(v)
		{
			if (!IsThisVehicleAccessibleToSoldier(s, v)) continue;
			std::string const name = lower(vehicleShortName(v).to_std_string());
			if (name.rfind(want, 0) == 0)
			{
				hit = VEHICLE2ID(v);
				++hits;
			}
		}
		if (hits == 1) return hit;
		if (hits > 1)
			err = ST::format("ambiguous vehicle: {} (try a longer prefix)", wantRaw);
		else
			err = ST::format("no accessible vehicle matching: {}", wantRaw);
		return -1;
	}

	// List accessible vehicles for a soldier — used when the user types
	// `assign <merc> vehicle` without a type, so they know what to pick.
	void printAccessibleVehicles(SOLDIERTYPE const& s)
	{
		std::size_t shown = 0;
		FOR_EACH_VEHICLE(v)
		{
			if (!IsThisVehicleAccessibleToSoldier(s, v)) continue;
			++shown;
			Console_Println(ST::format("  {} ({})",
				vehicleShortName(v),
				GetNumberInVehicle(v)));
		}
		if (shown == 0)
			Console_Println("  (no accessible vehicles in this sector)");
	}

	// ---------------------------------------------------------------
	//   Pairing decoders — read engine state to find who's working with
	//   whom. The engine doesn't store explicit doctor↔patient or
	//   trainer↔student pointers; pairings emerge from being in the
	//   same sector with compatible assignments. UpdateAssignments
	//   re-evaluates these hourly.
	// ---------------------------------------------------------------
	std::vector<SOLDIERTYPE const*> patientsOfDoctor(SOLDIERTYPE const& doctor)
	{
		std::vector<SOLDIERTYPE const*> out;
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (s == &doctor)             continue;
			if (s->sSector != doctor.sSector) continue;
			if (s->bAssignment != PATIENT) continue;
			out.push_back(s);
		}
		return out;
	}

	SOLDIERTYPE const* doctorForPatient(SOLDIERTYPE const& patient)
	{
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (s == &patient)            continue;
			if (s->sSector != patient.sSector) continue;
			if (s->bAssignment != DOCTOR) continue;
			return s;
		}
		return nullptr;
	}

	std::vector<SOLDIERTYPE const*> studentsOfTrainer(SOLDIERTYPE const& trainer)
	{
		std::vector<SOLDIERTYPE const*> out;
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (s == &trainer)                       continue;
			if (s->sSector != trainer.sSector)       continue;
			if (s->bAssignment != TRAIN_BY_OTHER)    continue;
			if (s->bTrainStat != trainer.bTrainStat) continue;
			out.push_back(s);
		}
		return out;
	}

	SOLDIERTYPE const* trainerForStudent(SOLDIERTYPE const& student)
	{
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (s == &student)                       continue;
			if (s->sSector != student.sSector)       continue;
			if (s->bAssignment != TRAIN_TEAMMATE)    continue;
			if (s->bTrainStat != student.bTrainStat) continue;
			return s;
		}
		return nullptr;
	}

	// Mirror Assignments.cc's static GetTrainingStatValue. Re-implemented
	// here rather than exposed — it's a stable 9-case mapping over public
	// SOLDIERTYPE fields, and a one-call surface change wouldn't earn
	// its keep.
	INT8 currentStatValue(SOLDIERTYPE const& s, INT8 stat)
	{
		switch (stat)
		{
			case STRENGTH:         return s.bStrength;
			case DEXTERITY:        return s.bDexterity;
			case AGILITY:          return s.bAgility;
			case HEALTH:           return s.bLifeMax;
			case MARKSMANSHIP:     return s.bMarksmanship;
			case MEDICAL:          return s.bMedical;
			case MECHANICAL:       return s.bMechanical;
			case LEADERSHIP:       return s.bLeadership;
			case EXPLOSIVE_ASSIGN: return s.bExplosive;
		}
		return 0;
	}

	// Same per-hour throughput the GUI face strip paints (Faces.cc:867).
	// usMaxPts is the engine's "if your stats were normal" upper bound
	// (no drugs, no fatigue); pts is what actually accumulates this hour.
	void printTrainingProgress(SOLDIERTYPE const& s)
	{
		static const SGPSector gunRange(GUN_RANGE_X, GUN_RANGE_Y, GUN_RANGE_Z);
		BOOLEAN const fAtGunRange = (s.sSector == gunRange);
		UINT16 maxPts = 0;
		INT16  pts    = 0;

		switch (s.bAssignment)
		{
			case TRAIN_SELF:
			{
				pts = GetSoldierTrainingPts(&s, s.bTrainStat, fAtGunRange, &maxPts);
				INT8 const cur = currentStatValue(s, s.bTrainStat);
				Console_Println(ST::format(
					"  Current {}: {}/{}. Wisdom: {}.",
					statName(s.bTrainStat), cur, TRAINING_RATING_CAP, s.bWisdom));
				Console_Println(ST::format(
					"  Training pts this hour: {} (max {} at full wisdom / no fatigue).",
					pts, maxPts));
				if (s.bTrainStat == MARKSMANSHIP && fAtGunRange)
					Console_Println("  Gun range bonus active.");
				break;
			}
			case TRAIN_BY_OTHER:
			{
				pts = GetSoldierStudentPts(&s, s.bTrainStat, fAtGunRange, &maxPts);
				INT8 const cur = currentStatValue(s, s.bTrainStat);
				Console_Println(ST::format(
					"  Current {}: {}/{}. Wisdom: {}.",
					statName(s.bTrainStat), cur, TRAINING_RATING_CAP, s.bWisdom));
				Console_Println(ST::format(
					"  Training pts this hour: {} (max {}). Includes trainer bonus.",
					pts, maxPts));
				if (s.bTrainStat == MARKSMANSHIP && fAtGunRange)
					Console_Println("  Gun range bonus active.");
				break;
			}
			case TRAIN_TEAMMATE:
			{
				pts = GetBonusTrainingPtsDueToInstructor(&s, nullptr, s.bTrainStat, fAtGunRange, &maxPts);
				INT8 const teach = currentStatValue(s, s.bTrainStat);
				Console_Println(ST::format(
					"  Teacher's {}: {}. Wisdom: {}, leadership: {}.",
					statName(s.bTrainStat), teach, s.bWisdom, s.bLeadership));
				Console_Println(ST::format(
					"  Bonus pts to each student this hour: {} (max {}).",
					pts, maxPts));
				if (s.bTrainStat == MARKSMANSHIP && fAtGunRange)
					Console_Println("  Gun range bonus active.");
				break;
			}
			case TRAIN_TOWN:
			{
				pts = GetTownTrainPtsForCharacter(&s, &maxPts);
				Console_Println(ST::format(
					"  Leadership: {}. Wisdom: {}.",
					s.bLeadership, s.bWisdom));
				// GUI scales by 10 for face-strip readability; mirror so
				// the numbers a sighted player hears match what we say.
				Console_Println(ST::format(
					"  Militia training pts this hour: {} (max {}).",
					(pts + 5) / 10, (maxPts + 5) / 10));
				break;
			}
			default: break;
		}
	}

	ST::string joinNames(std::vector<SOLDIERTYPE const*> const& v)
	{
		ST::string out;
		for (auto const* p : v)
		{
			if (!out.empty()) out += ", ";
			out += p->name;
		}
		return out;
	}

	// ---------------------------------------------------------------
	//   formatAssignmentDetail — verbose, single-line description of a
	//   merc's current task. Reused by `assign list`, `assign <merc>`,
	//   and the bare `assign` summary's per-merc breakouts.
	//
	//   Sleep is a flag orthogonal to bAssignment (the engine doesn't
	//   change the assignment when a merc nods off; it just zeros their
	//   per-hour throughput in UpdateAssignments). We suffix "(asleep)"
	//   so the user hears the disconnect — Wilco's bAssignment still
	//   says Practice while he's snoring, which would otherwise mislead.
	// ---------------------------------------------------------------
	ST::string assignmentBase(SOLDIERTYPE const& s)
	{
		switch (s.bAssignment)
		{
			case DOCTOR:
			{
				auto const ps = patientsOfDoctor(s);
				if (ps.empty()) return ST::string("Doctor (no patients in sector)");
				return ST::format("Doctor (treating {})", joinNames(ps));
			}
			case PATIENT:
			{
				auto const* d = doctorForPatient(s);
				return d ? ST::format("Patient (treated by {})", d->name)
				         : ST::string("Patient (no doctor in sector)");
			}
			case ASSIGNMENT_HOSPITAL:
				return ST::string("Hospital");
			case REPAIR:
			{
				if (s.fFixingRobot)
					return ST::string("Repairing the robot");
				if (s.bVehicleUnderRepairID != -1)
				{
					VEHICLETYPE const& v = GetVehicle(s.bVehicleUnderRepairID);
					return ST::format("Repairing {}", vehicleShortName(v));
				}
				return ST::string("Repairing items");
			}
			case VEHICLE:
			{
				if (s.iVehicleId < 0) return ST::string("In vehicle");
				VEHICLETYPE const& v = GetVehicle(s.iVehicleId);
				return ST::format("In {}", vehicleShortName(v));
			}
			case TRAIN_SELF:
				return ST::format("Training {} (self)", statName(s.bTrainStat));
			case TRAIN_TOWN:
				return ST::string("Training militia");
			case TRAIN_TEAMMATE:
			{
				auto const studs = studentsOfTrainer(s);
				if (studs.empty())
					return ST::format("Trainer — teaching {} (no students)",
						statName(s.bTrainStat));
				return ST::format("Trainer — teaching {} to {}",
					statName(s.bTrainStat), joinNames(studs));
			}
			case TRAIN_BY_OTHER:
			{
				auto const* t = trainerForStudent(s);
				if (!t)
					return ST::format("Student of {} (no trainer)", statName(s.bTrainStat));
				return ST::format("Student of {} (taught by {})",
					statName(s.bTrainStat), t->name);
			}
			case IN_TRANSIT:        return ST::string("In transit");
			case ASSIGNMENT_DEAD:   return ST::string("Dead");
			case ASSIGNMENT_POW:    return ST::string("POW");
			default: break;
		}
		if (Console_IsOnSquad(s))
			return ST::format("On squad {}", Console_SquadNumber(s));
		return pAssignmentStrings[s.bAssignment];
	}

	ST::string formatAssignmentDetail(SOLDIERTYPE const& s)
	{
		ST::string const base = assignmentBase(s);
		if (Console_IsAsleep(s))
			return ST::format("{} (asleep)", base);
		return base;
	}

	// ---------------------------------------------------------------
	//   Eligibility diagnostic. Returns the same reason the mapscreen
	//   would use (shaded menu item / error popup) so the SR user
	//   hears why the engine refuses.
	// ---------------------------------------------------------------
	enum class Kind { Doctor, Patient, Repair, Vehicle, TrainSelf, TrainTown,
	                  TrainTeach, TrainLearn, Squad };

	ST::string conditionReason(SOLDIERTYPE const& s)
	{
		// We can't see the AC_* bitmask from outside, so probe the
		// engine state fields the same way AreAssignmentConditionsMet
		// does. Reasons are ordered most-specific-first so the message
		// names the real blocker.
		if (s.bAssignment == ASSIGNMENT_POW) return ST::string("POW");
		if (IsCharacterInTransit(s))         return ST::string("in transit between sectors");
		if (s.fBetweenSectors)               return ST::string("between sectors — wait until arrival");
		if (s.bLife < OKLIFE)                return ST::string("unconscious");
		if (s.sSector.z != 0)                return ST::string("underground — return to surface");
		if (s.bInSector && gTacticalStatus.fEnemyInSector)
			return ST::string("enemies in this sector — leave or clear first");
		if (s.ubWhatKindOfMercAmI == MERC_TYPE__EPC)
			return ST::string("EPC — not assignable");
		return ST::string();
	}

	ST::string whyCantAssign(SOLDIERTYPE const& s, Kind k, INT8 stat)
	{
		ST::string r = conditionReason(s);
		if (!r.empty()) return r;

		switch (k)
		{
			case Kind::Doctor:
				if (s.bMedical == 0) return ST::string("no medical skill");
				if (!CanCharacterDoctor(&s))
					return ST::string("no medical kit in inventory");
				break;
			case Kind::Patient:
				if (s.bLife == s.bLifeMax) return ST::string("already at full life");
				if (!CanCharacterPatient(&s))
					return ST::string("can't be a patient right now");
				break;
			case Kind::Repair:
				if (s.bMechanical == 0) return ST::string("no mechanical skill");
				if (!CanCharacterRepair(&s))
					return ST::string("no toolkit, or nothing in this sector to repair");
				break;
			case Kind::Vehicle:
				if (!CanCharacterVehicle(s))
					return ST::string("no accessible vehicle here, or not in mapscreen mode");
				break;
			case Kind::TrainSelf:
				if (!CanCharacterTrainStat(&s, stat, TRUE, FALSE))
					return ST::format("can't self-train {} (cap reached or stat is zero)",
						statName(stat));
				break;
			case Kind::TrainTown:
				if (s.bLeadership == 0) return ST::string("no leadership skill");
				if (!CanCharacterTrainMilitia(&s))
					return ST::string("not a town/SAM sector, loyalty too low, militia full, or trainer slots used");
				break;
			case Kind::TrainTeach:
				if (!CanCharacterTrainTeammates(&s))
					return ST::string("alone in sector, or can't train right now");
				if (!CanCharacterTrainStat(&s, stat, FALSE, TRUE))
					return ST::format("can't teach {} (need rating >= 70 in that stat)",
						statName(stat));
				break;
			case Kind::TrainLearn:
				if (!CanCharacterBeTrainedByOther(&s))
					return ST::string("alone in sector, or can't train right now");
				if (!CanCharacterTrainStat(&s, stat, TRUE, FALSE))
					return ST::format("can't learn {} (stat is zero or capped)",
						statName(stat));
				break;
			case Kind::Squad:
				// Joinability is checked inside AddCharacterToSquad's
				// flow; a richer reason isn't available without exposing
				// CanCharacterSquad. Caller surfaces the BOOLEAN result.
				break;
		}
		return ST::string();
	}

	// ---------------------------------------------------------------
	//   Bare `assign` summary — task counts.
	// ---------------------------------------------------------------
	void cmdAssignBare()
	{
		if (!Console_RequireCampaign()) return;

		std::size_t doctors = 0, patients = 0;
		std::size_t repairing = 0;
		std::size_t inVehicle = 0;
		std::size_t trainSelf = 0, trainTown = 0, trainTeach = 0, trainLearn = 0;

		CFOR_EACH_IN_CHAR_LIST(c)
		{
			SOLDIERTYPE const& s = *c->merc;
			if (!Console_IsAlive(s)) continue;
			switch (s.bAssignment)
			{
				case DOCTOR:         ++doctors;    break;
				case PATIENT:        ++patients;   break;
				case REPAIR:         ++repairing;  break;
				case VEHICLE:        ++inVehicle;  break;
				case TRAIN_SELF:     ++trainSelf;  break;
				case TRAIN_TOWN:    ++trainTown;  break;
				case TRAIN_TEAMMATE: ++trainTeach; break;
				case TRAIN_BY_OTHER: ++trainLearn; break;
				default: break;
			}
		}

		std::size_t const total = doctors + patients + repairing + inVehicle +
		                          trainSelf + trainTown + trainTeach + trainLearn;
		if (total == 0)
		{
			Console_Println("No mercs on non-combat assignment.");
			Console_Println("Use 'assign <merc> doctor|patient|repair|vehicle|train|squad' to set one.");
			return;
		}

		Console_Println(ST::format("{} merc{} on non-combat assignment:",
			total, total == 1 ? "" : "s"));
		auto line = [&](std::size_t n, char const* label)
		{
			if (n > 0) Console_Println(ST::format("  {} {}", n, label));
		};
		line(doctors,    doctors    == 1 ? "doctoring"             : "doctoring");
		line(patients,   patients   == 1 ? "patient"               : "patients");
		line(repairing,  repairing  == 1 ? "repairing"             : "repairing");
		line(inVehicle,  inVehicle  == 1 ? "in a vehicle"          : "in vehicles");
		line(trainSelf,  trainSelf  == 1 ? "training self"         : "training self");
		line(trainTown,  trainTown  == 1 ? "training militia"      : "training militia");
		line(trainTeach, trainTeach == 1 ? "teaching teammates"    : "teaching teammates");
		line(trainLearn, trainLearn == 1 ? "being trained"         : "being trained");
		Console_Println(
			"Use 'assign list' to see who and where, "
			"'assign <merc>' for one merc's detail.");
	}

	// ---------------------------------------------------------------
	//   `assign list` — workforce grouped by sector. Repopulates the
	//   shared `tN` tag table so a follow-up `assign t3 doctor` (or
	//   `team merc t3`) refers to what was just listed.
	// ---------------------------------------------------------------
	void cmdAssignList()
	{
		if (!Console_RequireCampaign()) return;

		// Collect (slot, sector) for mercs on a non-combat assignment.
		struct Row { INT8 slot; SGPSector sec; };
		std::vector<Row> rows;
		for (INT8 i = 0; i < MAX_CHARACTER_COUNT; ++i)
		{
			SOLDIERTYPE const* const s = gCharactersList[i].merc;
			if (!s)                                      continue;
			if (!Console_IsAlive(*s))                    continue;
			if (!Console_IsOnNonCombatAssignment(*s))    continue;
			rows.push_back({ i, s->sSector });
		}

		if (rows.empty())
		{
			Console_Println("No mercs on non-combat assignment.");
			return;
		}

		// Sort by sector (so same-sector rows cluster), then by slot.
		std::sort(rows.begin(), rows.end(),
			[](Row const& a, Row const& b)
			{
				if (a.sec < b.sec) return true;
				if (b.sec < a.sec) return false;
				return a.slot < b.slot;
			});

		Console_Println(ST::format(
			"{} merc{} on non-combat assignment:",
			rows.size(), rows.size() == 1 ? "" : "s"));

		std::vector<INT8> tagOrder;
		tagOrder.reserve(rows.size());

		SGPSector lastSec{};
		bool      printedHeader = false;
		std::size_t tagN = 0;
		for (Row const& r : rows)
		{
			SOLDIERTYPE const& s = *gCharactersList[r.slot].merc;
			if (!printedHeader || r.sec != lastSec)
			{
				Console_Println(ST::format("  {}:",
					GetSectorIDString(r.sec, FALSE)));
				lastSec = r.sec;
				printedHeader = true;
			}
			++tagN;
			tagOrder.push_back(r.slot);
			Console_Println(ST::format("    t{} {}: {}",
				tagN, s.name, formatAssignmentDetail(s)));
		}

		Console_RegisterTeamTags(tagOrder);
	}

	// ---------------------------------------------------------------
	//   `assign <merc>` — per-merc deep readout.
	// ---------------------------------------------------------------
	void cmdAssignDescribe(SOLDIERTYPE const& s)
	{
		Console_Println(ST::format("{} in {}.",
			s.name, GetSectorIDString(s.sSector, FALSE)));
		// Skip the inline "(asleep)" suffix here; we surface sleep on
		// its own line below so the user hears it as a discrete fact
		// rather than tucked at the end of a busy assignment string.
		Console_Println(ST::format("  Assignment: {}.", assignmentBase(s)));
		if (Console_IsAsleep(s))
			Console_Println("  Sleep: asleep (no progress on assignment until awake).");
		else if (s.bBreathMax <= BREATHMAX_PRETTY_TIRED)
			Console_Println("  Sleep: tired (breath max reduced — throughput penalty).");
		if (Console_IsOnSquad(s))
			Console_Println(ST::format("  Squad: {}.", Console_SquadNumber(s)));
		Console_Println(ST::format(
			"  Life {}/{}, breath {}/{}.",
			s.bLife, s.bLifeMax, s.bBreath, s.bBreathMax));
		// Training mercs get the same per-hour readout the GUI face
		// strip paints — current stat / cap / wisdom / pts-this-hour.
		printTrainingProgress(s);
	}

	// ---------------------------------------------------------------
	//   Setter wrappers — each prints a confirmation or the eligibility
	//   reason. The engine setters silently no-op on ineligibility, so
	//   we run whyCantAssign first; if it's clean, we call the setter
	//   and trust the engine to have applied it.
	// ---------------------------------------------------------------
	void confirm(SOLDIERTYPE const& s)
	{
		Console_Println(ST::format("{}: {}.",
			s.name, formatAssignmentDetail(s)));
	}

	void refuse(SOLDIERTYPE const& s, ST::string const& reason)
	{
		Console_Println(ST::format("{}: {}.", s.name, reason));
	}

	void doDoctor(SOLDIERTYPE& s)
	{
		auto r = whyCantAssign(s, Kind::Doctor, 0);
		if (!r.empty()) { refuse(s, r); return; }
		SetSoldierAssignmentDoctor(s);
		confirm(s);
	}

	void doPatient(SOLDIERTYPE& s)
	{
		auto r = whyCantAssign(s, Kind::Patient, 0);
		if (!r.empty()) { refuse(s, r); return; }
		SetSoldierAssignmentPatient(s);
		confirm(s);
	}

	// Target spec for `assign <merc> repair [robot | vehicle [<type>]]`.
	// No further arg = items. Vehicle with no type = list options.
	void doRepair(SOLDIERTYPE& s, std::vector<std::string> const& args, std::size_t at)
	{
		auto r = whyCantAssign(s, Kind::Repair, 0);
		if (!r.empty()) { refuse(s, r); return; }

		BOOLEAN robot      = FALSE;
		INT8    vehicleId  = -1;

		if (at < args.size())
		{
			std::string const sub = lower(args[at]);
			if (sub == "robot")
			{
				robot = TRUE;
			}
			else if (sub == "vehicle")
			{
				if (at + 1 >= args.size())
				{
					Console_Println(ST::format("{}: accessible vehicles here:", s.name));
					printAccessibleVehicles(s);
					Console_Println(
						"  Re-run as 'assign <merc> repair vehicle <type>'.");
					return;
				}
				ST::string err;
				INT32 const vid = findAccessibleVehicleByName(s, args[at + 1], err);
				if (vid < 0) { refuse(s, err); return; }
				vehicleId = static_cast<INT8>(vid);
			}
			else if (sub != "items")
			{
				Console_Println(ST::format(
					"{}: unknown repair target: {} (try items | robot | vehicle <type>)",
					s.name, args[at]));
				return;
			}
		}

		SetSoldierAssignmentRepair(s, robot, vehicleId);
		confirm(s);
	}

	void doVehicle(SOLDIERTYPE& s, std::vector<std::string> const& args, std::size_t at)
	{
		auto r = whyCantAssign(s, Kind::Vehicle, 0);
		if (!r.empty()) { refuse(s, r); return; }

		if (at >= args.size())
		{
			Console_Println(ST::format("{}: accessible vehicles here:", s.name));
			printAccessibleVehicles(s);
			Console_Println("  Re-run as 'assign <merc> vehicle <type>'.");
			return;
		}
		ST::string err;
		INT32 const vid = findAccessibleVehicleByName(s, args[at], err);
		if (vid < 0) { refuse(s, err); return; }

		VEHICLETYPE& v = GetVehicle(vid);
		if (!PutSoldierInVehicle(s, v))
		{
			refuse(s, ST::format("couldn't board {} (vehicle full or refused)",
				vehicleShortName(v)));
			return;
		}
		confirm(s);
	}

	void doTrainSelf(SOLDIERTYPE& s, std::string const& statArg)
	{
		INT8 stat;
		ST::string err;
		if (!parseStat(statArg, stat, err)) { refuse(s, err); return; }
		auto r = whyCantAssign(s, Kind::TrainSelf, stat);
		if (!r.empty()) { refuse(s, r); return; }
		SetSoldierAssignmentTrainSelf(s, stat);
		confirm(s);
	}

	void doTrainTown(SOLDIERTYPE& s)
	{
		auto r = whyCantAssign(s, Kind::TrainTown, 0);
		if (!r.empty()) { refuse(s, r); return; }
		SetSoldierAssignmentTrainTown(s);
		confirm(s);
	}

	void doTrainTeach(SOLDIERTYPE& s, std::string const& statArg)
	{
		INT8 stat;
		ST::string err;
		if (!parseStat(statArg, stat, err)) { refuse(s, err); return; }
		auto r = whyCantAssign(s, Kind::TrainTeach, stat);
		if (!r.empty()) { refuse(s, r); return; }
		SetSoldierAssignmentTrainTeammate(s, stat);
		confirm(s);
	}

	void doTrainLearn(SOLDIERTYPE& s, std::string const& statArg)
	{
		INT8 stat;
		ST::string err;
		if (!parseStat(statArg, stat, err)) { refuse(s, err); return; }
		auto r = whyCantAssign(s, Kind::TrainLearn, stat);
		if (!r.empty()) { refuse(s, r); return; }
		SetSoldierAssignmentTrainByOther(s, stat);
		confirm(s);
	}

	void doTrain(SOLDIERTYPE& s, std::vector<std::string> const& args, std::size_t at)
	{
		if (at >= args.size())
		{
			Console_Println(ST::format(
				"{}: usage 'train self <stat> | train town | train teach <stat> | train learn <stat>'",
				s.name));
			return;
		}
		std::string const sub = lower(args[at]);
		std::string const statArg = at + 1 < args.size() ? args[at + 1] : std::string();

		if      (sub == "self")  doTrainSelf(s, statArg);
		else if (sub == "town")  doTrainTown(s);
		else if (sub == "teach" || sub == "teammate" || sub == "teammates")
			doTrainTeach(s, statArg);
		else if (sub == "learn" || sub == "student" || sub == "by")
			doTrainLearn(s, statArg);
		else
		{
			Console_Println(ST::format(
				"{}: unknown train mode: {} (try self | town | teach | learn)",
				s.name, args[at]));
		}
	}

	void doSquad(SOLDIERTYPE& s, std::vector<std::string> const& args, std::size_t at)
	{
		if (at >= args.size())
		{
			Console_Println(ST::format(
				"{}: usage 'squad <1..{}>'",
				s.name, NUMBER_OF_SQUADS));
			return;
		}
		long n;
		if (!parseInt(args[at], n) || n < 1 || n > NUMBER_OF_SQUADS)
		{
			Console_Println(ST::format(
				"{}: invalid squad number: {} (1..{} valid)",
				s.name, args[at], NUMBER_OF_SQUADS));
			return;
		}
		INT8 const squad = static_cast<INT8>(n - 1);
		// Condition gate first so the user hears the same reasons the
		// mapscreen would shade the On-Duty menu for (combat, EPC, etc.).
		auto cond = conditionReason(s);
		if (!cond.empty()) { refuse(s, cond); return; }

		if (!AddCharacterToSquad(&s, squad))
		{
			refuse(s, ST::format(
				"can't join squad {} (different sector, squad full or moving, or vehicle constraint)",
				n));
			return;
		}
		confirm(s);
	}

	// ---------------------------------------------------------------
	//   Kind dispatch from `assign <merc> <kind> [args...]`.
	// ---------------------------------------------------------------
	void cmdAssignSet(SOLDIERTYPE& s, std::vector<std::string> const& args)
	{
		std::string const kind = lower(args[2]);
		if      (kind == "doctor")  doDoctor(s);
		else if (kind == "patient") doPatient(s);
		else if (kind == "repair")  doRepair(s, args, 3);
		else if (kind == "vehicle") doVehicle(s, args, 3);
		else if (kind == "train")   doTrain(s, args, 3);
		else if (kind == "squad")   doSquad(s, args, 3);
		else
		{
			Console_Println(ST::format(
				"{}: unknown assignment: {} "
				"(try doctor | patient | repair | vehicle | train | squad)",
				s.name, args[2]));
		}
	}
}

void Cmd_Assign(std::vector<std::string> const& args)
{
	Console_EnsureMapscreen();

	if (args.size() < 2) { cmdAssignBare(); return; }
	std::string const sub = lower(args[1]);
	if (sub == "list") { cmdAssignList(); return; }

	// Otherwise arg[1] is a merc reference. Resolve, then either
	// describe (no further args) or change (kind follows).
	if (!Console_RequireCampaign()) return;
	ST::string err;
	INT8 const slot = Console_ResolveCharSlot(args[1], err);
	if (slot < 0) { Console_Println(err); return; }
	SOLDIERTYPE& s = *gCharactersList[slot].merc;

	if (args.size() < 3) { cmdAssignDescribe(s); return; }
	cmdAssignSet(s, args);
}
