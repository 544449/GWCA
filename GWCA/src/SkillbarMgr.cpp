#include <GWCA\Managers\SkillbarMgr.h>

#include <GWCA\Context\GameContext.h>
#include <GWCA\Context\WorldContext.h>

#include <GWCA\Managers\CtoSMgr.h>
#include <GWCA\Managers\AgentMgr.h>
#include <GWCA\Managers\PartyMgr.h>
#include <GWCA\Managers\PlayerMgr.h>
#include <GWCA\Managers\MemoryMgr.h>


typedef void(__fastcall *UseSkill_t)(DWORD, DWORD, DWORD, DWORD);
static UseSkill_t UseSkill;

static const char _Base64ToValue[128] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // [0,   16)
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // [16,  32)
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, // [32,  48)
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, // [48,  64)
	-1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, // [64,  80)
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, // [80,  96)
	-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, // [96,  112)
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, // [112, 128)
};

static void _WriteBits(int val, char buff[6]) {
	buff[0] = ((val >> 0) & 1);
	buff[1] = ((val >> 1) & 1);
	buff[2] = ((val >> 2) & 1);
	buff[3] = ((val >> 3) & 1);
	buff[4] = ((val >> 4) & 1);
	buff[5] = ((val >> 5) & 1);
}

static int _ReadBits(char **str, int n) {
	int val = 0;
	char *s = *str;
	for (int i = 0; i < n; i++)
		val |= (*s++ << i);
	*str = s;
	return val;
}

GW::Skill GW::SkillbarMgr::GetSkillConstantData(DWORD SkillID) {
	static GW::Skill* skillconstants = nullptr;
	if (skillconstants == nullptr) {
		// Skill array.
		BYTE* SkillArray = (BYTE*)Scanner::Find("\x8D\x04\xB6\x5E\xC1\xE0\x05\x05", "xxxxxxxx", 0);
		printf("SkillArray Addr = %p\n", SkillArray);
		if (SkillArray) {
			skillconstants = *(GW::Skill**)(SkillArray + 8);
		}
	}
	return skillconstants[SkillID];
}

void GW::SkillbarMgr::ChangeSecondary(DWORD profession, int heroindex) {
	GW::AgentID id = Agents::GetHeroAgentID(heroindex);
	CtoS::SendPacket(0xC, 0x3B, id, profession);
}

void GW::SkillbarMgr::LoadSkillbar(DWORD skillids[8], int heroindex) {
	GW::AgentID id = Agents::GetHeroAgentID(heroindex);
	CtoS::SendPacket(0x2C, 0x56, id, 0x8, skillids[0], skillids[1], skillids[2], skillids[3], skillids[4], skillids[5], skillids[6], skillids[7]);
}

bool GW::SkillbarMgr::LoadSkillTemplate(const char *temp, int heroindex) {
	const int SKILL_MAX = 3410; // @Move
	const int ATTRIBUTE_MAX = 44; // @Move

	size_t len = strlen(temp);
	char *bitStr = new char[len * 6]; // @Enhancement, this doesn't need to be a heap alloc.

	for (size_t i = 0; i < len; i++) {
		int numeric_value = _Base64ToValue[temp[i]];
		if (numeric_value == -1) {
			fprintf_s(stderr, "Unvalid base64 character '%c' in string '%s'\n", temp[i], temp);
			goto free_and_false;
		}
		_WriteBits(numeric_value, bitStr + (6 * i));
	}

	int AttribIDs[10] = { 0 };
	int AttribVal[10] = { 0 };
	int AttribCount = 0;

	int SkillIDs[8] = { 0 };
	int SkillCount = 0;

	char *it = bitStr;
	char *end = bitStr + 6 * len;

	// HEADER
	int header = _ReadBits(&it, 4);
	if (header != 0 && header != 14) goto free_and_false;
	if (header == 14) _ReadBits(&it, 4);
	int bits_per_prof = 2 * _ReadBits(&it, 2) + 4;
	int prof1 = _ReadBits(&it, bits_per_prof);
	int prof2 = _ReadBits(&it, bits_per_prof);
	if (prof1 <= 0 || prof2 < 0 || prof1 > 10 || prof2 > 10) goto free_and_false;;

	// ATTRIBUTES
	AttribCount = _ReadBits(&it, 4);
	int bits_per_attr = _ReadBits(&it, 4) + 4;
	for (int i = 0; i < AttribCount; i++) {
		AttribIDs[i] = _ReadBits(&it, bits_per_attr);
		AttribVal[i] = _ReadBits(&it, 4);
		if (AttribIDs[i] > ATTRIBUTE_MAX) {
			fprintf_s(stderr, "Attribute id %d is out of range. (max = %d)\n", AttribIDs[i], 44);
			goto free_and_false;
		}
	}

	// SKILLS
	int bits_per_skill = _ReadBits(&it, 4) + 8;
	for (SkillCount = 0; SkillCount < 8; SkillCount++) {
		SkillIDs[SkillCount] = _ReadBits(&it, bits_per_skill);
		if (SkillIDs[SkillCount] > SKILL_MAX) {
			fprintf_s(stderr, "Skill id %d is out of range. (max = %d)\n", SkillIDs[SkillCount], SKILL_MAX);
			goto free_and_false;
		}
		if (it + bits_per_skill > end) break; // Gw parse a template that doesn't specifie all empty skills.
	}

	if (!GW::PartyMgr::GetIsPartyLoaded()) goto free_and_false;
	GW::PartyInfo* info = GW::PartyMgr::GetPartyInfo();
	if (info == nullptr) goto free_and_false;
	GW::PlayerArray players = GW::Agents::GetPlayerArray();
	if (!players.valid()) goto free_and_false;

	Agent *me = GW::Agents::GetPlayer();
	Array<HeroPartyMember> heroes = info->heroes;

	if (heroindex >= 0 && heroindex < (int)heroes.size()) {
		GW::HeroPartyMember hero = heroes[heroindex];
		for (GW::PlayerPartyMember& player : info->players) {
			long id = players[player.loginnumber].AgentID;
			if (id == GW::Agents::GetPlayerId()) {
				if (hero.ownerplayerid == player.loginnumber) {
					BYTE Primary = GW::Constants::HeroProfs[hero.heroid];
					if (Primary == prof1 || Primary == 0) { //Hacky, because we can't check for mercenary heroes and Razah
						heroindex++;
						GW::PlayerMgr::ChangeSecondProfession((GW::Constants::Profession)prof2, heroindex);
						LoadSkillbar((DWORD *)SkillIDs, heroindex);
						SetAttributes(AttribCount, (DWORD *)AttribIDs, (DWORD *)AttribVal, heroindex);
					}
					return true;
				}
			}
		}
	} else if (heroindex == -1) {
		if (me && me->Primary == prof1) {
			GW::PlayerMgr::ChangeSecondProfession((GW::Constants::Profession)prof2);
			LoadSkillbar((DWORD *)SkillIDs);
			SetAttributes(AttribCount, (DWORD *)AttribIDs, (DWORD *)AttribVal);
		}
		return true;
	} else goto free_and_false;

free_and_false:
	delete[] bitStr;
	return false;
}

void GW::SkillbarMgr::SetAttributes(DWORD attributecount, DWORD * attributeids, DWORD * attributevalues, int heroindex) {

	struct tSetAttributes {
		DWORD header = 0x9;
		GW::AgentID id;
		DWORD attributecount1;
		DWORD attributeids[16];
		DWORD attributecount2;
		DWORD attributevalues[16];
	};

	static tSetAttributes* set_attributes_buffer = new tSetAttributes();

	GW::AgentID id = Agents::GetHeroAgentID(heroindex);

	set_attributes_buffer->header = 0x9;

	set_attributes_buffer->id = id;

	set_attributes_buffer->attributecount1 = attributecount;
	set_attributes_buffer->attributecount2 = attributecount;

	for (DWORD i = 0; i < attributecount; ++i) {
		set_attributes_buffer->attributeids[i] = attributeids[i];
		set_attributes_buffer->attributevalues[i] = attributevalues[i];
	}

	CtoS::SendPacket<tSetAttributes>(set_attributes_buffer);
}

void GW::SkillbarMgr::UseSkill(DWORD Slot, DWORD Target /*= 0*/, DWORD CallTarget /*= 0*/) {
	static UseSkill_t useskill_func = nullptr;
	if (useskill_func == nullptr) {
		useskill_func = (UseSkill_t)Scanner::Find("\x55\x8B\xEC\x83\xEC\x10\x53\x56\x8B\xD9\x57\x8B\xF2\x89\x5D\xF0", "xxxxxxxxxxxxxxxx", 0);
		printf("UseSkillFunction = 0x%X\n", (DWORD)useskill_func);
	}
	if (useskill_func) {
		useskill_func(Agents::GetPlayerId(), Slot, Target, CallTarget);
	}
}

void GW::SkillbarMgr::UseSkillByID(DWORD SkillID, DWORD Target /*= 0*/, DWORD CallTarget /*= 0*/) {
	CtoS::SendPacket(0x14, 0x40, SkillID, 0, Target, CallTarget);
}

int GW::SkillbarMgr::GetSkillSlot(GW::Constants::SkillID SkillID) {
	DWORD id = static_cast<DWORD>(SkillID);
	GW::Skillbar bar = Skillbar::GetPlayerSkillbar();
	if (!bar.IsValid()) return -1;
	for (int i = 0; i < 8; ++i) {
		if (bar.Skills[i].SkillId == id) {
			return i;
		}
	}
	return -1;
}
