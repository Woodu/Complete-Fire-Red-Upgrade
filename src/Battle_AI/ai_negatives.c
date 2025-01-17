#include "../defines.h"
#include "../defines_battle.h"
#include "../../include/random.h"
#include "../../include/constants/items.h"

#include "../../include/new/ability_tables.h"
#include "../../include/new/accuracy_calc.h"
#include "../../include/new/ai_advanced.h"
#include "../../include/new/ai_util.h"
#include "../../include/new/ai_master.h"
#include "../../include/new/battle_start_turn_start.h"
#include "../../include/new/battle_util.h"
#include "../../include/new/battle_script_util.h"
#include "../../include/new/damage_calc.h"
#include "../../include/new/end_turn.h"
#include "../../include/new/util.h"
#include "../../include/new/item.h"
#include "../../include/new/general_bs_commands.h"
#include "../../include/new/move_tables.h"
/*
ai_negatives.c
	All possible subtractions to an AIs move viability.
*/

#define TARGETING_PARTNER (bankDef == bankAtkPartner)
#define PARTNER_MOVE_EFFECT_IS_SAME (IS_DOUBLE_BATTLE \
									&& gBattleMoves[move].effect == gBattleMoves[partnerMove].effect \
									&& gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE \
									&& gBattleStruct->moveTarget[bankAtkPartner] == bankDef)
#define PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET (IS_DOUBLE_BATTLE \
									&& gBattleMoves[move].effect == gBattleMoves[partnerMove].effect \
									&& gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE)
#define PARTNER_MOVE_EFFECT_IS_STATUS_SAME_TARGET (IS_DOUBLE_BATTLE \
									&& gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE \
									&& gBattleStruct->moveTarget[bankAtkPartner] == bankDef \
									&& (gBattleMoves[partnerMove].effect == EFFECT_SLEEP \
									 || gBattleMoves[partnerMove].effect == EFFECT_POISON \
									 || gBattleMoves[partnerMove].effect == EFFECT_TOXIC \
									 || gBattleMoves[partnerMove].effect == EFFECT_PARALYZE \
									 || gBattleMoves[partnerMove].effect == EFFECT_WILL_O_WISP \
									 || gBattleMoves[partnerMove].effect == EFFECT_YAWN))

#define PARTNER_MOVE_EFFECT_IS_WEATHER (IS_DOUBLE_BATTLE \
									&& gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE \
									&& (gBattleMoves[partnerMove].effect == EFFECT_SUNNY_DAY \
									 || gBattleMoves[partnerMove].effect == EFFECT_RAIN_DANCE \
									 || gBattleMoves[partnerMove].effect == EFFECT_SANDSTORM \
									 || gBattleMoves[partnerMove].effect == EFFECT_HAIL))
#define PARTNER_MOVE_EFFECT_IS_TERRAIN (IS_DOUBLE_BATTLE \
									&& gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE \
									&& gBattleMoves[partnerMove].effect == EFFECT_SET_TERRAIN)
#define PARTNER_MOVE_IS_TAILWIND_TRICKROOM (IS_DOUBLE_BATTLE \
									&& gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE \
									&& (partnerMove == MOVE_TAILWIND || partnerMove == MOVE_TRICKROOM))

#define PARTNER_MOVE_IS_SAME (IS_DOUBLE_BATTLE \
							  && move == partnerMove \
						   	  && gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE \
							  && gBattleStruct->moveTarget[bankAtkPartner] == bankDef)
#define PARTNER_MOVE_IS_SAME_NO_TARGET (IS_DOUBLE_BATTLE \
										&& gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE \
										&& move == partnerMove)

//Doubles is now defined as being a non 1v1 Double Battle
#undef IS_DOUBLE_BATTLE
#define IS_DOUBLE_BATTLE (gBattleTypeFlags & BATTLE_TYPE_DOUBLE && ((BATTLER_ALIVE(foe1) && BATTLER_ALIVE(foe2)) || BATTLER_ALIVE(bankAtkPartner)))

extern move_effect_t gSetStatusMoveEffects[];
extern move_effect_t gStatLoweringMoveEffects[];
extern move_effect_t gConfusionMoveEffects[];
extern species_t gTelekinesisBanList[];
extern const struct FlingStruct gFlingTable[];

//This file's functions:
static void AI_Flee(void);
static void AI_Watch(void);

u8 AI_Script_Negatives(const u8 bankAtk, const u8 bankDef, const u16 originalMove, const u8 originalViability)
{
	u8 decreased;
	u16 predictedMove = IsValidMovePrediction(bankDef, bankAtk); //The move the target is likely to make against the attacker
	u32 i;
	s16 viability = originalViability;

	u16 move = TryReplaceMoveWithZMove(bankAtk, bankDef, originalMove);

	u16 atkSpecies = SPECIES(bankAtk);
	u16 defSpecies = SPECIES(bankDef);
	u8 atkAbility = GetAIAbility(bankAtk, bankDef, move);
	u8 defAbility = GetAIAbility(bankDef, bankAtk, predictedMove);

	if (!NO_MOLD_BREAKERS(atkAbility, move)
	&& gMoldBreakerIgnoredAbilities[defAbility])
		defAbility = ABILITY_NONE;

	u8 atkEffect = ITEM_EFFECT(bankAtk);	//unused
	u8 defEffect = ITEM_EFFECT(bankDef);
	u16 defItem = ITEM(bankDef);
	u16 atkItem = ITEM(bankAtk);

	u8 atkQuality = ITEM_QUALITY(bankAtk);
	//u8 defQuality = ITEM_QUALITY(bankDef);	//unused
	u32 atkStatus1 = gBattleMons[bankAtk].status1;
	u32 defStatus1 = gBattleMons[bankDef].status1;
	u32 atkStatus2 = gBattleMons[bankAtk].status2;
	u32 defStatus2 = gBattleMons[bankDef].status2;
	u32 atkStatus3 = gStatuses3[bankAtk];
	u32 defStatus3 = gStatuses3[bankDef];
	u8 atkGender = GetGenderFromSpeciesAndPersonality(atkSpecies, gBattleMons[bankAtk].personality);
	u8 defGender = GetGenderFromSpeciesAndPersonality(defSpecies, gBattleMons[bankDef].personality);

	u8 moveEffect = gBattleMoves[move].effect;
	u8 moveSplit = CalcMoveSplit(bankAtk, move);
	u8 moveTarget = gBattleMoves[move].target;
	u8 moveType = GetMoveTypeSpecial(bankAtk, move);
	u8 moveFlags = gBattleMoves[move].flags;
	u16 moveAcc = AccuracyCalc(move, bankAtk, bankDef);

	//Load partner data
	u8 bankAtkPartner = (gBattleTypeFlags & BATTLE_TYPE_DOUBLE) ? PARTNER(bankAtk) : bankAtk;
	u8 bankDefPartner = (gBattleTypeFlags & BATTLE_TYPE_DOUBLE) ? PARTNER(bankDef) : bankDef;
	u8 atkPartnerAbility = (gBattleTypeFlags & BATTLE_TYPE_DOUBLE) ? ABILITY(bankAtkPartner) : ABILITY_NONE;
	u8 defPartnerAbility = (gBattleTypeFlags & BATTLE_TYPE_DOUBLE) ? ABILITY(bankDefPartner) : ABILITY_NONE;

	u16 partnerMove = MOVE_NONE;
	if (!IsBankIncapacitated(bankAtkPartner))
		partnerMove = (gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE) ? gChosenMovesByBanks[bankAtkPartner] : IsValidMovePrediction(bankAtkPartner, bankAtk);

	//Load Alternative targets
	u8 foe1, foe2;
	foe1 = FOE(bankAtk);

	if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
		foe2 = PARTNER(FOE(bankAtk));
	else
		foe2 = foe1;

	//Affects User Check
	if (moveTarget & MOVE_TARGET_USER)
		goto MOVESCR_CHECK_0;

	//Check who Partner's attacking in doubles
	if (IS_DOUBLE_BATTLE
	&&  !IsBankIncapacitated(bankAtkPartner)
	&&  gChosenMovesByBanks[bankAtkPartner] != MOVE_NONE //Partner actually selected a move
	&&  gBattleStruct->moveTarget[bankAtkPartner] == bankDef
	&&  gBattleMoves[gChosenMovesByBanks[bankAtkPartner]].target & MOVE_TARGET_SELECTED //Partner isn't using spread move
	&&  CountAliveMonsInBattle(BATTLE_ALIVE_DEF_SIDE, bankAtk, bankDef) >= 2 //With one target left, both Pokemon should aim for the same target
	&&  MoveKnocksOutXHits(gChosenMovesByBanks[bankAtkPartner], bankAtkPartner, gBattleStruct->moveTarget[bankAtkPartner], 1)
	&&  SPLIT(move) == SPLIT_STATUS)
	{
		DECREASE_VIABILITY(9); //Don't use a status move on the mon who the Partner is set to KO
		return viability;
	}

	//Ranged Move Check
	if (moveTarget & (MOVE_TARGET_BOTH | MOVE_TARGET_ALL))
	{
		if (moveType == TYPE_ELECTRIC && ABILITY_ON_OPPOSING_FIELD(bankAtk, ABILITY_LIGHTNINGROD))
		{
			DECREASE_VIABILITY(20);
			return viability;
		}
		else if (moveType == TYPE_WATER && ABILITY_ON_OPPOSING_FIELD(bankAtk, ABILITY_STORMDRAIN))
		{
			DECREASE_VIABILITY(20);
			return viability;
		}
	}

	#ifdef AI_TRY_TO_KILL_RATE
		u8 killRate = AI_TRY_TO_KILL_RATE;

		#ifdef VAR_GAME_DIFFICULTY
		if (VarGet(VAR_GAME_DIFFICULTY) == OPTIONS_EASY_DIFFICULTY)
			killRate = AI_TRY_TO_KILL_RATE / 5;
		#endif

		if (AI_THINKING_STRUCT->aiFlags == AI_SCRIPT_CHECK_BAD_MOVE //Only basic AI
		&& gRandomTurnNumber % 100 < killRate
		&& DamagingMoveInMoveset(bankAtk)
		&& !TARGETING_PARTNER)
		{
			if (IS_SINGLE_BATTLE || CountAliveMonsInBattle(BATTLE_ALIVE_DEF_SIDE, bankAtk, bankDef) == 1) //Single Battle or only 1 target left
			{
				if (MoveKnocksOutPossiblyGoesFirstWithBestAccuracy(move, bankAtk, bankDef, TRUE)) //Check going first
					INCREASE_VIABILITY(7);

				else if (IsStrongestMove(move, bankAtk, bankDef))
					INCREASE_VIABILITY(2);
			}
			else //Double Battle
			{
				IncreaseDoublesDamageViability(&viability, 0xFF, bankAtk, bankDef, move);
			}
		}
	#endif

	// Gravity Table Prevention Check
	if (IsGravityActive() && CheckTableForMove(move, gGravityBannedMoves))
		return 0; //Can't select this move period

	// Ungrounded check
	if (CheckGrounding(bankDef) == IN_AIR && moveType == TYPE_GROUND)
		return 0;

	// Powder Move Checks (safety goggles, defender has grass type, overcoat, and powder move table)
	if (CheckTableForMove(move, gPowderMoves) && !IsAffectedByPowder(bankDef))
		DECREASE_VIABILITY(10); //No return b/c could be reduced further by absorb abilities

	//Target Ability Checks
	if (NO_MOLD_BREAKERS(atkAbility, move))
	{
		switch (defAbility) { //Type-specific ability checks - primordial weather handled separately

			//Electric
			case ABILITY_VOLTABSORB:
			case ABILITY_MOTORDRIVE:
			case ABILITY_LIGHTNINGROD:
				if (moveType == TYPE_ELECTRIC) // && (moveSplit != SPLIT_STATUS))
				{
					if (!TARGETING_PARTNER) //Good idea to attack partner
					{
						DECREASE_VIABILITY(20);
						return viability;
					}
				}
				break;

			// Water
			case ABILITY_WATERABSORB:
			case ABILITY_DRYSKIN:
			case ABILITY_STORMDRAIN:
				if (moveType == TYPE_WATER)
				{
					if (!TARGETING_PARTNER) //Good idea to attack partner
					{
						DECREASE_VIABILITY(20);
						return viability;
					}
				}
				break;

			//case ABILITY_WATERCOMPACTION:
			//	if (moveType == TYPE_WATER)
			//		return viability - 10;
			//	break;

			// Fire
			case ABILITY_FLASHFIRE:
				if (moveType == TYPE_FIRE)
				{
					if (!TARGETING_PARTNER) //Good idea to attack partner
					{
						DECREASE_VIABILITY(20);
						return viability;
					}
				}
				break;

			//case ABILITY_HEATPROOF:
			//case ABILITY_WATERBUBBLE: //Handled by damage calc
			//	if (moveType == TYPE_FIRE) // && (moveSplit != SPLIT_STATUS))
			//		return viability - 10;
			//	break;

			// Grass
			case ABILITY_SAPSIPPER:
				if (moveType == TYPE_GRASS)
				{
					if (!TARGETING_PARTNER) //Good idea to attack partner
					{
						DECREASE_VIABILITY(20);
						return viability;
					}
				}
				break;

			// Dark
			case ABILITY_JUSTIFIED:
				if (moveType == TYPE_DARK && moveSplit != SPLIT_STATUS)
				{
					if (!TARGETING_PARTNER) //Good idea to attack partner
					{
						DECREASE_VIABILITY(9);
						return viability;
					}
				}
				break;

			//Multiple move types
			case ABILITY_RATTLED:
				if ((moveSplit != SPLIT_STATUS)
				&& (moveType == TYPE_DARK || moveType == TYPE_GHOST || moveType == TYPE_BUG))
				{
					if (!TARGETING_PARTNER) //Good idea to attack partner
					{
						DECREASE_VIABILITY(9);
						return viability;
					}
				}
				break;

			//Move category checks
			case ABILITY_SOUNDPROOF:
				if (CheckSoundMove(move))
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_BULLETPROOF:
				if (CheckTableForMove(move, gBallBombMoves))
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_DAZZLING:
			case ABILITY_QUEENLYMAJESTY:
				if (PriorityCalc(bankAtk, ACTION_USE_MOVE, move) > 0) //Check if right num
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_AROMAVEIL:
				if (CheckTableForMove(move, gAromaVeilProtectedMoves))
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_SWEETVEIL:
				if (moveEffect == EFFECT_SLEEP || moveEffect == EFFECT_YAWN)
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_FLOWERVEIL:
				if (IsOfType(bankDef, TYPE_GRASS)
				&& (CheckTableForMoveEffect(move, gSetStatusMoveEffects) || CheckTableForMoveEffect(move, gStatLoweringMoveEffects)))
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_MAGICBOUNCE:
				if (moveFlags & FLAG_MAGIC_COAT_AFFECTED)
				{
					DECREASE_VIABILITY(20);
					return viability;
				}
				break;

			case ABILITY_CONTRARY:
				if (CheckTableForMoveEffect(move, gStatLoweringMoveEffects))
				{
					if (!TARGETING_PARTNER) //Good idea to attack partner
					{
						DECREASE_VIABILITY(20);
						return viability;
					}
				}
				break;

			case ABILITY_CLEARBODY:
			case ABILITY_FULLMETALBODY:
			case ABILITY_WHITESMOKE:
				if (CheckTableForMoveEffect(move, gStatLoweringMoveEffects))
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_HYPERCUTTER:
				if ((moveEffect == EFFECT_ATTACK_DOWN ||  moveEffect == EFFECT_ATTACK_DOWN_2)
				&& move != MOVE_PLAYNICE && move != MOVE_NOBLEROAR && move != MOVE_TEARFULLOOK && move != MOVE_VENOMDRENCH)
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_KEENEYE:
				if (moveEffect == EFFECT_ACCURACY_DOWN
				||  moveEffect == EFFECT_ACCURACY_DOWN_2)
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_BIGPECKS:
				if (moveEffect == EFFECT_DEFENSE_DOWN
				||  moveEffect == EFFECT_DEFENSE_DOWN_2)
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_DEFIANT:
			case ABILITY_COMPETITIVE:
				if (CheckTableForMoveEffect(move, gStatLoweringMoveEffects))
				{
					if (!TARGETING_PARTNER) //Good idea to attack partner
					{
						DECREASE_VIABILITY(8); //Not 10 b/c move still works, just not recommended
						return viability;
					}
				}
				break;

			case ABILITY_COMATOSE:
				if (CheckTableForMoveEffect(move, gSetStatusMoveEffects))
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_SHIELDSDOWN:
				if (GetBankPartyData(bankDef)->species == SPECIES_MINIOR_SHIELD
				&&  CheckTableForMoveEffect(move, gSetStatusMoveEffects))
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case ABILITY_WONDERSKIN:
				if (moveSplit == SPLIT_STATUS)
					moveAcc = 50;
				break;

			case ABILITY_LEAFGUARD:
				if (WEATHER_HAS_EFFECT && (gBattleWeather & WEATHER_SUN_ANY)
				&& CheckTableForMoveEffect(move, gSetStatusMoveEffects))
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;
		}

		//Target Partner Ability Check
		if (IS_DOUBLE_BATTLE && !TARGETING_PARTNER)
		{
			switch (defPartnerAbility) {
				case ABILITY_LIGHTNINGROD:
					if (moveType == TYPE_ELECTRIC)
					{
						DECREASE_VIABILITY(20);
						return viability;
					}
					break;

				case ABILITY_STORMDRAIN:
					if (moveType == TYPE_WATER)
					{
						DECREASE_VIABILITY(20);
						return viability;
					}
					break;

				case ABILITY_SWEETVEIL:
					if (moveEffect == EFFECT_SLEEP || moveEffect == EFFECT_YAWN)
					{
						DECREASE_VIABILITY(10);
						return viability;
					}
					break;

				case ABILITY_FLOWERVEIL:
					if ((IsOfType(bankDef, TYPE_GRASS))
					&& (CheckTableForMoveEffect(move, gSetStatusMoveEffects) || CheckTableForMoveEffect(move, gStatLoweringMoveEffects)))
					{
						DECREASE_VIABILITY(10);
						return viability;
					}
					break;

				case ABILITY_AROMAVEIL:
					if (CheckTableForMove(move, gAromaVeilProtectedMoves))
					{
						DECREASE_VIABILITY(10);
						return viability;
					}
					break;

				case ABILITY_DAZZLING:
				case ABILITY_QUEENLYMAJESTY:
					if (PriorityCalc(bankAtk, ACTION_USE_MOVE, move) > 0) //Check if right num
					{
						DECREASE_VIABILITY(10);
						return viability;
					}
					break;
			}
		}
	}

	#ifndef OLD_PRANKSTER
	if (atkAbility == ABILITY_PRANKSTER)
	{
		if (IsOfType(bankDef, TYPE_DARK)
		&& moveSplit == SPLIT_STATUS
		&& !(moveTarget & (MOVE_TARGET_OPPONENTS_FIELD | MOVE_TARGET_USER))) //Directly strikes target
		{
			DECREASE_VIABILITY(10);
			return viability;
		}
	}
	#endif

	//Terrain Check
	if (CheckGrounding(bankDef) == GROUNDED)
	{
		switch (gTerrainType) {
			case ELECTRIC_TERRAIN:
				if (moveEffect == EFFECT_SLEEP || moveEffect == EFFECT_YAWN)
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case MISTY_TERRAIN:
				if (CheckTableForMoveEffect(move, gSetStatusMoveEffects) || CheckTableForMoveEffect(move, gConfusionMoveEffects))
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;

			case PSYCHIC_TERRAIN:
				if (PriorityCalc(bankAtk, ACTION_USE_MOVE, move) > 0)
				{
					DECREASE_VIABILITY(10);
					return viability;
				}
				break;
		}
	}

	//Throat Chop Check
	if (CantUseSoundMoves(bankAtk) && CheckSoundMove(move))
		return 0; //Can't select this move period

	//Heal Block Check
	if (IsHealBlocked(bankAtk) && CheckHealingMove(move))
		return 0; //Can't select this move period

	//Primal Weather Check
	if (gBattleWeather & WEATHER_SUN_PRIMAL && moveType == TYPE_WATER && moveSplit != SPLIT_STATUS)
	{
		DECREASE_VIABILITY(20);
		return viability;
	}
	else if (gBattleWeather & WEATHER_RAIN_PRIMAL && moveType == TYPE_FIRE && moveSplit != SPLIT_STATUS)
	{
		DECREASE_VIABILITY(20);
		return viability;
	}

	// Check Move Effects
	MOVESCR_CHECK_0:
	switch (moveEffect)
	{
		case EFFECT_HIT:
			goto AI_STANDARD_DAMAGE;

		case EFFECT_SLEEP:
			if (AI_SpecialTypeCalc(move, bankAtk, bankDef) & MOVE_RESULT_NO_EFFECT)
			{
				DECREASE_VIABILITY(10);
				break;
			}

		AI_CHECK_SLEEP: ;
			if (!CanBePutToSleep(bankDef, TRUE)
			|| (MoveBlockedBySubstitute(move, bankAtk, bankDef))
			|| PARTNER_MOVE_EFFECT_IS_STATUS_SAME_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_ABSORB:
			if (defAbility == ABILITY_LIQUIDOOZE)
				DECREASE_VIABILITY(6);

			if (move == MOVE_STRENGTHSAP)
			{
				if (defAbility == ABILITY_CONTRARY)
					DECREASE_VIABILITY(10);
				else if (!STAT_CAN_FALL(bankDef, STAT_STAGE_ATK))
					DECREASE_VIABILITY(10);
				break;
			}
			break;

		case EFFECT_EXPLOSION:
		#ifdef OKAY_WITH_AI_SUICIDE
			if (NO_MOLD_BREAKERS(atkAbility, move) && ABILITY_PRESENT(ABILITY_DAMP))
			{
				DECREASE_VIABILITY(10);
			}
			else if (ViableMonCountFromBank(bankDef) == 1 //If the Target only has one PKMN left
			&& MoveKnocksOutXHits(move, bankAtk, bankDef, 1)) //The AI can knock out the target
			{
				//Good to use move
			}
			else if (IS_DOUBLE_BATTLE)
			{
				if (ViableMonCountFromBank(bankDef) == 2 //If the Target has both Pokemon remaining in doubles
				&& MoveKnocksOutXHits(move, bankAtk, bankDef, 1)
				&& MoveKnocksOutXHits(move, bankAtk, bankDefPartner, 1))
				{
					//Good to use move
				}
				else
					DECREASE_VIABILITY(4);
			}
			else //Single Battle
			{
				if (MoveKnocksOutXHits(move, bankAtk, bankDef, 1)) //The AI can knock out the target
				{
					if (ViableMonCountFromBank(bankDef) == 1) //If the Target only has one PKMN left
					{
						//Good to use move
					}
					else if (CanKnockOutWithoutMove(move, bankAtk, bankDef))
						DECREASE_VIABILITY(4); //Better to use a different move to knock out
				}
				else
					DECREASE_VIABILITY(4);
			}
		#else
			DECREASE_VIABILITY(10);
		#endif
			break;

		case EFFECT_DREAM_EATER:
			if (defAbility != ABILITY_COMATOSE && !(defStatus1 & STATUS1_SLEEP))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_MIRROR_MOVE: //May cause issues with priority calcs?
			switch (move) {
				case MOVE_COPYCAT:
					if (MoveWouldHitFirst(move, bankAtk, bankDef))
					{
					COPYCAT_CHECK_LAST_MOVE:
						if (gNewBS->LastUsedMove == MOVE_NONE
						|| gNewBS->LastUsedMove == 0xFFFF
						|| CheckTableForMove(gNewBS->LastUsedMove, gCopycatBannedMoves)
						|| FindMovePositionInMoveset(gNewBS->LastUsedMove, bankAtk) < 4) //If you have the move, use it directly
							DECREASE_VIABILITY(10);
						else
							return AI_Script_Negatives(bankAtk, bankDef, gNewBS->LastUsedMove, originalViability);
					}
					else
					{
						if (predictedMove == MOVE_NONE)
							goto COPYCAT_CHECK_LAST_MOVE;
						else if (CheckTableForMove(predictedMove, gCopycatBannedMoves)
							 || FindMovePositionInMoveset(predictedMove, bankAtk) < 4)
						{
							DECREASE_VIABILITY(10);
						}
						else
							return AI_Script_Negatives(bankAtk, bankDef, predictedMove, originalViability);
					}
					break;

				default: //Mirror Move
					if (gBattleStruct->lastTakenMoveFrom[bankAtk][bankDef] != MOVE_NONE)
						return AI_Script_Negatives(bankAtk, bankDef, gBattleStruct->lastTakenMoveFrom[bankAtk][bankDef], originalViability);
					DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_SPLASH:
			if (!IsTypeZCrystal(atkItem, moveType) || gNewBS->ZMoveData->used[bankAtk])
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_TELEPORT:
			DECREASE_VIABILITY(10);
			break;

		case EFFECT_ATTACK_UP:
		case EFFECT_ATTACK_UP_2:
			if (atkAbility != ABILITY_CONTRARY)
			{
				switch (move) {
					case MOVE_HONECLAWS:
						if (STAT_STAGE(bankAtk, STAT_STAGE_ATK) >= STAT_STAGE_MAX
						&& (STAT_STAGE(bankAtk, STAT_STAGE_ACC) >= STAT_STAGE_MAX || !PhysicalMoveInMoveset(bankAtk)))
							DECREASE_VIABILITY(10);
						break;

					default:
						if (STAT_STAGE(bankAtk, STAT_STAGE_ATK) >= STAT_STAGE_MAX || !PhysicalMoveInMoveset(bankAtk))
							DECREASE_VIABILITY(10);
				}
			}
			else
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_DEFENSE_UP:
		case EFFECT_DEFENSE_UP_2:
		case EFFECT_DEFENSE_CURL:
			switch (move) {
				case MOVE_FLOWERSHIELD:
					if (!IsOfType(bankAtk, TYPE_GRASS)
					&&  !(IS_DOUBLE_BATTLE && IsOfType(bankAtkPartner, TYPE_GRASS)))
						DECREASE_VIABILITY(10);
					break;

				case MOVE_MAGNETICFLUX:
					if (atkAbility == ABILITY_PLUS || atkAbility == ABILITY_MINUS)
						goto AI_COSMIC_POWER;

					if (IS_DOUBLE_BATTLE)
					{
						if (atkPartnerAbility == ABILITY_PLUS || atkPartnerAbility == ABILITY_MINUS)
						{
							if ((STAT_STAGE(bankAtkPartner, STAT_STAGE_DEF) >= STAT_STAGE_MAX)
							&&  (STAT_STAGE(bankAtkPartner, STAT_STAGE_SPDEF) >= STAT_STAGE_MAX))
								DECREASE_VIABILITY(10);
						}
					}
					break;

				case MOVE_AROMATICMIST:
					if (!IS_DOUBLE_BATTLE
					|| gBattleMons[bankAtkPartner].hp == 0
					|| !STAT_CAN_RISE(bankAtkPartner, STAT_STAGE_SPDEF))
						DECREASE_VIABILITY(10);
					break;

				default:
					if (atkAbility == ABILITY_CONTRARY || !STAT_CAN_RISE(bankAtk, STAT_STAGE_DEF))
						DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_SPEED_UP:
		case EFFECT_SPEED_UP_2:
			if (atkAbility == ABILITY_CONTRARY || !STAT_CAN_RISE(bankAtk, STAT_STAGE_SPEED))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SPECIAL_ATTACK_UP:
		case EFFECT_SPECIAL_ATTACK_UP_2:
			switch(move) {
				case MOVE_GROWTH:
				case MOVE_WORKUP:
				AI_WORK_UP_CHECK: ;
					if (((!STAT_CAN_RISE(bankAtk,STAT_STAGE_ATK)|| !PhysicalMoveInMoveset(bankAtk))
					  && (!STAT_CAN_RISE(bankAtk, STAT_STAGE_SPATK) || !SpecialMoveInMoveset(bankAtk)))
					|| atkAbility == ABILITY_CONTRARY)
						DECREASE_VIABILITY(10);
					break;

				case MOVE_ROTOTILLER:
					if (IS_DOUBLE_BATTLE)
					{
						if (!(IsOfType(bankAtk, TYPE_GRASS)
							  && CheckGrounding(bankAtk)
							  && atkAbility != ABILITY_CONTRARY
							  && (STAT_CAN_RISE(bankAtk, STAT_STAGE_ATK) || STAT_CAN_RISE(bankAtk, STAT_STAGE_SPATK)))
						&&  !(IsOfType(bankAtkPartner, TYPE_GRASS)
							  && CheckGrounding(bankAtkPartner)
							  && atkPartnerAbility != ABILITY_CONTRARY
							  && (STAT_CAN_RISE(bankAtkPartner, STAT_STAGE_ATK) || STAT_CAN_RISE(bankAtkPartner, STAT_STAGE_SPATK))))
						{
							DECREASE_VIABILITY(10);
						}
					}
					else if (!(IsOfType(bankAtk, TYPE_GRASS)
						    && CheckGrounding(bankAtk)
						    && atkAbility != ABILITY_CONTRARY
						    && (STAT_CAN_RISE(bankAtk, STAT_STAGE_ATK) || STAT_CAN_RISE(bankAtk, STAT_STAGE_SPATK))))
					{
						DECREASE_VIABILITY(10);
					}
					break;

				case MOVE_GEARUP:
					if (atkAbility == ABILITY_PLUS || atkAbility == ABILITY_MINUS)
						goto AI_WORK_UP_CHECK;

					if (IS_DOUBLE_BATTLE)
					{
						if (atkPartnerAbility == ABILITY_PLUS || atkPartnerAbility == ABILITY_MINUS)
						{
							if ((!STAT_CAN_RISE(bankAtkPartner, STAT_STAGE_ATK) || !PhysicalMoveInMoveset(bankAtk))
							&&  (!STAT_CAN_RISE(bankAtkPartner, STAT_STAGE_SPATK) || !SpecialMoveInMoveset(bankAtk)))
								DECREASE_VIABILITY(10);
						}
					}
					break;

				default:
					if (atkAbility == ABILITY_CONTRARY || !STAT_CAN_RISE(bankAtk, STAT_STAGE_SPATK) || !SpecialMoveInMoveset(bankAtk))
						DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_SPECIAL_DEFENSE_UP:
		case EFFECT_SPECIAL_DEFENSE_UP_2:
		AI_SPDEF_RAISE_1: ;
			if (atkAbility == ABILITY_CONTRARY || !STAT_CAN_RISE(bankAtk, STAT_STAGE_SPDEF))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_ACCURACY_UP:
		case EFFECT_ACCURACY_UP_2:
			if (atkAbility == ABILITY_CONTRARY || !STAT_CAN_RISE(bankAtk, STAT_STAGE_ACC))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_EVASION_UP:
		case EFFECT_EVASION_UP_2:
		case EFFECT_MINIMIZE:
			switch (move) {
				case MOVE_ACUPRESSURE:
					if (StatsMaxed(bankDef) || defAbility == ABILITY_CONTRARY)
						DECREASE_VIABILITY(10);
					break;

				default:
					if (atkAbility == ABILITY_CONTRARY || !STAT_CAN_RISE(bankAtk, STAT_STAGE_EVASION))
						DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_ATTACK_DOWN:
		case EFFECT_ATTACK_DOWN_2:
			decreased = FALSE;
			switch (move) {
				case MOVE_VENOMDRENCH:
					if (!(defStatus1 & STATUS_PSN_ANY))
					{
						DECREASE_VIABILITY(10);
						decreased = TRUE;
						break;
					}
					//Poisoned target
					else if (STAT_STAGE(bankDef, STAT_STAGE_SPEED) == STAT_STAGE_MIN
						 && (STAT_STAGE(bankDef, STAT_STAGE_SPATK) == STAT_STAGE_MIN || !SpecialMoveInMoveset(bankDef))
						 && (STAT_STAGE(bankDef, STAT_STAGE_ATK) == STAT_STAGE_MIN || !PhysicalMoveInMoveset(bankDef)))
					{
						DECREASE_VIABILITY(10);
						decreased = TRUE;
					}
					break;

				case MOVE_PLAYNICE:
				case MOVE_NOBLEROAR:
				case MOVE_TEARFULLOOK:
					if ((STAT_STAGE(bankDef, STAT_STAGE_SPATK) == STAT_STAGE_MIN || !SpecialMoveInMoveset(bankDef))
					&&  (STAT_STAGE(bankDef, STAT_STAGE_ATK) == STAT_STAGE_MIN || !PhysicalMoveInMoveset(bankDef)))
					{
						DECREASE_VIABILITY(10);
						decreased = TRUE;
					}
					break;

				default:
					if (STAT_STAGE(bankDef, STAT_STAGE_ATK) == STAT_STAGE_MIN || !PhysicalMoveInMoveset(bankDef))
					{
						DECREASE_VIABILITY(10);
						decreased = TRUE;
					}
			}
			if (decreased) break;

		AI_SUBSTITUTE_CHECK:
			if (MoveBlockedBySubstitute(move, bankAtk, bankDef))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_DEFENSE_DOWN:
		case EFFECT_DEFENSE_DOWN_2:
			if (STAT_STAGE(bankDef, STAT_STAGE_DEF) == STAT_STAGE_MIN
			|| MoveBlockedBySubstitute(move, bankAtk, bankDef))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SPEED_DOWN:
		case EFFECT_SPEED_DOWN_2:
			if (STAT_STAGE(bankDef, STAT_STAGE_SPEED) == STAT_STAGE_MIN
			|| MoveBlockedBySubstitute(move, bankAtk, bankDef))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SPECIAL_ATTACK_DOWN:
		case EFFECT_SPECIAL_ATTACK_DOWN_2:
			if (move == MOVE_CAPTIVATE
			&& (atkGender == MON_GENDERLESS || defGender == MON_GENDERLESS || atkGender == defGender))
				DECREASE_VIABILITY(10);
			else if (STAT_STAGE(bankDef, STAT_STAGE_SPATK) == STAT_STAGE_MIN || !SpecialMoveInMoveset(bankDef)
			|| MoveBlockedBySubstitute(move, bankAtk, bankDef))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SPECIAL_DEFENSE_DOWN:
		case EFFECT_SPECIAL_DEFENSE_DOWN_2:
			if (STAT_STAGE(bankDef, STAT_STAGE_SPDEF) == STAT_STAGE_MIN
			|| MoveBlockedBySubstitute(move, bankAtk, bankDef))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_ACCURACY_DOWN:
		case EFFECT_ACCURACY_DOWN_2:
			if (STAT_STAGE(bankDef, STAT_STAGE_ACC) == STAT_STAGE_MIN
			|| MoveBlockedBySubstitute(move, bankAtk, bankDef))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_EVASION_DOWN:
		case EFFECT_EVASION_DOWN_2:
		AI_LOWER_EVASION:
			if (STAT_STAGE(bankDef, STAT_STAGE_EVASION) == STAT_STAGE_MIN
			|| MoveBlockedBySubstitute(move, bankAtk, bankDef))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_HAZE:
		AI_HAZE_CHECK: ;
			decreased = FALSE;
			//Don't want to reset own high stats
			for (i = 0; i <= BATTLE_STATS_NO-1; ++i)
			{
				if (STAT_STAGE(bankAtk, i) > 6 || STAT_STAGE(bankAtkPartner, i) > 6)
				{
					DECREASE_VIABILITY(10);
					decreased = TRUE;
					break;
				}
			}
			if (decreased)
				break;

			//Don't want to reset enemy lowered stats
			for (i = 0; i <= BATTLE_STATS_NO-1; ++i)
			{
				if (STAT_STAGE(bankDef, i) < 6 || STAT_STAGE(bankDefPartner, i) < 6)
				{
					DECREASE_VIABILITY(10);
					break;
				}
			}

			if (PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_BIDE:
			if (!DamagingMoveInMoveset(bankDef)
			||  GetHealthPercentage(bankAtk) < 30 //Close to death
			||  defStatus1 & (STATUS1_SLEEP | STATUS1_FREEZE)) //No point in biding if can't take damage
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_ROAR:
			if (PARTNER_MOVE_EFFECT_IS_SAME)
			{
				DECREASE_VIABILITY(10);
				break; //Don't blow out the same Pokemon twice
			}

			//Don't blow out a Pokemon that'll faint this turn or is taking
			//bad secondary damage.
			if (WillFaintFromSecondaryDamage(bankDef)
			||  GetLeechSeedDamage(bankDef) > 0
			||  GetNightmareDamage(bankDef) > 0
			||  GetCurseDamage(bankDef) > 0
			||  GetTrapDamage(bankDef) > 0
			||  GetPoisonDamage(bankDef) >= gBattleMons[bankDef].maxHP / 4)
				DECREASE_VIABILITY(10);

			switch (move) {
				case MOVE_DRAGONTAIL:
				case MOVE_CIRCLETHROW:
					goto AI_STANDARD_DAMAGE;

				default:
					if (IS_DOUBLE_BATTLE)
					{
						if (ViableMonCountFromBankLoadPartyRange(bankDef) <= 2
						||  defAbility == ABILITY_SUCTIONCUPS
						||  defStatus3 & STATUS3_ROOTED)
							DECREASE_VIABILITY(10);
					}
					else
					{
						if (ViableMonCountFromBankLoadPartyRange(bankDef) <= 1
						||  defAbility == ABILITY_SUCTIONCUPS
						||  defStatus3 & STATUS3_ROOTED)
							DECREASE_VIABILITY(10);
					}
			}
			break;

		case EFFECT_CONVERSION:
			//Check first move type
			if (IsOfType(bankAtk, gBattleMoves[gBattleMons[bankAtk].moves[0]].type))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_RESTORE_HP:
		case EFFECT_REST:
		case EFFECT_MORNING_SUN:
		AI_RECOVERY:
			switch (move) {
				case MOVE_PURIFY:
					if (!(defStatus1 & STATUS_ANY)
					|| MoveBlockedBySubstitute(move, bankAtk, bankDef))
					{
						DECREASE_VIABILITY(10);
						break;
					}
					else if (bankDef == bankAtkPartner)
						break; //Always heal your ally
					else if (GetHealthPercentage(bankAtk) == 100)
						DECREASE_VIABILITY(10);
					else if (GetHealthPercentage(bankAtk) >= 90)
						DECREASE_VIABILITY(8); //No point in healing, but should at least do it if nothing better
					break;

				default:
					if (GetHealthPercentage(bankAtk) == 100)
						DECREASE_VIABILITY(10);
					else if (GetHealthPercentage(bankAtk) >= 90)
						DECREASE_VIABILITY(9); //No point in healing, but should at least do it if nothing better
			}
			break;

		case EFFECT_POISON:
		case EFFECT_TOXIC:
			if (move == MOVE_TOXICTHREAD
			&& STAT_STAGE(bankDef, STAT_STAGE_SPEED) > STAT_STAGE_MIN
			&& defAbility != ABILITY_CONTRARY)
				break;

			if (AI_SpecialTypeCalc(move, bankAtk, bankDef) & MOVE_RESULT_NO_EFFECT)
			{
				DECREASE_VIABILITY(10);
				break;
			}

		AI_POISON_CHECK: ;
			if (!CanBePoisoned(bankDef, bankAtk, TRUE)
			|| MoveBlockedBySubstitute(move, bankAtk, bankDef)
			|| PARTNER_MOVE_EFFECT_IS_STATUS_SAME_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_LIGHT_SCREEN:
			if (gSideAffecting[SIDE(bankAtk)] & SIDE_STATUS_LIGHTSCREEN)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_0HKO:
			if (AI_SpecialTypeCalc(move, bankAtk, bankDef) & (MOVE_RESULT_NO_EFFECT | MOVE_RESULT_MISSED)
			|| (NO_MOLD_BREAKERS(atkAbility, move) && defAbility == ABILITY_STURDY))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_RECOIL_IF_MISS:
			if (atkAbility == ABILITY_MAGICGUARD)
				goto AI_STANDARD_DAMAGE;
			else if (moveAcc < 75)
				DECREASE_VIABILITY(6);
			break;

		case EFFECT_MIST:
			if (gSideAffecting[SIDE(bankAtk)] & SIDE_STATUS_MIST
			|| PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_FOCUS_ENERGY:
			if (atkStatus2 & STATUS2_FOCUS_ENERGY)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_RECOIL:
			if (atkAbility == ABILITY_MAGICGUARD || atkAbility == ABILITY_ROCKHEAD)
				goto AI_STANDARD_DAMAGE;

			u32 dmg = CalcFinalAIMoveDamage(move, bankAtk, bankDef, 1);

			if (CheckTableForMove(move, gPercent25RecoilMoves))
				dmg = MathMax(1, dmg / 4);
			else if (CheckTableForMove(move, gPercent33RecoilMoves))
				dmg = MathMax(1, dmg / 3);
			else if (CheckTableForMove(move, gPercent50RecoilMoves))
				dmg = MathMax(1, dmg / 2);
			else if (CheckTableForMove(move, gPercent66RecoilMoves))
				dmg = MathMax(1, (dmg * 2) / 3);
			else if (CheckTableForMove(move, gPercent75RecoilMoves))
				dmg = MathMax(1, (dmg * 3) / 4);
			else if (CheckTableForMove(move, gPercent100RecoilMoves))
				dmg = MathMax(1, dmg);
			else if (move == MOVE_MINDBLOWN)
			{
				if (MoveBlockedBySubstitute(move, bankAtk, bankDef))
				{
					DECREASE_VIABILITY(9);
					break; //Don't use Mind Blown to break a Substitute
				}

				dmg = MathMax(1, gBattleMons[bankAtk].maxHP / 2);
			}

			if (dmg >= gBattleMons[bankAtk].hp //Recoil kills attacker
			&&  ViableMonCountFromBank(bankDef) > 1) //Foe has more than 1 target left
			{
				if (dmg >= gBattleMons[bankDef].hp && !CanKnockOutWithoutMove(move, bankAtk, bankDef))
					break; //If it's the only KO move then just use it
				else
					DECREASE_VIABILITY(4); //Not as good to use move if you'll faint and not win
			}
			else
				goto AI_STANDARD_DAMAGE;
			break;

		case EFFECT_CONFUSE:
		AI_CONFUSE:
			switch (move) {
				case MOVE_TEETERDANCE: //Check if can affect either target
					if ((IsConfused(bankDef)
					  || (NO_MOLD_BREAKERS(atkAbility, move) && defAbility == ABILITY_OWNTEMPO)
					  || (CheckGrounding(bankDef) == GROUNDED && gTerrainType == MISTY_TERRAIN)
					  || (MoveBlockedBySubstitute(move, bankAtk, bankDef)))
					&&  (IsConfused(bankDefPartner)
					  || (NO_MOLD_BREAKERS(atkAbility, move) && defPartnerAbility == ABILITY_OWNTEMPO)
					  || (CheckGrounding(bankDefPartner) == GROUNDED && gTerrainType == MISTY_TERRAIN)
					  || (MoveBlockedBySubstitute(move, bankAtk, bankDefPartner))))
					{
						DECREASE_VIABILITY(10);
					}
					break;
				default:
					if (IsConfused(bankDef)
					|| (NO_MOLD_BREAKERS(atkAbility, move) && defAbility == ABILITY_OWNTEMPO)
					|| (CheckGrounding(bankDef) == GROUNDED && gTerrainType == MISTY_TERRAIN)
					|| (MoveBlockedBySubstitute(move, bankAtk, bankDef))
					|| PARTNER_MOVE_EFFECT_IS_SAME)
						DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_TRANSFORM:
			if (atkStatus2 & STATUS2_TRANSFORMED
			||  defStatus2 & (STATUS2_TRANSFORMED | STATUS2_SUBSTITUTE)) //Leave out Illusion b/c AI is supposed to be fooled
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_REFLECT:
			switch (move) {
				case MOVE_AURORAVEIL:
					if (gNewBS->AuroraVeilTimers[SIDE(bankAtk)]
					|| !(gBattleWeather & WEATHER_HAIL_ANY)
					|| PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
						DECREASE_VIABILITY(10);
					break;

				default:
					if (gSideAffecting[SIDE(bankAtk)] & SIDE_STATUS_REFLECT
					|| PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
						DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_PARALYZE:
		AI_PARALYZE_CHECK: ;
			if (!CanBeParalyzed(bankDef, TRUE)
			|| MoveBlockedBySubstitute(move, bankAtk, bankDef)
			|| PARTNER_MOVE_EFFECT_IS_STATUS_SAME_TARGET)
				DECREASE_VIABILITY(10);
			else if (move != MOVE_GLARE
				&& AI_SpecialTypeCalc(move, bankAtk, bankDef) & MOVE_RESULT_NO_EFFECT)
			{
				DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_RAZOR_WIND:
		case EFFECT_SKULL_BASH:
		case EFFECT_SKY_ATTACK:
			if (atkEffect == ITEM_EFFECT_POWER_HERB)
				goto AI_STANDARD_DAMAGE;

			if (CanKnockOut(bankDef, bankAtk) //Attacker can be knocked out
			&&  predictedMove != MOVE_NONE)
				DECREASE_VIABILITY(4);
			goto AI_STANDARD_DAMAGE;

		//Add check for sound move?
		case EFFECT_SUBSTITUTE:
			if (atkStatus2 & STATUS2_SUBSTITUTE
			|| GetHealthPercentage(bankAtk) <= 25)
				DECREASE_VIABILITY(10);
			else if (ABILITY(bankDef) == ABILITY_INFILTRATOR
			|| SoundMoveInMoveset(bankDef))
				DECREASE_VIABILITY(8);
			break;

		case EFFECT_RECHARGE:
			if (atkAbility != ABILITY_TRUANT
			&& MoveKnocksOutXHits(move, bankAtk, bankDef, 1)
			&& CanKnockOutWithoutMove(move, bankAtk, bankDef))
				DECREASE_VIABILITY(9); //Never use move as finisher if you don't have to
			break;

		case EFFECT_SPITE:
		case EFFECT_MIMIC:
			if (MoveWouldHitFirst(move, bankAtk, bankDef))
			{
				if (gLastUsedMoves[bankDef] == MOVE_NONE
				||  gLastUsedMoves[bankDef] == 0xFFFF)
					DECREASE_VIABILITY(10);
			}
			else if (predictedMove == MOVE_NONE)
				DECREASE_VIABILITY(10);
			goto AI_SUBSTITUTE_CHECK;

		case EFFECT_METRONOME:
			break;

		case EFFECT_LEECH_SEED:
			if (IsOfType(bankDef, TYPE_GRASS)
			|| defStatus3 & STATUS3_LEECHSEED
			|| defAbility == ABILITY_LIQUIDOOZE
			|| PARTNER_MOVE_EFFECT_IS_SAME)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_DISABLE:
			if (gDisableStructs[bankDef].disableTimer1 == 0
			&&  defEffect != ITEM_EFFECT_CURE_ATTRACT
			&& !PARTNER_MOVE_EFFECT_IS_SAME)
			{
				if (MoveWouldHitFirst(move, bankAtk, bankDef))
				{
					if (gLastUsedMoves[bankDef] == MOVE_NONE
					|| gLastUsedMoves[bankDef] == 0xFFFF)
						DECREASE_VIABILITY(10);
				}
				else if (predictedMove == MOVE_NONE)
					DECREASE_VIABILITY(10);
			}
			else
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_COUNTER:
		case EFFECT_MIRROR_COAT:
			if (SPLIT(predictedMove) == SPLIT_STATUS
			|| predictedMove == MOVE_NONE
			|| MoveBlockedBySubstitute(predictedMove, bankDef, bankAtk))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_ENCORE:
			if (gDisableStructs[bankDef].encoreTimer == 0
			&&  defEffect != ITEM_EFFECT_CURE_ATTRACT
			&& !PARTNER_MOVE_EFFECT_IS_SAME)
			{
				if (MoveWouldHitFirst(move, bankAtk, bankDef))
				{
					if (gLastUsedMoves[bankDef] == MOVE_NONE
					|| gLastUsedMoves[bankDef] == 0xFFFF)
						DECREASE_VIABILITY(10);
				}
				else if (predictedMove == MOVE_NONE)
					DECREASE_VIABILITY(10);
			}
			else
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_ENDEAVOR:
		case EFFECT_PAIN_SPLIT:
			if (gBattleMons[bankAtk].hp > (gBattleMons[bankAtk].hp + gBattleMons[bankDef].hp) / 2)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SNORE:
		case EFFECT_SLEEP_TALK:
			if (((atkStatus1 & STATUS_SLEEP) == 1 || !(atkStatus1 & STATUS_SLEEP))
			&& atkAbility != ABILITY_COMATOSE)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_CONVERSION_2:
			if (gNewBS->LastUsedTypes[bankDef] == 0)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_LOCK_ON:
			switch (move) {
				case MOVE_LASERFOCUS:
					if (IsLaserFocused(bankAtk))
						DECREASE_VIABILITY(10);
					else if (defAbility == ABILITY_SHELLARMOR || defAbility == ABILITY_BATTLEARMOR)
						DECREASE_VIABILITY(8);
					break;

				default: //Lock on
					if (atkStatus3 & STATUS3_LOCKON
					|| atkAbility == ABILITY_NOGUARD
					|| defAbility == ABILITY_NOGUARD
					|| PARTNER_MOVE_EFFECT_IS_SAME)
						DECREASE_VIABILITY(10);
					break;
			}
			break;

		case EFFECT_SKETCH:
			if (gLastUsedMoves[bankDef] == MOVE_NONE)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_DESTINY_BOND:
			if (gNewBS->DestinyBondCounters[bankAtk] != 0
			|| atkStatus2 & STATUS2_DESTINY_BOND)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_FALSE_SWIPE:
			if (MoveKnocksOutXHits(move, bankAtk, bankDef, 1)
			&&  CanKnockOutWithoutMove(move, bankAtk, bankDef))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_HEAL_BELL:
			if (move == MOVE_HEALBELL)
			{
				if (!PartyMemberStatused(bankAtk, TRUE) //Check Soundproof
				|| PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
					DECREASE_VIABILITY(10);
			}
			else if (!PartyMemberStatused(bankAtk, FALSE)
			|| PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_MEAN_LOOK:
			switch (move) {
				case MOVE_SPIRITSHACKLE:
				case MOVE_ANCHORSHOT:
					goto AI_STANDARD_DAMAGE;

				default: //Mean look
					if (IsTrapped(bankDef, TRUE)
					|| PARTNER_MOVE_EFFECT_IS_SAME)
						DECREASE_VIABILITY(10);
					break;
			}
			break;

		case EFFECT_NIGHTMARE:
			if (defStatus2 & STATUS2_NIGHTMARE
			|| !(defStatus1 & STATUS_SLEEP || defAbility == ABILITY_COMATOSE)
			|| PARTNER_MOVE_EFFECT_IS_SAME)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_CURSE:
			if (IsOfType(bankAtk, TYPE_GHOST))
			{
				if (defStatus2 & STATUS2_CURSED
				|| PARTNER_MOVE_EFFECT_IS_SAME)
					DECREASE_VIABILITY(10);
				else if (GetHealthPercentage(bankAtk) <= 50)
					DECREASE_VIABILITY(6);
			}
			else //Regular Curse
			{
				if (!STAT_CAN_RISE(bankAtk, STAT_STAGE_ATK)
				&& !STAT_CAN_RISE(bankAtk, STAT_STAGE_DEF)
				&& !STAT_CAN_FALL(bankAtk, STAT_STAGE_SPEED))
					DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_PROTECT:
			decreased = FALSE;
			switch (move) {
				case MOVE_QUICKGUARD:
				case MOVE_WIDEGUARD:
				case MOVE_CRAFTYSHIELD:
					if (!IS_DOUBLE_BATTLE)
					{
						DECREASE_VIABILITY(10);
						decreased = TRUE;
					}
					break;

				case MOVE_MATBLOCK:
					if (!gDisableStructs[bankAtk].isFirstTurn)
					{
						DECREASE_VIABILITY(10);
						decreased = TRUE;
					}
					break;

				case MOVE_ENDURE:
					if (gBattleMons[bankAtk].hp == 1 || IsTakingSecondaryDamage(bankAtk)) //Don't use Endure if you'll die after using it
					{
						DECREASE_VIABILITY(10);
						decreased = TRUE;
					}
					break;
			}
			if (decreased)
				break;

			if (gBattleMons[bankDef].status2 & STATUS2_RECHARGE)
			{
				DECREASE_VIABILITY(10);
				break;
			}

			if (gBattleMoves[gLastResultingMoves[bankAtk]].effect == EFFECT_PROTECT
			&&  move != MOVE_QUICKGUARD
			&&  move != MOVE_WIDEGUARD
			&&  move != MOVE_CRAFTYSHIELD) //These moves have infinite usage
			{
				if (WillFaintFromSecondaryDamage(bankAtk)
				&&  defAbility != ABILITY_MOXIE
				&&  defAbility != ABILITY_BEASTBOOST)
				{
					DECREASE_VIABILITY(10); //Don't protect if you're going to faint after protecting
				}
				else if (gDisableStructs[bankAtk].protectUses == 1 && Random() % 100 < 50)
				{
					if (IS_SINGLE_BATTLE)
						DECREASE_VIABILITY(6);
					else
						DECREASE_VIABILITY(10); //Don't try double protecting in doubles
				}
				else if (gDisableStructs[bankAtk].protectUses >= 2)
					DECREASE_VIABILITY(10);
			}

			if (AI_THINKING_STRUCT->aiFlags == AI_SCRIPT_CHECK_BAD_MOVE //Only basic AI
			&& IS_DOUBLE_BATTLE) //Make the regular AI know how to use Protect minimally in Doubles
			{
				u8 shouldProtect = ShouldProtect(bankAtk, bankDef, move);
				if (shouldProtect == USE_PROTECT || shouldProtect == PROTECT_FROM_FOES)
					IncreaseFoeProtectionViability(&viability, 0xFF, bankAtk, bankDef);
				else if (shouldProtect == PROTECT_FROM_ALLIES)
					IncreaseAllyProtectionViability(&viability, 0xFF);
			}
			break;

		case EFFECT_SPIKES:
			if (IS_DOUBLE_BATTLE)
			{
				if (ViableMonCountFromBank(bankDef) <= 2)
				{
					DECREASE_VIABILITY(10);
					break;
				}
			}
			else
			{
				if (ViableMonCountFromBank(bankDef) <= 1)
				{
					DECREASE_VIABILITY(10);
					break;
				}
			}

			switch (move) {
				case MOVE_STEALTHROCK:
					if (gSideTimers[SIDE(bankDef)].srAmount > 0)
						DECREASE_VIABILITY(10);
					else if (PARTNER_MOVE_IS_SAME_NO_TARGET)
						DECREASE_VIABILITY(10); //Only one mon needs to set up Stealth Rocks
					break;

				case MOVE_TOXICSPIKES:
					if (gSideTimers[SIDE(bankDef)].tspikesAmount >= 2)
						DECREASE_VIABILITY(10);
					else if (PARTNER_MOVE_IS_SAME_NO_TARGET && gSideTimers[SIDE(bankDef)].tspikesAmount == 1)
						DECREASE_VIABILITY(10); //Only one mon needs to set up the last layer of Toxic Spikes
					break;

				case MOVE_STICKYWEB:
					if (gSideTimers[SIDE(bankDef)].stickyWeb)
						DECREASE_VIABILITY(10);
					else if (PARTNER_MOVE_IS_SAME_NO_TARGET && gSideTimers[SIDE(bankDef)].stickyWeb)
						DECREASE_VIABILITY(10); //Only one mon needs to set up Sticky Web
					break;

				default: //Spikes
					if (gSideTimers[SIDE(bankDef)].spikesAmount >= 3)
						DECREASE_VIABILITY(10);
					else if (PARTNER_MOVE_IS_SAME_NO_TARGET && gSideTimers[SIDE(bankDef)].spikesAmount == 2)
						DECREASE_VIABILITY(10); //Only one mon needs to set up the last layer of Spikes
					break;
			}
			break;

		case EFFECT_FORESIGHT:
			switch (move) {
				case MOVE_MIRACLEEYE:
					if (defStatus3 & STATUS3_MIRACLE_EYED)
						DECREASE_VIABILITY(10);

					if (STAT_STAGE(bankDef, STAT_STAGE_EVASION) <= 4
					|| !(IsOfType(bankDef, TYPE_DARK))
					|| PARTNER_MOVE_EFFECT_IS_SAME)
						DECREASE_VIABILITY(9);
					break;

				default: //Foresight
					if (defStatus2 & STATUS2_FORESIGHT)
						DECREASE_VIABILITY(10);
					else if (STAT_STAGE(bankDef, STAT_STAGE_EVASION) <= 4
						|| !(IsOfType(bankDef, TYPE_GHOST))
						|| PARTNER_MOVE_EFFECT_IS_SAME)
					{
						DECREASE_VIABILITY(9);
					}
					break;
			}
			break;

		case EFFECT_PERISH_SONG:
			if (IS_DOUBLE_BATTLE)
			{
				if (ViableMonCountFromBank(bankAtk) <= 2
				&&  atkAbility != ABILITY_SOUNDPROOF
				&&  atkPartnerAbility != ABILITY_SOUNDPROOF
				&&  ViableMonCountFromBank(FOE(bankAtk)) >= 3)
					DECREASE_VIABILITY(10); //Don't wipe your team if you're going to lose

				else if ((!BATTLER_ALIVE(FOE(bankAtk)) || ABILITY(FOE(bankAtk)) == ABILITY_SOUNDPROOF || gStatuses3[FOE(bankAtk)] & STATUS3_PERISH_SONG)
				&&  (!BATTLER_ALIVE(PARTNER(FOE(bankAtk))) || ABILITY(PARTNER(FOE(bankAtk))) == ABILITY_SOUNDPROOF || gStatuses3[PARTNER(FOE(bankAtk))] & STATUS3_PERISH_SONG))
					DECREASE_VIABILITY(10); //Both enemies are perish songed

				else if (PARTNER_MOVE_EFFECT_IS_SAME)
					DECREASE_VIABILITY(10);
			}
			else
			{
				if (ViableMonCountFromBank(bankAtk) == 1
				&&  atkAbility != ABILITY_SOUNDPROOF
				&&  ViableMonCountFromBank(bankDef) >= 2)
					DECREASE_VIABILITY(10);

				if (gStatuses3[FOE(bankAtk)] & STATUS3_PERISH_SONG || ABILITY(FOE(bankAtk)) == ABILITY_SOUNDPROOF)
					DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_SANDSTORM:
			if (gBattleWeather & (WEATHER_SANDSTORM_ANY | WEATHER_PRIMAL_ANY | WEATHER_CIRCUS)
			|| PARTNER_MOVE_EFFECT_IS_WEATHER)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SWAGGER:
			if (bankDef == bankAtkPartner)
			{
				if (defAbility == ABILITY_CONTRARY)
					DECREASE_VIABILITY(10);
			}
			else
				goto AI_CONFUSE;
			break;

		case EFFECT_ATTRACT:
			if (defAbility == ABILITY_OBLIVIOUS || (defStatus2 & STATUS2_INFATUATION)
			||  defGender == atkGender || atkGender == MON_GENDERLESS || defGender == MON_GENDERLESS
			|| PARTNER_MOVE_EFFECT_IS_SAME)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SAFEGUARD:
			if (gSideAffecting[SIDE(bankAtk)] & SIDE_STATUS_SAFEGUARD
			|| PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_BURN_UP:
			if (!IsOfType(bankAtk, TYPE_FIRE))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_BATON_PASS:
			if (move == MOVE_UTURN || move == MOVE_VOLTSWITCH)
			{
				goto AI_STANDARD_DAMAGE;
			}
			else if (ViableMonCountFromBankLoadPartyRange(bankAtk) <= 1)
			{
				DECREASE_VIABILITY(10);
				break;
			}
			else //Baton pass
			{
				//Check Substitute, Aqua Ring, Magnet Rise, Ingrain, and stats
				if (atkStatus2 & STATUS2_SUBSTITUTE
				|| (atkStatus3 & (STATUS3_ROOTED | STATUS3_AQUA_RING | STATUS3_LEVITATING | STATUS3_POWER_TRICK))
				|| AnyStatIsRaised(bankAtk))
					break;

				DECREASE_VIABILITY(6);
				break;
			}
			break;

		case EFFECT_RAPID_SPIN:
			if (move == MOVE_DEFOG)
			{
				if (gSideAffecting[SIDE(bankDef)] & (SIDE_STATUS_REFLECT | SIDE_STATUS_SAFEGUARD | SIDE_STATUS_MIST)
				|| gNewBS->AuroraVeilTimers[SIDE(bankDef)] != 0
				|| gSideAffecting[SIDE(bankAtk)] & SIDE_STATUS_SPIKES)
				{
					if (PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
					{
						DECREASE_VIABILITY(10); //Only need one hazards removal
						break;
					}
				}

				if (gSideAffecting[SIDE(bankDef)] & SIDE_STATUS_SPIKES)
				{
					DECREASE_VIABILITY(10); //Don't blow away opposing spikes
					break;
				}

				if (IS_DOUBLE_BATTLE)
				{
					if (gBattleMoves[partnerMove].effect == EFFECT_SPIKES //Partner is going to set up hazards
					&& !MoveWouldHitBeforeOtherMove(move, bankAtk, partnerMove, bankAtkPartner)) //Partner is going to set up before the potential Defog
					{
						DECREASE_VIABILITY(10);
						break; //Don't use Defog if partner is going to set up hazards
					}
				}

				goto AI_LOWER_EVASION;
			}
			else if ((atkStatus2 & STATUS2_WRAPPED) || (atkStatus3 & STATUS3_LEECHSEED))
				goto AI_STANDARD_DAMAGE;

			//Spin checks
			if (!(gSideAffecting[SIDE(bankAtk)] & SIDE_STATUS_SPIKES))
				DECREASE_VIABILITY(6);
			break;

		case EFFECT_RAIN_DANCE:
			if (gBattleWeather & (WEATHER_RAIN_ANY | WEATHER_PRIMAL_ANY | WEATHER_CIRCUS)
			|| PARTNER_MOVE_EFFECT_IS_WEATHER)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SUNNY_DAY:
			if (gBattleWeather & (WEATHER_SUN_ANY | WEATHER_PRIMAL_ANY | WEATHER_CIRCUS)
			|| PARTNER_MOVE_EFFECT_IS_WEATHER)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_BELLY_DRUM:
			if (atkAbility == ABILITY_CONTRARY)
				DECREASE_VIABILITY(10);
			else if (GetHealthPercentage(bankAtk) <= 50)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_PSYCH_UP:
			if (move == MOVE_SPECTRALTHIEF)
				goto AI_STANDARD_DAMAGE;
			else
				goto AI_HAZE_CHECK;
			break;

		case EFFECT_FUTURE_SIGHT:
			if (gWishFutureKnock->futureSightCounter[bankAtk] != 0)
				DECREASE_VIABILITY(10);
			else
				goto AI_STANDARD_DAMAGE;
			break;

		case EFFECT_SOLARBEAM:
			if (atkEffect == ITEM_EFFECT_POWER_HERB
			|| (WEATHER_HAS_EFFECT && gBattleWeather & WEATHER_SUN_ANY))
				goto AI_STANDARD_DAMAGE;

			if (CanKnockOut(bankDef, bankAtk)) //Attacker can be knocked out
				DECREASE_VIABILITY(4);

			goto AI_STANDARD_DAMAGE;

		case EFFECT_SEMI_INVULNERABLE: ;
			if (predictedMove != MOVE_NONE
			&& MoveWouldHitFirst(move, bankAtk, bankDef)
			&& gBattleMoves[predictedMove].effect == EFFECT_SEMI_INVULNERABLE)
				DECREASE_VIABILITY(10); //Don't Fly if opponent is going to fly after you

			if (WillFaintFromWeatherSoon(bankAtk)
			&& (move == MOVE_FLY || move == MOVE_BOUNCE))
				DECREASE_VIABILITY(10); //Attacker will faint while in the air

			goto AI_STANDARD_DAMAGE;

		case EFFECT_FAKE_OUT:
			if (!gDisableStructs[bankAtk].isFirstTurn)
				DECREASE_VIABILITY(10);
			else if (move == MOVE_FAKEOUT)
			{
				if (ITEM_EFFECT(bankAtk) == ITEM_EFFECT_CHOICE_BAND
				&& (ViableMonCountFromBank(bankDef) >= 2 || !MoveKnocksOutXHits(MOVE_FAKEOUT, bankAtk, bankDef, 1)))
				{
					if (IS_DOUBLE_BATTLE)
					{
						if (ViableMonCountFromBankLoadPartyRange(bankAtk) <= 2)
							DECREASE_VIABILITY(10); //Don't lock the attacker into Fake Out if they can't switch out afterwards.
					}
					else //Single Battle
					{
						if (ViableMonCountFromBank(bankAtk) <= 1)
							DECREASE_VIABILITY(10); //Don't lock the attacker into Fake Out if they can't switch out afterwards.
					}
				}
			}

			goto AI_STANDARD_DAMAGE;

		case EFFECT_STOCKPILE:
			if (gDisableStructs[bankAtk].stockpileCounter >= 3)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SPIT_UP:
			if (gDisableStructs[bankAtk].stockpileCounter == 0)
				DECREASE_VIABILITY(10);
			else
				goto AI_STANDARD_DAMAGE;
			break;

		case EFFECT_SWALLOW:
			if (gDisableStructs[bankAtk].stockpileCounter == 0)
				DECREASE_VIABILITY(10);
			else
				goto AI_RECOVERY;
			break;

		case EFFECT_HAIL:
			if (gBattleWeather & (WEATHER_HAIL_ANY | WEATHER_PRIMAL_ANY | WEATHER_CIRCUS)
			|| PARTNER_MOVE_EFFECT_IS_WEATHER)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_TORMENT:
			if (IsTormented(bankDef)
			|| PARTNER_MOVE_EFFECT_IS_SAME)
			{
				DECREASE_VIABILITY(10);
				break;
			}
			if (defEffect == ITEM_EFFECT_CURE_ATTRACT)
				DECREASE_VIABILITY(6);
			goto AI_SUBSTITUTE_CHECK;

		case EFFECT_FLATTER:
			if (bankDef == bankAtkPartner)
			{
				if (defAbility == ABILITY_CONTRARY)
					DECREASE_VIABILITY(10);
			}
			else
				goto AI_CONFUSE;
			break;

		case EFFECT_WILL_O_WISP:
		AI_BURN_CHECK: ;
			if (!CanBeBurned(bankDef, TRUE)
			|| MoveBlockedBySubstitute(move, bankAtk, bankDef)
			|| PARTNER_MOVE_EFFECT_IS_STATUS_SAME_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_MEMENTO:
			if (ViableMonCountFromBankLoadPartyRange(bankAtk) <= 1
			|| PARTNER_MOVE_EFFECT_IS_SAME)
			{
				DECREASE_VIABILITY(10);
				break;
			}
			switch (move) {
				case MOVE_HEALINGWISH:
				case MOVE_LUNARDANCE:
					if (TeamFullyHealedMinusBank(bankAtk))
						DECREASE_VIABILITY(10);
					break;

				case MOVE_FINALGAMBIT:
					//Just the viablemonfromcount check, but not stat check
					break;

				default: //Memento
					if (MoveBlockedBySubstitute(move, bankAtk, bankDef))
						DECREASE_VIABILITY(10);
					else if (STAT_STAGE(bankDef, STAT_STAGE_ATK) == STAT_STAGE_MIN
						  && STAT_STAGE(bankDef, STAT_STAGE_SPATK) == STAT_STAGE_MIN)
					{
						DECREASE_VIABILITY(10);
					}
					break;
			}
			break;

		case EFFECT_FOCUS_PUNCH: ;
			switch (move) {
				case MOVE_SHELLTRAP: ;
					if (!CheckContact(predictedMove, bankDef))
						DECREASE_VIABILITY(10); //Probably better not to use it
					break;

				case MOVE_BEAKBLAST:
					break;

				default:
					if (predictedMove != MOVE_NONE
					&& !MoveBlockedBySubstitute(predictedMove, bankDef, bankAtk)
					&& SPLIT(predictedMove) != SPLIT_STATUS
					&& gBattleMoves[predictedMove].power != 0)
						DECREASE_VIABILITY(10); //Probably better not to use it
			}
			break;

		case EFFECT_NATURE_POWER:
			return AI_Script_Negatives(bankAtk, bankDef, GetNaturePowerMove(), originalViability);

		case EFFECT_CHARGE:
			if (atkStatus3 & STATUS3_CHARGED_UP)
			{
				DECREASE_VIABILITY(10);
				break;
			}

			if (!MoveTypeInMoveset(bankAtk, TYPE_ELECTRIC))
				goto AI_SPDEF_RAISE_1;
			break;

		case EFFECT_TAUNT:
			if (IsTaunted(bankDef)
			|| PARTNER_MOVE_EFFECT_IS_SAME)
				DECREASE_VIABILITY(1);
			break;

		case EFFECT_FOLLOW_ME:
		case EFFECT_HELPING_HAND:
			if (!IS_DOUBLE_BATTLE
			||  !BATTLER_ALIVE(bankAtkPartner)
			||  PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET
			||  (partnerMove != MOVE_NONE && SPLIT(partnerMove) == SPLIT_STATUS)
			||  gBattleStruct->monToSwitchIntoId[bankAtkPartner] != PARTY_SIZE) //Partner is switching out.
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_TRICK:
			switch (move) {
				case MOVE_BESTOW:
					if (atkItem == ITEM_NONE
					|| !CanTransferItem(atkSpecies, atkItem))
						DECREASE_VIABILITY(10);
					break;

				default: //Trick
					if ((atkItem == ITEM_NONE && defItem == ITEM_NONE)
					|| !CanTransferItem(atkSpecies, atkItem)
					|| !CanTransferItem(atkSpecies, defItem)
					|| !CanTransferItem(defSpecies, atkItem)
					|| !CanTransferItem(defSpecies, defItem)
					|| (defAbility == ABILITY_STICKYHOLD))
						DECREASE_VIABILITY(10);
					break;
			}
			break;

		case EFFECT_ROLE_PLAY:
			atkAbility = *GetAbilityLocation(bankAtk);
			defAbility = *GetAbilityLocation(bankDef);

			if (atkAbility == defAbility
			||  defAbility == ABILITY_NONE
			||  CheckTableForAbility(atkAbility, gRolePlayAttackerBannedAbilities)
			||  CheckTableForAbility(defAbility, gRolePlayBannedAbilities))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_WISH:
			if (gWishFutureKnock->wishCounter[bankAtk] != 0)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_ASSIST: ;
			u8 i, firstMonId, lastMonId;
			struct Pokemon* party = LoadPartyRange(bankAtk, &firstMonId, &lastMonId);

			for (i = firstMonId; i < lastMonId; ++i)
			{
				if (party[i].species != SPECIES_NONE
				&& !GetMonData(&party[i], MON_DATA_IS_EGG, NULL)
				&& gBattlerPartyIndexes[i] != gBattlerPartyIndexes[bankAtk])
					break; //At least one other Pokemon on team
			}

			if (i == lastMonId)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_INGRAIN:
			switch (move) {
				case MOVE_AQUARING:
					if (atkStatus3 & STATUS3_AQUA_RING)
						DECREASE_VIABILITY(10);
					break;

				default:
					if (atkStatus3 & STATUS3_ROOTED)
						DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_SUPERPOWER:
			if (move == MOVE_HYPERSPACEFURY && atkSpecies != SPECIES_HOOPA_UNBOUND)
				DECREASE_VIABILITY(10);
			break;

			break;

		case EFFECT_MAGIC_COAT:
			if (!MagicCoatableMovesInMoveset(bankDef))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_RECYCLE:
			if (move == MOVE_BELCH)
			{
				if (!(gNewBS->BelchCounters & gBitTable[gBattlerPartyIndexes[bankAtk]]))
					DECREASE_VIABILITY(10);
				else
					goto AI_STANDARD_DAMAGE;
			}
			else if (gNewBS->SavedConsumedItems[bankAtk] == ITEM_NONE || atkItem != ITEM_NONE)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_YAWN:
			if (defStatus3 & STATUS3_YAWN)
				DECREASE_VIABILITY(10);
			else
				goto AI_CHECK_SLEEP;
			break;

		case EFFECT_KNOCK_OFF:
			if (defEffect == ITEM_EFFECT_ASSAULT_VEST
			|| (defEffect == ITEM_EFFECT_CHOICE_BAND && gBattleStruct->choicedMove[bankDef]))
			{
				if (GetStrongestMove(bankDef, bankAtk) == MOVE_NONE
				|| AI_SpecialTypeCalc(GetStrongestMove(bankDef, bankAtk), bankDef, bankAtk) & (MOVE_RESULT_NO_EFFECT | MOVE_RESULT_MISSED))
					DECREASE_VIABILITY(9); //Don't use Knock Off is the enemy's only moves don't affect the AI
			}
			break;

		case EFFECT_SKILL_SWAP:
			atkAbility = *GetAbilityLocation(bankAtk); //Get actual abilities
			defAbility = *GetAbilityLocation(bankDef);

			switch (move) {
				case MOVE_WORRYSEED:
					if (defAbility == ABILITY_INSOMNIA
					|| CheckTableForAbility(defAbility, gWorrySeedGastroAcidBannedAbilities)
					|| MoveBlockedBySubstitute(move, bankAtk, bankDef))
						DECREASE_VIABILITY(10);
					break;

				case MOVE_GASTROACID:
					if (IsAbilitySuppressed(bankDef)
					||  CheckTableForAbility(defAbility, gWorrySeedGastroAcidBannedAbilities)
					||  MoveBlockedBySubstitute(move, bankAtk, bankDef))
						DECREASE_VIABILITY(10);
					break;

				case MOVE_ENTRAINMENT:
					if (atkAbility == ABILITY_NONE
					||  CheckTableForAbility(atkAbility, gEntrainmentBannedAbilitiesAttacker)
					||  CheckTableForAbility(defAbility, gEntrainmentBannedAbilitiesTarget)
					||  MoveBlockedBySubstitute(move, bankAtk, bankDef))
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_COREENFORCER:
					goto AI_STANDARD_DAMAGE;

				case MOVE_SIMPLEBEAM:
					if (defAbility == ABILITY_SIMPLE
					||  CheckTableForAbility(defAbility, gSimpleBeamBannedAbilities)
					||  MoveBlockedBySubstitute(move, bankAtk, bankDef))
						DECREASE_VIABILITY(10);
					break;

				default: //Skill Swap
					if (atkAbility == ABILITY_NONE || defAbility == ABILITY_NONE
					|| CheckTableForAbility(atkAbility, gSkillSwapBannedAbilities)
					|| CheckTableForAbility(defAbility, gSkillSwapBannedAbilities))
						DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_IMPRISON:
			if (atkStatus3 & STATUS3_IMPRISONED)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_REFRESH:
			if (!(atkStatus1 & (STATUS1_PSN_ANY | STATUS1_BURN | STATUS1_PARALYSIS)))
			{
				DECREASE_VIABILITY(10);
				break;
			}
			else if (move == MOVE_PSYCHOSHIFT)
			{
				if (atkStatus1 & STATUS1_PSN_ANY)
					goto AI_POISON_CHECK;
				else if (atkStatus1 & STATUS1_BURN)
					goto AI_BURN_CHECK;
				else if (atkStatus1 & STATUS1_PARALYSIS)
					goto AI_PARALYZE_CHECK;
				else if (atkStatus1 & STATUS1_SLEEP)
					goto AI_CHECK_SLEEP;
				else
					DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_SNATCH:
			//Check target for any snatchable moves
			if (!HasSnatchableMove(bankDef)
			|| PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_MUD_SPORT:
			if (IsMudSportActive()
			|| PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_TICKLE:
			if (defAbility == ABILITY_CONTRARY)
			{
				DECREASE_VIABILITY(10);
				break;
			}
			else
			{
				if ((STAT_STAGE(bankDef, STAT_STAGE_ATK) == STAT_STAGE_MIN || !PhysicalMoveInMoveset(bankDef))
				&&  STAT_STAGE(bankDef, STAT_STAGE_DEF) == STAT_STAGE_MIN)
				{
					DECREASE_VIABILITY(10);
					break;
				}
			}
			goto AI_SUBSTITUTE_CHECK;

		case EFFECT_COSMIC_POWER:
			if (atkAbility == ABILITY_CONTRARY)
				DECREASE_VIABILITY(10);
			else
			{
				AI_COSMIC_POWER:
				if (STAT_STAGE(bankAtk, STAT_STAGE_DEF) >= STAT_STAGE_MAX
				&& STAT_STAGE(bankAtk, STAT_STAGE_SPDEF) >= STAT_STAGE_MAX)
					DECREASE_VIABILITY(10);
			}
			break;

		case EFFECT_BULK_UP:
			if (atkAbility == ABILITY_CONTRARY)
				DECREASE_VIABILITY(10);
			else
			{
				switch (move) {
					case MOVE_COIL:
						if (STAT_STAGE(bankAtk, STAT_STAGE_ACC) >= STAT_STAGE_MAX
						&& (STAT_STAGE(bankAtk, STAT_STAGE_ATK) >= STAT_STAGE_MAX && !PhysicalMoveInMoveset(bankAtk))
						&&  STAT_STAGE(bankAtk, STAT_STAGE_DEF) >= STAT_STAGE_MAX)
							DECREASE_VIABILITY(10);
						break;

					default:
						if ((STAT_STAGE(bankAtk, STAT_STAGE_ATK) >= STAT_STAGE_MAX && !PhysicalMoveInMoveset(bankAtk))
						&&  STAT_STAGE(bankAtk, STAT_STAGE_DEF) >= STAT_STAGE_MAX)
							DECREASE_VIABILITY(10);
				}
			}
			break;

		case EFFECT_WATER_SPORT:
			if (IsWaterSportActive()
			|| PARTNER_MOVE_EFFECT_IS_SAME_NO_TARGET)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_CALM_MIND:
			if (atkAbility == ABILITY_CONTRARY)
					DECREASE_VIABILITY(10);
			else
			{
				switch (move) {
					case MOVE_QUIVERDANCE:
					case MOVE_GEOMANCY:
						if (STAT_STAGE(bankAtk, STAT_STAGE_SPEED) >= STAT_STAGE_MAX
						&& (STAT_STAGE(bankAtk, STAT_STAGE_SPATK) >= STAT_STAGE_MAX || !SpecialMoveInMoveset(bankAtk))
						&&  STAT_STAGE(bankAtk, STAT_STAGE_SPDEF) >= STAT_STAGE_MAX)
							DECREASE_VIABILITY(10);
						break;

					default:
						if ((STAT_STAGE(bankAtk, STAT_STAGE_SPATK) >= STAT_STAGE_MAX || !SpecialMoveInMoveset(bankAtk))
						&&  STAT_STAGE(bankAtk, STAT_STAGE_SPDEF) >= STAT_STAGE_MAX)
							DECREASE_VIABILITY(10);
				}
			}
			break;

		case EFFECT_DRAGON_DANCE:
			switch (move) {
				case MOVE_SHELLSMASH:
					if (atkAbility == ABILITY_CONTRARY)
						goto AI_COSMIC_POWER;

					if ((STAT_STAGE(bankAtk, STAT_STAGE_SPATK) >= STAT_STAGE_MAX || !SpecialMoveInMoveset(bankAtk))
					&&  (STAT_STAGE(bankAtk, STAT_STAGE_ATK) >= STAT_STAGE_MAX || !PhysicalMoveInMoveset(bankAtk))
					&&  (STAT_STAGE(bankAtk, STAT_STAGE_SPEED) >= STAT_STAGE_MAX))
						DECREASE_VIABILITY(10);
					break;

				default: //Dragon Dance + Shift Gear
					if (atkAbility == ABILITY_CONTRARY)
						DECREASE_VIABILITY(10);
					else
					{
						if ((STAT_STAGE(bankAtk, STAT_STAGE_ATK) >= STAT_STAGE_MAX || !PhysicalMoveInMoveset(bankAtk))
						&&  (STAT_STAGE(bankAtk, STAT_STAGE_SPEED) >= STAT_STAGE_MAX))
							DECREASE_VIABILITY(10);
					}
			}
			break;

		case EFFECT_STAT_SWAP_SPLIT:
			if (bankDef == bankAtkPartner)
				break;

			switch (move) {
				case MOVE_POWERTRICK:
					if (gBattleMons[bankAtk].defense >= gBattleMons[bankAtk].attack && !PhysicalMoveInMoveset(bankAtk))
						DECREASE_VIABILITY(10);
					break;

				case MOVE_POWERSWAP: //Don't use if attacker's stat stages are higher than opponents
					if (STAT_STAGE(bankAtk, STAT_STAGE_ATK) >= STAT_STAGE(bankDef, STAT_STAGE_SPATK)
					&&  STAT_STAGE(bankAtk, STAT_STAGE_SPATK) >= STAT_STAGE(bankDef, STAT_STAGE_SPATK))
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_GUARDSWAP: //Don't use if attacker's stat stages are higher than opponents
					if (STAT_STAGE(bankAtk, STAT_STAGE_DEF) >= STAT_STAGE(bankDef, STAT_STAGE_SPDEF)
					&&  STAT_STAGE(bankAtk, STAT_STAGE_SPDEF) >= STAT_STAGE(bankDef, STAT_STAGE_SPDEF))
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_SPEEDSWAP:
					if (IsTrickRoomActive())
					{
						if (gBattleMons[bankAtk].speed <= gBattleMons[bankDef].speed)
							DECREASE_VIABILITY(10);
					}
					else if (gBattleMons[bankAtk].speed >= gBattleMons[bankDef].speed)
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_HEARTSWAP: ;
					u8 attackerPositiveStages = CountBanksPositiveStatStages(bankAtk);
					u8 attackerNegativeStages = CountBanksNegativeStatStages(bankAtk);
					u8 targetPositiveStages = CountBanksPositiveStatStages(bankDef);
					u8 targetNegativeStages = CountBanksNegativeStatStages(bankDef);

					if (attackerPositiveStages >= targetPositiveStages
					&&  attackerNegativeStages <= targetNegativeStages)
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_POWERSPLIT: ;
					u8 atkAttack = gBattleMons[bankAtk].attack;
					u8 defAttack = gBattleMons[bankDef].attack;
					u8 atkSpAttack = gBattleMons[bankAtk].spAttack;
					u8 defSpAttack = gBattleMons[bankDef].spAttack;

					if (atkAttack + atkSpAttack >= defAttack + defSpAttack) //Combined attacker stats are > than combined target stats
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_GUARDSPLIT: ;
					u8 atkDefense = gBattleMons[bankAtk].defense;
					u8 defDefense = gBattleMons[bankDef].defense;
					u8 atkSpDefense = gBattleMons[bankAtk].spDefense;
					u8 defSpDefense = gBattleMons[bankDef].spDefense;

					if (atkDefense + atkSpDefense >= defDefense + defSpDefense) //Combined attacker stats are > than combined target stats
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;
			}
			break;

		case EFFECT_ME_FIRST: ;
			if (move == MOVE_MEFIRST && predictedMove != MOVE_NONE)
			{
				if (!MoveWouldHitFirst(move, bankAtk, bankDef))
					DECREASE_VIABILITY(10);
				else
					return AI_Script_Negatives(bankAtk, bankDef, predictedMove, originalViability);
			}
			else //Target is predicted to switch most likely
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_NATURAL_GIFT:
			if (atkAbility == ABILITY_KLUTZ
			|| IsMagicRoomActive()
			|| GetPocketByItemId(atkItem) != POCKET_BERRIES)
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_SET_TERRAIN:
			if (PARTNER_MOVE_EFFECT_IS_TERRAIN)
			{
				DECREASE_VIABILITY(10);
				break;
			}

			switch (move) {
				case MOVE_ELECTRICTERRAIN:
					if (gTerrainType == ELECTRIC_TERRAIN)
						DECREASE_VIABILITY(10);
					break;
				case MOVE_GRASSYTERRAIN:
					if (gTerrainType == GRASSY_TERRAIN)
						DECREASE_VIABILITY(10);
					break;
				case MOVE_MISTYTERRAIN:
					if (gTerrainType == MISTY_TERRAIN)
						DECREASE_VIABILITY(10);
					break;
				case MOVE_PSYCHICTERRAIN:
					if (gTerrainType == PSYCHIC_TERRAIN)
						DECREASE_VIABILITY(10);
					break;
			}
			break;

		case EFFECT_PLEDGE:
			if (IS_DOUBLE_BATTLE
			&&  gBattleMons[bankAtkPartner].hp > 0)
			{
				if (partnerMove != MOVE_NONE
				&&  gBattleMoves[partnerMove].effect == EFFECT_PLEDGE
				&&  move != partnerMove) //Different pledge moves
				{
					if (gBattleMons[bankAtkPartner].status1 & (STATUS1_SLEEP | STATUS1_FREEZE)
					&&  gBattleMons[bankAtkPartner].status1 != 1) //Will wake up this turn
						DECREASE_VIABILITY(10); //Don't use combo move if your partner will cause failure
				}
			}
			break;

		case EFFECT_FIELD_EFFECTS:
			switch (move) {
				case MOVE_TRICKROOM:
					if (PARTNER_MOVE_IS_TAILWIND_TRICKROOM)
						DECREASE_VIABILITY(10);

					if (IsTrickRoomActive()) //Trick Room Up
					{
						if (GetPokemonOnSideSpeedAverage(bankAtk) < GetPokemonOnSideSpeedAverage(bankDef)) //Attacker side slower than target side
							DECREASE_VIABILITY(10); //Keep the Trick Room up
					}
					else //No Trick Room Up
					{
						if (GetPokemonOnSideSpeedAverage(bankAtk) > GetPokemonOnSideSpeedAverage(bankDef)) //Attacker side faster than target side
							DECREASE_VIABILITY(10); //Keep the Trick Room down
					}
					break;

				case MOVE_MAGICROOM:
					if (IsMagicRoomActive()
					|| PARTNER_MOVE_IS_SAME_NO_TARGET)
						DECREASE_VIABILITY(10);
					break;

				case MOVE_WONDERROOM:
					if (IsWonderRoomActive()
					|| PARTNER_MOVE_IS_SAME_NO_TARGET)
						DECREASE_VIABILITY(10);
					break;

				case MOVE_GRAVITY:
					if ((IsGravityActive()
					&&  !IsOfType(bankAtk, TYPE_FLYING)
					&&  atkEffect != ITEM_EFFECT_AIR_BALLOON) //Should revert Gravity in this case
					|| PARTNER_MOVE_IS_SAME_NO_TARGET)
						DECREASE_VIABILITY(10);
					break;

				case MOVE_IONDELUGE:
					if (IsIonDelugeActive()
					|| PARTNER_MOVE_IS_SAME_NO_TARGET)
						DECREASE_VIABILITY(10);
					break;

				case MOVE_PLASMAFISTS:
					goto AI_STANDARD_DAMAGE;
			}
			break;

		case EFFECT_FLING:
			if (!CanFling(atkItem, atkSpecies, atkAbility, bankAtk, gNewBS->EmbargoTimers[bankAtk]))
				DECREASE_VIABILITY(10);

			u8 effect = gFlingTable[atkItem].effect;

			switch (effect) {
				case MOVE_EFFECT_BURN:
					goto AI_BURN_CHECK;

				case MOVE_EFFECT_PARALYSIS:
					goto AI_PARALYZE_CHECK;

				case MOVE_EFFECT_POISON:
				case MOVE_EFFECT_TOXIC:
					goto AI_POISON_CHECK;

				case MOVE_EFFECT_FREEZE:
					if (!CanBeFrozen(bankDef, TRUE)
					 || MoveBlockedBySubstitute(move, bankAtk, bankDef))
						DECREASE_VIABILITY(10);
					break;
			}

			goto AI_STANDARD_DAMAGE;

		case EFFECT_ATTACK_BLOCKERS:
			switch (move) {
				case MOVE_EMBARGO:
					if (defAbility == ABILITY_KLUTZ
					|| IsMagicRoomActive()
					|| gNewBS->EmbargoTimers[bankDef] != 0
					|| PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_POWDER:
					if (!MoveTypeInMoveset(bankDef, TYPE_FIRE)
					|| PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_TELEKINESIS:
					if (defStatus3 & (STATUS3_TELEKINESIS | STATUS3_ROOTED | STATUS3_SMACKED_DOWN)
					||  IsGravityActive()
					||  defEffect == ITEM_EFFECT_IRON_BALL
					||  CheckTableForSpecies(defSpecies, gTelekinesisBanList)
					|| PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_THROATCHOP:
					goto AI_STANDARD_DAMAGE;

				default: //Heal Block
					if (IsHealBlocked(bankDef)
					|| PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;
			}
			break;

		case EFFECT_TYPE_CHANGES:
			switch (move) {
				case MOVE_SOAK:
					if (PARTNER_MOVE_IS_SAME
					|| (gBattleMons[bankDef].type1 == TYPE_WATER
					 && gBattleMons[bankDef].type2 == TYPE_WATER
					 && gBattleMons[bankDef].type3 == TYPE_BLANK))
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_TRICKORTREAT:
					if (gBattleMons[bankDef].type1 == TYPE_GHOST
					||  gBattleMons[bankDef].type2 == TYPE_GHOST
					||  gBattleMons[bankDef].type3 == TYPE_GHOST
					||  PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				case MOVE_FORESTSCURSE:
					if (gBattleMons[bankDef].type1 == TYPE_GRASS
					||  gBattleMons[bankDef].type2 == TYPE_GRASS
					||  gBattleMons[bankDef].type3 == TYPE_GRASS
					||  PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;
			}
			break;

		case EFFECT_HEAL_TARGET:
			switch (move) {
				case MOVE_POLLENPUFF:
					if (!TARGETING_PARTNER)
						goto AI_STANDARD_DAMAGE;

				__attribute__ ((fallthrough));
				default:
					if (!TARGETING_PARTNER) //Don't heal enemies
						DECREASE_VIABILITY(10);
					else
					{
						if (BATTLER_MAX_HP(bankDef))
							DECREASE_VIABILITY(10);
						if (gBattleMons[bankDef].hp > gBattleMons[bankDef].maxHP / 2)
							DECREASE_VIABILITY(5);
						else
							goto AI_SUBSTITUTE_CHECK;
					}
			}
			break;

		case EFFECT_TOPSY_TURVY_ELECTRIFY:
			switch (move) {
				case MOVE_ELECTRIFY:
					if (!MoveWouldHitFirst(move, bankAtk, bankDef)
					||  GetMoveTypeSpecial(bankDef, predictedMove) == TYPE_ELECTRIC //Move will already be electric type
					||  PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					else
						goto AI_SUBSTITUTE_CHECK;
					break;

				default: ; //Topsy Turvy
					if (!TARGETING_PARTNER)
					{
						u8 targetPositiveStages = CountBanksPositiveStatStages(bankDef);
						u8 targetNegativeStages = CountBanksNegativeStatStages(bankDef);

						if (targetPositiveStages == 0 //No good stat changes to make bad
						||  PARTNER_MOVE_IS_SAME)
							DECREASE_VIABILITY(10);

						else if (targetNegativeStages < targetPositiveStages)
							DECREASE_VIABILITY(5); //More stages would be made positive than negative
					}
			}
			break;

		case EFFECT_FAIRY_LOCK_HAPPY_HOUR:
			switch (move) {
				case MOVE_FAIRYLOCK:
					if (IsFairyLockActive()
					||  PARTNER_MOVE_IS_SAME_NO_TARGET)
						DECREASE_VIABILITY(10);
					break;

				case MOVE_HAPPYHOUR:
					if (!(gBattleTypeFlags & (BATTLE_TYPE_LINK
											| BATTLE_TYPE_EREADER_TRAINER
											| BATTLE_TYPE_FRONTIER
											| BATTLE_TYPE_TRAINER_TOWER))
					|| SIDE(bankAtk) != B_SIDE_PLAYER //Only increase money amount if will benefit player
					|| gNewBS->HappyHourByte != 0) //Already used Happy Hour
					{
						if (!IsTypeZCrystal(atkItem, moveType) || gNewBS->ZMoveData->used[bankAtk])
							DECREASE_VIABILITY(10);
					}
					break;

				case MOVE_CELEBRATE:
				case MOVE_HOLDHANDS:
					if (!IsTypeZCrystal(atkItem, moveType)
					|| atkQuality != moveType
					|| gNewBS->ZMoveData->used[bankAtk])
						DECREASE_VIABILITY(10);
					break;
			}
			break;

		case EFFECT_INSTRUCT_AFTER_YOU_QUASH:
			switch (move) {
				case MOVE_INSTRUCT: ;
					u16 instructedMove;

					if (!MoveWouldHitFirst(move, bankAtk, bankDef))
						instructedMove = predictedMove;
					else
						instructedMove = gLastPrintedMoves[bankDef];

					if (instructedMove == MOVE_NONE
					||  CheckTableForMove(instructedMove, gInstructBannedMoves)
					||  CheckTableForMove(instructedMove, gMovesThatRequireRecharging)
					||  CheckTableForMove(instructedMove, gMovesThatCallOtherMoves)
					|| (IsZMove(instructedMove))
					|| (gLockedMoves[bankDef] != 0 && gLockedMoves[bankDef] != 0xFFFF)
					||  gBattleMons[bankDef].status2 & STATUS2_MULTIPLETURNS
					||  PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					else if (IS_DOUBLE_BATTLE)
					{
						if (!TARGETING_PARTNER)
							DECREASE_VIABILITY(10);
					}
					else
					{
						if (gBattleMoves[instructedMove].target & (MOVE_TARGET_SELECTED
																 | MOVE_TARGET_DEPENDS
																 | MOVE_TARGET_RANDOM
																 | MOVE_TARGET_BOTH
																 | MOVE_TARGET_ALL
																 | MOVE_TARGET_OPPONENTS_FIELD)
						&& instructedMove != MOVE_MINDBLOWN)
							DECREASE_VIABILITY(10); //Don't force the enemy to attack you again unless it can kill itself with Mind Blown
						else if (instructedMove != MOVE_MINDBLOWN)
							DECREASE_VIABILITY(5); //Do something better
					}
					break;

				case MOVE_QUASH:
					if (!IS_DOUBLE_BATTLE
					|| !MoveWouldHitFirst(move, bankAtk, bankDef)
					||  PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					break;

				default: //After You
					if (!TARGETING_PARTNER
					|| !IS_DOUBLE_BATTLE
					|| !MoveWouldHitFirst(move, bankAtk, bankDef)
					||  PARTNER_MOVE_IS_SAME)
						DECREASE_VIABILITY(10);
					break;
			}
			break;

		case EFFECT_SUCKER_PUNCH: ;
			if (predictedMove != MOVE_NONE)
			{
				if (SPLIT(predictedMove) == SPLIT_STATUS
				|| !MoveWouldHitFirst(move, bankAtk, bankDef))
				{
					DECREASE_VIABILITY(10);
					break;
				}
			}

			//If the foe has move prediction, assume damage move for now.
			goto AI_STANDARD_DAMAGE;

		case EFFECT_TEAM_EFFECTS:
			switch (move) {
				case MOVE_TAILWIND:
					if (gNewBS->TailwindTimers[SIDE(bankAtk)] != 0
					||  PARTNER_MOVE_IS_TAILWIND_TRICKROOM
					||  (IsTrickRoomActive() && gNewBS->TrickRoomTimer != 1)) //Trick Room active and not ending this turn
						DECREASE_VIABILITY(10);
					break;

				case MOVE_LUCKYCHANT:
					if (gNewBS->LuckyChantTimers[SIDE(bankAtk)] != 0
					||  PARTNER_MOVE_IS_SAME_NO_TARGET)
						DECREASE_VIABILITY(10);
					break;

				case MOVE_MAGNETRISE:
					if (IsGravityActive()
					|| gNewBS->MagnetRiseTimers[bankAtk] != 0
					|| atkEffect == ITEM_EFFECT_IRON_BALL
					|| atkStatus3 & (STATUS3_ROOTED | STATUS3_LEVITATING | STATUS3_SMACKED_DOWN)
					|| CheckGrounding(bankAtk) == IN_AIR)
						DECREASE_VIABILITY(10);
					break;
			}
			break;

		case EFFECT_CAMOUFLAGE:
			if (IsOfType(bankAtk, GetCamouflageType()))
				DECREASE_VIABILITY(10);
			break;

		case EFFECT_LASTRESORT_SKYDROP:
			switch (move) {
				case MOVE_LASTRESORT:
					if (!CanUseLastResort(bankAtk))
						DECREASE_VIABILITY(10);
					else
						goto AI_STANDARD_DAMAGE;
					break;

			default: //Sky Drop
				if (WillFaintFromWeatherSoon(bankAtk)
				||  MoveBlockedBySubstitute(move, bankAtk, bankDef)
				||  GetActualSpeciesWeight(defSpecies, defAbility, defEffect, bankDef, TRUE) >= 2000) //200.0 kg
					DECREASE_VIABILITY(10);
				else
					goto AI_STANDARD_DAMAGE;
			}
			break;

		case EFFECT_SYNCHRONOISE:
			//Check holding ring target or is of same type
			if (defEffect == ITEM_EFFECT_RING_TARGET
			|| IsOfType(bankDef, gBattleMons[bankAtk].type1)
			|| IsOfType(bankDef, gBattleMons[bankAtk].type2)
			|| IsOfType(bankDef, gBattleMons[bankAtk].type3))
				goto AI_STANDARD_DAMAGE;
			else
				DECREASE_VIABILITY(10);
			break;

		default:
		AI_STANDARD_DAMAGE: ;
			if (moveSplit != SPLIT_STATUS && !TARGETING_PARTNER)
			{
				if (AI_SpecialTypeCalc(move, bankAtk, bankDef) & (MOVE_RESULT_NO_EFFECT | MOVE_RESULT_MISSED))
					DECREASE_VIABILITY(15);
			}
			break;
	} //Move effects switch

	if (IS_DOUBLE_BATTLE
	&&  partnerMove != MOVE_NONE
	&&  gBattleMoves[partnerMove].effect == EFFECT_HELPING_HAND
	&&  SPLIT(move) == SPLIT_STATUS)
		DECREASE_VIABILITY(10); //Don't use a status move if partner wants to help

	//Put Acc check here

	if (viability < 0)
		return 0;

	return viability;
}

static void AI_Flee(void)
{
	AI_THINKING_STRUCT->aiAction |= (AI_ACTION_DONE | AI_ACTION_FLEE | AI_ACTION_DO_NOT_ATTACK);
}

u8 AI_Script_Roaming(const u8 bankAtk, const unusedArg u8 bankDef, const unusedArg u16 move, const u8 originalViability)
{
	u8 atkAbility = ABILITY(bankAtk);
	u8 atkEffect = ITEM_EFFECT(bankAtk);

	if (atkAbility == ABILITY_RUNAWAY
	||  atkEffect == ITEM_EFFECT_CAN_ALWAYS_RUN
	||  IsOfType(bankAtk, TYPE_GHOST))
	{
		AI_THINKING_STRUCT->aiAction |= (AI_ACTION_DONE | AI_ACTION_FLEE | AI_ACTION_DO_NOT_ATTACK);
	}

	else if (IsTrapped(bankAtk, FALSE))
	{
		return originalViability;
	}

	AI_Flee();
	return originalViability;
}

static void AI_Watch(void)
{
	AI_THINKING_STRUCT->aiAction |= (AI_ACTION_DONE | AI_ACTION_WATCH | AI_ACTION_DO_NOT_ATTACK);
}

u8 AI_Script_Safari(const unusedArg u8 bankAtk, const unusedArg u8 bankDef, const unusedArg u16 move, const u8 originalViability)
{
	u8 safariFleeRate = gBattleStruct->safariEscapeFactor * 5; // Safari flee rate, from 0-20.

	if ((u8) (Random() % 100) < safariFleeRate)
	{
		AI_Flee();
	}
	else
	{
		AI_Watch();
	}

	return originalViability;
}

u8 AI_Script_FirstBattle(const unusedArg u8 bankAtk, const unusedArg u8 bankDef, const unusedArg u16 move, const u8 originalViability)
{
	s16 viability = originalViability;
	if (GetHealthPercentage(bankDef) < 20 &&  SPLIT(move) != SPLIT_STATUS)
		DECREASE_VIABILITY(20); //Only use status moves to let the player win

	if (viability < 0)
		return 0;

	return viability;
}
