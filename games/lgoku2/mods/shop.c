// The Capsule shop and what you are wearing: opened from the Start menu's
// Extras page (hub.c). Zenie comes from beaten
// enemies (combat.c).
//
//   OUTFIT      What the character is wearing, on Buu's Fury's own Equip
//               screen (its picture is the backdrop): body (endurance), hands
//               (strength), feet (power) and an accessory. A on a slot changes
//               it to the next piece you own, or to nothing.
//   BUY GEAR    Equipment, as in Buu's Fury. A piece is bought once and can
//               then be worn by any character, by several at a time.
//   TECHNIQUES  Any base character can learn any other base character's
//               special attacks (nine in all, less his own two). A learned
//               technique goes in a third special slot of his own (L reaches
//               it after the energy blast; fusion.c runs that), so his own two
//               specials stay. With several learned, A on the slot picks which.
//               (Not Hercule, nor fusions.)
//   SUPPLIES    A full restore of health and energy.
//
// L and R change the page, Left and Right the character, A buys or puts on,
// B closes.
//
// Equipment does not touch the character's own stats: this file writes the
// numbers the fighting code and status screen read (the player object's copy
// of the stats) every frame, as the character's own plus what is worn.
// What is owned and worn is kept in the mod settings file, not the save.
#include "lg2.h"

#include <stdio.h>
#include <string.h>

#include "generated/equip_backdrop.inc"

enum { BTN_A = 1, BTN_B = 2, BTN_RIGHT = 16, BTN_LEFT = 32, BTN_UP = 64, BTN_DOWN = 128, BTN_R = 256, BTN_L = 512 };

int lg2StatBoostPercent = 100;

// ---- gear ------------------------------------------------------------------------------
enum { GLOVES, FOCUS, CLOTHING, CHARM, SLOTS };
enum { PLAIN, MORE_ZENIE, LESS_HURT, MORE_DAMAGE };
static const struct Item { const char* name; uint8_t slot, bonus, effect; int price; const char* does; } items[] = {
	{ "TRAINING WEIGHTS", GLOVES, 2, PLAIN, 150, "STR +2" },
	{ "FIGHTING GLOVES", GLOVES, 5, PLAIN, 700, "STR +5" },
	{ "POWER WRISTBANDS", GLOVES, 9, PLAIN, 2800, "STR +9" },
	{ "Z SWORD", GLOVES, 14, PLAIN, 9500, "STR +14" },
	{ "WEIGHTED BOOTS", FOCUS, 2, PLAIN, 150, "POW +2" },
	{ "FIGHTING BOOTS", FOCUS, 5, PLAIN, 700, "POW +5" },
	{ "KI BOOTS", FOCUS, 9, PLAIN, 2800, "POW +9" },
	{ "KAI BOOTS", FOCUS, 14, PLAIN, 9500, "POW +14" },
	{ "TURTLE GI", CLOTHING, 2, PLAIN, 150, "END +2" },
	{ "WEIGHTED GI", CLOTHING, 5, PLAIN, 700, "END +5" },
	{ "SAIYAN ARMOUR", CLOTHING, 9, PLAIN, 2800, "END +9" },
	{ "KAI ROBES", CLOTHING, 14, PLAIN, 9500, "END +14" },
	{ "LUCKY COIN", CHARM, 0, MORE_ZENIE, 500, "ZENIE +50%" },
	{ "GUARD CHARM", CHARM, 0, LESS_HURT, 2000, "DAMAGE TAKEN -15%" },
	{ "POWER STONE", CHARM, 0, MORE_DAMAGE, 2000, "DAMAGE DEALT +15%" },
};
#define ITEMS ((int) (sizeof(items) / sizeof(items[0])))

static uint32_t owned;                       // one bit an item
static int8_t worn[LG2_CHARACTERS][SLOTS];   // item number, or -1

static void saveGear(int who) {
	char name[24];
	int s, packed = 0;
	modSetSetting("shop_owned", (int) owned);
	for (s = 0; s < SLOTS; ++s) {
		packed |= (worn[who][s] + 1) << (s * 5);
	}
	snprintf(name, sizeof(name), "shop_worn_%d", who);
	modSetSetting(name, packed);
}

static void loadGear(void) {
	char name[24];
	int who, s;
	owned = (uint32_t) modGetSetting("shop_owned", 0);
	for (who = 0; who < LG2_CHARACTERS; ++who) {
		snprintf(name, sizeof(name), "shop_worn_%d", who);
		int packed = modGetSetting(name, 0);
		for (s = 0; s < SLOTS; ++s) {
			int item = ((packed >> (s * 5)) & 31) - 1;
			worn[who][s] = item >= 0 && item < ITEMS && items[item].slot == s && (owned & (1u << item)) ? item : -1;
		}
	}
}

// The numbers the fighting code reads: the character's own stats, times any
// boost, plus gear. Written through both pointers to the player object, so the
// status screen shows them too.
static void writeStats(void) {
	int who = lg2CurrentChar(), i, k;
	if (who < 0 || who >= LG2_CHARACTERS) {
		return;
	}
	const struct LG2Char* c = lg2Char(who);
	// The first pointer is zero while the Start menu is open; the second still
	// names the player then. At other times the second can be STALE: after an
	// area change it may hold the old player's address, where an enemy now
	// lives. (Writing through it regardless froze the game.) So it is used only
	// when the first is zero, and only if what it points at is drawn with the
	// current character's sprite.
	uint32_t pointers[2] = { modRead32(LG2_RAM_PLAYER_PTR), 0 };
	if (!pointers[0]) {
		uint32_t second = modRead32(LG2_RAM_PLAYER_PTR2);
		const uint32_t* defs = (const uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
		if (lg2IsPlayer(second) && modRead32(second + LG2_ACTOR_SPRITE_DEF) == defs[lg2FormDef(c->sprite)]) {
			pointers[1] = second;
		}
	}
	int charm = worn[who][CHARM] >= 0 ? items[worn[who][CHARM]].effect : PLAIN;
	lg2ZeniePercent = charm == MORE_ZENIE ? 150 : 100;
	lg2DamageTakenPercent = charm == LESS_HURT ? 85 : 100;
	lg2DamageDealtPercent = charm == MORE_DAMAGE ? 115 : 100;
	for (k = 0; k < 2; ++k) {
		uint8_t* stats = lg2IsPlayer(pointers[k]) ? modRam(pointers[k] + LG2_PLAYER_STATS) : 0;
		if (!stats) {
			continue;
		}
		for (i = 0; i < LG2_STATS; ++i) {
			int value = (c->stat[i] >> 8) * lg2StatBoostPercent / 100;
			if (worn[who][i] >= 0) {
				value += items[worn[who][i]].bonus;
			}
			if (i == LG2_END) {
				value = lg2FightingEndurance(value); // see lg2.h: endurance near 128 would make him untouchable
			}
			int most = i == LG2_END ? 124 : 250;
			stats[i] = value > most ? most : value;
		}
	}
}

// ---- techniques ----------------------------------------------------------------------------
#define TECHNIQUE_PRICE 1500
// The base characters' specials, by their number in LG2_ROM_ATTACKS
static const struct { const char* name; uint8_t attack; } taught[] = {
	{ "BIG BANG", 0 }, { "ENERGY PUNCH", 7 }, { "BURNING ATTACK", 1 }, { "SWORD BLAST", 13 }, { "MASENKO BALL", 4 },
	{ "KAMEHAMEHA", LG2_ATTACK_KAMEHAMEHA }, { "SPIRIT BOMB", 10 }, { "SPECIAL BEAM CANNON", 9 }, { "SCATTER SHOT", 8 },
};
#define TAUGHT ((int) (sizeof(taught) / sizeof(taught[0])))

static int canLearn(int who) {
	return who >= 0 && who < LG2_CHARACTERS && who != LG2_HERCULE;
}

static int own(int who, int attack) {
	const struct LG2Char* start = (const struct LG2Char*) modRom(LG2_ROM_CHARS) + who;
	return start->attack[0] == attack || start->attack[1] == attack;
}

static int knows(int who, int t) {
	char name[24];
	if (own(who, taught[t].attack)) {
		return 1;
	}
	snprintf(name, sizeof(name), "learned_%d_a%d", who, taught[t].attack);
	return modGetSetting(name, 0);
}

static void learn(int who, int t) {
	char name[24];
	snprintf(name, sizeof(name), "learned_%d_a%d", who, taught[t].attack);
	modSetSetting(name, 1);
}

// Earlier versions kept "learned_<who>_0" (Kamehameha) and "_1" (Masenko)
static void migrate(void) {
	static const uint8_t was[2] = { LG2_ATTACK_KAMEHAMEHA, LG2_ATTACK_MASENKO };
	char name[24], now[24];
	int who, t;
	for (who = 0; who < LG2_CHARACTERS; ++who) {
		for (t = 0; t < 2; ++t) {
			snprintf(name, sizeof(name), "learned_%d_%d", who, t);
			if (modGetSetting(name, 0)) {
				snprintf(now, sizeof(now), "learned_%d_a%d", who, was[t]);
				modSetSetting(now, 1);
				modSetSetting(name, 0);
			}
		}
	}
}

// What is in a character's third special slot: the technique chosen for it,
// else the first one he knows; -1 for none
int lg2ThirdSpecial(int who) {
	char name[24];
	int pick, t;
	if (!canLearn(who)) {
		return -1;
	}
	snprintf(name, sizeof(name), "third_a_%d", who);
	pick = modGetSetting(name, -1);
	for (t = 0; t < TAUGHT; ++t) {
		if (taught[t].attack == pick && !own(who, pick) && knows(who, t)) {
			return pick;
		}
	}
	for (t = 0; t < TAUGHT; ++t) {
		if (!own(who, taught[t].attack) && knows(who, t)) {
			return taught[t].attack;
		}
	}
	return -1;
}

const char* lg2ThirdSpecialName(int who) {
	int attack = lg2ThirdSpecial(who), t;
	for (t = 0; t < TAUGHT; ++t) {
		if (taught[t].attack == attack) {
			return taught[t].name;
		}
	}
	return "";
}

// Before the third slot existed, a learned technique replaced the second own
// special in the record (and was saved that way): put the own one back
static void repairRecords(void) {
	int who;
	for (who = 0; who < LG2_CHARACTERS; ++who) {
		struct LG2Char* c = lg2Char(who);
		const struct LG2Char* start = (const struct LG2Char*) modRom(LG2_ROM_CHARS) + who;
		int k;
		if (!(c->flags & 1) || lg2FusionKind) {
			continue;
		}
		for (k = 0; k < 2; ++k) {
			if (c->attack[k] != start->attack[k] && !own(who, c->attack[k])) {
				c->attack[k] = start->attack[k];
			}
		}
	}
}

// The techniques on offer to a character: everyone's but his own two, in order
static int learnable(int who, int n) {
	int t;
	for (t = 0; t < TAUGHT; ++t) {
		if (!own(who, taught[t].attack) && n-- == 0) {
			return t;
		}
	}
	return -1;
}

static int learnableCount(int who) {
	int t, n = 0;
	for (t = 0; t < TAUGHT; ++t) {
		n += !own(who, taught[t].attack);
	}
	return n;
}

static const char* attackName(int who, int attack) {
	const struct LG2Char* start = (const struct LG2Char*) modRom(LG2_ROM_CHARS) + who;
	int t;
	for (t = 0; t < TAUGHT; ++t) {
		if (attack == taught[t].attack) {
			return taught[t].name;
		}
	}
	return attack == start->attack[0] ? "OWN FIRST SPECIAL" : attack == start->attack[1] ? "OWN SECOND SPECIAL" : "?";
}

// ---- the menu ---------------------------------------------------------------------------------
enum { PAGE_OUTFIT, PAGE_GEAR, PAGE_TECHNIQUES, PAGE_SUPPLIES, PAGES };
static const char* const pageNames[PAGES] = { "OUTFIT", "BUY GEAR", "TECHNIQUES", "SUPPLIES" };
// The rows of the Equip screen, top to bottom, and which of our slots each is
static const uint8_t rowSlot[SLOTS] = { CLOTHING, GLOVES, FOCUS, CHARM };
static const char* const rowNames[SLOTS] = { "BODY", "HANDS", "FEET", "ACCESSORY" };
#define RESTORE_PRICE 50
static int open, page, cursor, viewed;
static const char* note = "";
static unsigned noteUntil;

static void tell(const char* text) {
	note = text;
	noteUntil = modFrame() + 120;
}

static int rowsOn(int which) {
	return which == PAGE_OUTFIT ? SLOTS : which == PAGE_GEAR ? ITEMS : which == PAGE_TECHNIQUES ? 3 + learnableCount(viewed) : 1;
}

static int pay(int price) {
	if (lg2Zenie() < price) {
		tell("NOT ENOUGH ZENIE");
		return 0;
	}
	lg2AddZenie(-price);
	return 1;
}

static void choose(void) {
	struct LG2Char* c = lg2Char(viewed);
	if (page == PAGE_OUTFIT) {
		// the next owned piece for this slot after the one worn, then nothing, then round again
		int slot = rowSlot[cursor], i, now = worn[viewed][slot], next = -1;
		for (i = now + 1; i < ITEMS; ++i) {
			if (items[i].slot == slot && (owned & (1u << i))) {
				next = i;
				break;
			}
		}
		if (next < 0 && now < 0) {
			tell("NOTHING OWNED FOR THIS: SEE BUY GEAR");
		}
		worn[viewed][slot] = next;
		saveGear(viewed);
	} else if (page == PAGE_GEAR) {
		const struct Item* item = &items[cursor];
		if (!(owned & (1u << cursor))) {
			if (pay(item->price)) {
				owned |= 1u << cursor;
				worn[viewed][item->slot] = cursor;
				tell("BOUGHT AND PUT ON");
			}
		} else {
			worn[viewed][item->slot] = worn[viewed][item->slot] == cursor ? -1 : cursor;
		}
		saveGear(viewed);
	} else if (page == PAGE_TECHNIQUES) {
		if (!canLearn(viewed)) {
			tell("NOTHING TO LEARN HERE");
		} else if (cursor < 2) {
			tell("HIS OWN: THEY STAY");
		} else if (cursor == 2) {
			// the third slot: the next learned technique, round and round
			char name[24];
			int t, now = -1, next = -1;
			for (t = 0; t < TAUGHT; ++t) {
				if (taught[t].attack == lg2ThirdSpecial(viewed)) {
					now = t;
				}
			}
			for (t = 1; t <= TAUGHT; ++t) {
				int k = (now + t) % TAUGHT;
				if (!own(viewed, taught[k].attack) && knows(viewed, k)) {
					next = k;
					break;
				}
			}
			if (next < 0) {
				tell("NOTHING LEARNED YET");
			} else {
				snprintf(name, sizeof(name), "third_a_%d", viewed);
				modSetSetting(name, taught[next].attack);
			}
		} else {
			int t = learnable(viewed, cursor - 3);
			if (t < 0) {
				tell("?");
			} else if (knows(viewed, t)) {
				tell("ALREADY KNOWN");
			} else if (pay(TECHNIQUE_PRICE)) {
				learn(viewed, t);
				tell("LEARNED: IT IS IN SLOT 3");
			}
		}
	} else if (c->hp == 0) {
		tell("CANNOT HELP THE FALLEN");
	} else if (c->hp == c->maxHp && c->ep == c->maxEp) {
		tell("ALREADY AT FULL STRENGTH");
	} else if (pay(RESTORE_PRICE)) {
		c->hp = c->maxHp;
		c->ep = c->maxEp;
		tell("RESTORED");
	}
}

// The outfit page: Buu's Fury's Equip screen, with our words on it
static void drawOutfit(void) {
	const struct LG2Char* c = lg2Char(viewed);
	static const uint8_t statRow[LG2_STATS] = { 37, 50, 62 }; // where the screen has STR, POW, END
	int row, i, x, y;
	memcpy(modScreen(), equipBackdrop, sizeof(equipBackdrop));
	for (y = 0; y < EQUIP_ARROW_H; ++y) { // the cursor
		for (x = 0; x < EQUIP_ARROW_W; ++x) {
			uint32_t color = equipArrow[y * EQUIP_ARROW_W + x];
			if (color != 0xFFFFFFFF) {
				modScreen()[(EQUIP_ARROW_Y + cursor * 16 + y) * 240 + EQUIP_ARROW_X + x] = color;
			}
		}
	}
	modTextPlain(1);
	modTextScale(2);
	modText(34, 12, 0xFFFFFF, "%s", pageNames[PAGES - 1]); // the pages L and R lead to
	modText(206 - 8 * (int) strlen(pageNames[PAGE_OUTFIT + 1]), 12, 0xFFFFFF, "%s", pageNames[PAGE_OUTFIT + 1]);
	for (row = 0; row < SLOTS; ++row) {
		int item = worn[viewed][rowSlot[row]];
		modText(34, 32 + row * 16, item >= 0 ? 0xFFFFFF : 0xA0A0A0, "%s", item >= 0 ? items[item].name : "-NONE-");
	}
	for (i = 0; i < LG2_STATS; ++i) {
		int bonus = worn[viewed][i] >= 0 ? items[worn[viewed][i]].bonus : 0;
		modText(198, statRow[i], bonus ? 0x40FF40 : 0xC0C0C0, "%d", (c->stat[i] >> 8) + bonus);
	}
	modTextScale(1);
	{
		int item = worn[viewed][rowSlot[cursor]];
		modText(14, 100, 0x00FFFF, "%s", lg2Name(viewed));
		modText(60, 100, 0xFFFFFF, "%s: %s", rowNames[cursor], item >= 0 ? items[item].does : "NOTHING WORN");
		modText(14, 110, 0x00FFFF, "ZENIE %d", lg2Zenie());
		modText(14, 120, 0xC0C0C0, "LEFT/RIGHT: CHARACTER   L/R: OTHER PAGES");
		if (modFrame() < noteUntil) {
			modText(60, 110, 0x40FFFF, "%s", note);
		}
	}
	modTextPlain(0);
}

static void draw(void) {
	const struct LG2Char* c = lg2Char(viewed);
	int i;
	if (page == PAGE_OUTFIT) {
		drawOutfit();
		return;
	}
	modRect(4, 4, 232, 152, 0x402010);
	modRect(6, 6, 228, 148, 0x000000);
	modTextScale(2);
	modText(12, 10, 0x40C0FF, "CAPSULE SHOP");
	modTextScale(1);
	modText(150, 10, 0x40E0FF, "ZENIE %d", lg2Zenie());
	modText(150, 18, 0xFFFFFF, "%s", lg2Name(viewed));
	for (i = 0; i < PAGES; ++i) {
		if (i == page) {
			modRect(10 + i * 56, 24, 4 * (int) strlen(pageNames[i]) + 4, 9, 0x804020);
		}
		modText(12 + i * 56, 26, i == page ? 0xFFFFFF : 0x808080, "%s", pageNames[i]);
	}
	if (page == PAGE_GEAR) {
		for (i = 0; i < ITEMS; ++i) {
			const struct Item* item = &items[i];
			int have = (owned >> i) & 1, on = worn[viewed][item->slot] == i;
			int y = 37 + i * 7 + item->slot * 2;
			uint32_t color = i == cursor ? 0xFFFFFF : on ? 0x40FF40 : have ? 0xC0C0C0 : 0x808080;
			modText(10, y, color, "%c%-17s %-17s", i == cursor ? '>' : ' ', item->name, item->does);
			if (on) {
				modText(190, y, 0x40FF40, "WORN");
			} else if (have) {
				modText(190, y, 0xC0C0C0, "OWNED");
			} else {
				modText(190, y, lg2Zenie() >= item->price ? 0x40E0FF : 0x4040C0, "%dZ", item->price);
			}
		}
	} else if (page == PAGE_TECHNIQUES) {
		if (!canLearn(viewed)) {
			modText(12, 44, 0xC0C0C0, "%s HAS NOTHING TO LEARN HERE.", lg2Name(viewed));
		} else {
			const struct LG2Char* start = (const struct LG2Char*) modRom(LG2_ROM_CHARS) + viewed;
			for (i = 0; i < 2; ++i) {
				int has = (lg2Char(viewed)->flags & LG2_ATTACK_OWNED(i)) != 0;
				modText(12, 44 + i * 10, i == cursor ? 0xFFFFFF : 0xC0C0C0, "%cSPECIAL SLOT %d: %s%s", i == cursor ? '>' : ' ', i + 1,
				        attackName(viewed, start->attack[i]), has ? "" : " (NOT YET)");
			}
			modText(12, 64, cursor == 2 ? 0xFFFFFF : 0xC0C0C0, "%cSPECIAL SLOT 3: %s", cursor == 2 ? '>' : ' ',
			        lg2ThirdSpecial(viewed) >= 0 ? lg2ThirdSpecialName(viewed) : "NOTHING LEARNED");
			for (i = 0; i < learnableCount(viewed); ++i) {
				int t = learnable(viewed, i), known = knows(viewed, t);
				modText(12, 76 + i * 8, i + 3 == cursor ? 0xFFFFFF : 0xC0C0C0, "%cLEARN %-20s", i + 3 == cursor ? '>' : ' ', taught[t].name);
				modText(124, 76 + i * 8, known ? 0x40FF40 : 0x40E0FF, known ? "KNOWN" : "%dZ", TECHNIQUE_PRICE);
			}
			modText(12, 136, 0x909090, "L REACHES SLOT 3 AFTER YOUR OWN SPECIALS; A ON IT PICKS");
		}
	} else {
		modText(12, 44, 0xFFFFFF, ">FULL RESTORE: HEALTH AND ENERGY");
		modText(170, 44, 0x40E0FF, "%dZ", RESTORE_PRICE);
		modText(12, 56, 0xC0C0C0, "%s: HP %d/%d  EP %d/%d", lg2Name(viewed), c->hp, c->maxHp, c->ep, c->maxEp);
	}
	if (modFrame() < noteUntil) {
		modText(12, 147, 0x40FFFF, "%s", note);
	} else {
		modText(12, 147, 0x909090, "L/R PAGE  LEFT/RIGHT CHARACTER  A CHOOSE  B CLOSE");
	}
}

static int fresh;

// Opened from the Start menu's Extras page
void lg2OpenShop(int which) {
	open = fresh = 1;
	viewed = lg2CurrentChar();
	page = which >= 0 && which < PAGES ? which : PAGE_OUTFIT;
	cursor = 0;
	if (!lg2HubOpen) {
		modSetPaused(1);
		modLog("shop: open, paused");
	}
}

int lg2ShopIsOpen(void) {
	return open;
}

void lg2RepairRecords(void) {
	repairRecords();
}

static void repairOnce(void) {
	static int done;
	if (!done && (modRead32(LG2_RAM_PLAYER_PTR) >> 24) == 2) {
		done = 1;
		repairRecords();
	}
}

static void frame(void) {
	static uint32_t lastKeys;
	uint32_t keys = modKeys(), pressed = keys & ~lastKeys;
	lastKeys = keys;
	if (!open) {
		if (!modPaused() || lg2HubOpen) {
			writeStats();
		}
		return;
	}
	if (fresh) { // the press that opened it is not a press in it
		fresh = 0;
		pressed = 0;
	}
	if (pressed & BTN_B) {
		open = 0;
		if (!lg2HubOpen) {
			modSetPaused(0);
			modLog("shop: closed, unpaused");
		}
		writeStats();
		return;
	}
	if (pressed & (BTN_L | BTN_R)) {
		page = (page + ((pressed & BTN_R) ? 1 : PAGES - 1)) % PAGES;
		cursor = 0;
	}
	if (pressed & (BTN_LEFT | BTN_RIGHT)) {
		int step = (pressed & BTN_RIGHT) ? 1 : LG2_CHARACTERS - 1, tries;
		for (tries = 0; tries < LG2_CHARACTERS; ++tries) {
			viewed = (viewed + step) % LG2_CHARACTERS;
			if (lg2Char(viewed)->flags & 1) {
				break;
			}
		}
	}
	if (pressed & BTN_UP) {
		cursor = (cursor + rowsOn(page) - 1) % rowsOn(page);
	}
	if (pressed & BTN_DOWN) {
		cursor = (cursor + 1) % rowsOn(page);
	}
	if (pressed & BTN_A) {
		choose();
	}
	draw();
}

MOD_INIT(shop) {
	migrate();
	loadGear();
	modOnFrame(repairOnce);
	modOnFrame(frame);
}
