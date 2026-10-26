// Other looks for Gohan, from Buu's Fury: adult Gohan and the Great Saiyaman.
//
// He is the same character as far as the game is concerned: same level,
// stats, attacks and save data. Only the sprite changes. Looks are chosen on
// the Switch Characters screen, like every character's: with Gohan highlighted,
// Up and Down go through kid, adult, Great Saiyaman, and the Saiyaman's clothes
// without the helmet and cape. The choice is remembered.
//
// The sprites in generated/ were converted from Buu's Fury by
// tools/sprites/convert_bf.py (definition 111, orange gi, and 112, the Great
// Saiyaman). They are copied into the enlarged part of the ROM and the game's
// sprite table is pointed at one of them in place of Gohan's own.
//
// Adult Gohan does not go Super Saiyan: his transformation is the Mystic form.
// It is the same transformation as far as the game is concerned, but his hair
// stays black (the same sprite is used) and a white aura flames up around him.
// The Great Saiyaman looks the same transformed or not: the helmet stays on.
// Without the helmet he goes Super Saiyan as usual, blond hair and all.
//
// His face changes too (see looks.c).
#include "lg2.h"

#include <stdlib.h>
#include <string.h>

#include "generated/adult_gohan_data.inc"
#include "generated/saiyaman_bare_data.inc"
#include "generated/saiyaman_bare_ssj_data.inc"
#include "generated/saiyaman_data.inc"

enum { BTN_UP = 64, BTN_DOWN = 128 };
enum { TRANSFORM_SLOT = 2, KID_CELL_SAGA_FORM = 2, KID_ARMOUR_FORM = 3, SSJ2_FORM = 4 };


// Gohan's sprite definition numbers: his ordinary outfits, and Super Saiyan
static const uint8_t outfits[] = { 0x35, 0x37, 0x38, 0x39, 0x3A, 0x3B };
#define OUTFITS (sizeof(outfits) / sizeof(outfits[0]))
#define FIRST_SUPER_SAIYAN 4

static uint32_t kid[OUTFITS]; // the game's own definitions, as game addresses
enum { KID, ADULT, SAIYAMAN, SAIYAMAN_BARE, LOOKS };
static int shown;             // which look the sprite table currently holds
static int wanted;
int lg2GohanLook;
static unsigned messageUntil;

// The sprite a look uses for outfit number i. Adult Gohan and the helmeted
// Saiyaman look the same transformed or not; without the helmet his hair shows,
// so that look has a Super Saiyan sprite too.
static uint32_t lookDef(int look, unsigned i) {
	uint32_t offset = look == SAIYAMAN ? saiyamanRomOffset :
	                  look == SAIYAMAN_BARE ? (i >= FIRST_SUPER_SAIYAN ? saiyamanBareSsjRomOffset : saiyamanBareRomOffset) :
	                  adultGohanRomOffset;
	return 0x08000000 + offset;
}

static void apply(int look) {
	uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
	int current = lg2FormDef(lg2Char(LG2_GOHAN)->sprite);
	unsigned i;
	if (shown != KID) { // back to his own sprites first
		for (i = 0; i < OUTFITS; ++i) {
			table[outfits[i]] = kid[i];
			// All of his outfits share the look's one sprite, so only the outfit
			// he is actually wearing says which kid sprite to show
			if (outfits[i] == current) {
				lg2MoveActors(lookDef(shown, i), kid[i]);
			}
		}
	}
	if (look != KID) {
		for (i = 0; i < OUTFITS; ++i) {
			kid[i] = table[outfits[i]];
			table[outfits[i]] = lookDef(look, i);
			lg2MoveActors(kid[i], lookDef(look, i));
		}
	}
	shown = lg2GohanLook = look;
}

// ---- Mystic form --------------------------------------------------------------------

// A thin dark edge beside the white keeps it visible on pale floors
static void blendDark(int x, int y) {
	if (x < 0 || x >= 240 || y < 0 || y >= 160) {
		return;
	}
	uint32_t* p = &modScreen()[y * 240 + x];
	uint32_t c = *p;
	*p = (((c & 0xFF) * 5 / 8) | ((((c >> 8) & 0xFF) * 5 / 8) << 8) | ((((c >> 16) & 0xFF) * 6 / 8) << 16));
}

static void blendWhite(int x, int y, int strength) { // strength out of 4
	if (x < 0 || x >= 240 || y < 0 || y >= 160) {
		return;
	}
	uint32_t* p = &modScreen()[y * 240 + x];
	uint32_t c = *p;
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	r += (255 - r) * strength / 4;
	g += (255 - g) * strength / 4;
	b += (255 - b) * strength / 4;
	*p = r | (g << 8) | (b << 16);
}

static int isqrt(int v) {
	int r = 0;
	while ((r + 1) * (r + 1) <= v) {
		++r;
	}
	return r;
}

// The aura of the anime: a flame wrapped round him, rounded under his feet,
// bulging round his body with smaller flames licking up its sides, and
// tapering over his head into pointed tongues, the tallest in the middle;
// all of it flickering. Drawn as a bright edge with a thin dark rim outside
// it, the inside lit a little (a shimmering checker, so he shows through).
static void drawMysticAura(uint32_t player) {
	uint32_t frameRecord = modRead32(player + LG2_ACTOR_FRAME_RECORD);
	int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16;
	int h = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 3) : 32;
	int cx = (int) modRead32(player + LG2_ACTOR_SCREEN_X) + w / 2;
	int feet = (int) modRead32(player + LG2_ACTOR_SCREEN_Y) + h;
	int frame = (int) modFrame();
	int reach = w / 2 + 5;     // how far out from his middle it is at his sides
	int rounding = 7;          // the rounding under his feet
	int shoulders = h - 3;     // where the tongues over his head begin
	static const int flicker[8] = { 0, 2, 3, 2, 0, -2, -3, -2 };
	// each tongue: where it rises from (from his middle), from how high up,
	// how high it reaches above his feet, how wide its base is, and when it flickers
	struct { int at, from, peak, base, phase; } tongues[7] = {
		{ 0, shoulders, h + 19, reach, 0 },             // the tallest, over his head
		{ -(reach - 4), shoulders, h + 8, 6, 3 },
		{ reach - 4, shoulders, h + 11, 6, 6 },
		{ -(reach - 1), h / 2 - 2, h / 2 + 10, 4, 1 }, // licking up his sides
		{ reach - 1, h / 2 + 2, h / 2 + 14, 4, 5 },
		{ -(reach - 1), h / 4 - 1, h / 4 + 8, 3, 7 },
		{ reach - 1, h / 4 - 3, h / 4 + 6, 3, 2 },
	};
	int top = 0, hh, i;
	for (i = 0; i < 7; ++i) {
		tongues[i].peak += flicker[(frame / 3 + tongues[i].phase) & 7];
		tongues[i].at += flicker[(frame / 5 + tongues[i].phase * 3) & 7] / 3;
		if (tongues[i].peak > top) {
			top = tongues[i].peak;
		}
	}
	for (hh = 0; hh < top; ++hh) {
		int y = feet + 3 - hh, left = 1000, right = -1000, x, hw;
		int ripple = ((hh * 2 + frame) / 3) % 6 < 3; // ripples rising up its sides
		if (hh < shoulders) { // the body of it: rounded under his feet, fullest round his middle
			if (hh < rounding) {
				hw = (reach - 2) * isqrt(rounding * rounding - (rounding - hh) * (rounding - hh)) / rounding;
			} else {
				int span = shoulders - rounding;
				hw = reach - 2 + 3 * 4 * (hh - rounding) * (shoulders - hh) / (span * span) + ripple;
			}
			left = -hw;
			right = hw;
		}
		for (i = 0; i < 7; ++i) {
			int span = tongues[i].peak - tongues[i].from, q = tongues[i].peak - hh;
			if (hh < tongues[i].from || q <= 0 || span <= 0) {
				continue;
			}
			hw = tongues[i].base * q * q / (span * span); // curving in to a point
			if (tongues[i].at - hw < left) {
				left = tongues[i].at - hw;
			}
			if (tongues[i].at + hw > right) {
				right = tongues[i].at + hw;
			}
		}
		if (left > right) {
			continue;
		}
		left += cx;
		right += cx;
		blendDark(left - 1, y);
		blendDark(right + 1, y);
		blendWhite(left, y, 4);
		blendWhite(right, y, 4);
		blendWhite(left + 1, y, 2);
		blendWhite(right - 1, y, 2);
		for (x = left + 2; x <= right - 2; ++x) {
			if (((x + y + frame / 4) & 1) == 0) {
				blendWhite(x, y, 1);
			}
		}
	}
}

// ---- the M for Mystic -----------------------------------------------------------
// The attack box shows a Z for the transformation. For adult Gohan, whose
// transformation is the Mystic form, it shows an M instead, in the Z's style
// (looks.c draws the letter).
static const char* const mystic[6] = {
	"##.........##",
	"###.......###",
	"##.##...##.##",
	"##..##.##..##",
	"##...###...##",
	"##....#....##",
};

static void frame(void) {
	static uint32_t lastKeys;
	uint32_t pressed = modKeys() & ~lastKeys;
	lastKeys = modKeys();
	if (modPaused()) {
		return;
	}
	int onScreen = (pressed & (BTN_UP | BTN_DOWN)) && lg2SwitchScreenOpen() && lg2SwitchScreenChar() == LG2_GOHAN;
	if (onScreen) {
		wanted = (pressed & BTN_UP) && onScreen ? (wanted + LOOKS - 1) % LOOKS : (wanted + 1) % LOOKS;
		modSetSetting("adult_gohan", wanted);
		messageUntil = modFrame() + 120;
	}
	if (wanted != shown && !lg2FusionKind) {
		apply(wanted);
	}
	// Adult Gohan has his Mystic form whether or not the story has reached his
	// transformation yet: the slot is his once the look is on
	if (shown == ADULT && (lg2Char(LG2_GOHAN)->flags & 1) && !(lg2Char(LG2_GOHAN)->flags & LG2_ATTACK_OWNED(TRANSFORM_SLOT))) {
		lg2Char(LG2_GOHAN)->flags |= LG2_ATTACK_OWNED(TRANSFORM_SLOT);
		modLog("adult_gohan: the transformation slot is his now (Mystic)");
	}
	// Before the story changes his clothes he is in the Saiyan armour (form
	// 3), which "transforms" into the gi (2), not into a Super Saiyan form: the
	// slot would do nothing. The outfit does not show under this look, so
	// choosing the slot puts him in the gi, from which the slot works.
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	if (shown == ADULT && lg2Char(LG2_GOHAN)->sprite == KID_ARMOUR_FORM && lg2Char(LG2_GOHAN)->selectedAttack == TRANSFORM_SLOT &&
	    lg2CurrentChar() == LG2_GOHAN && lg2IsPlayer(player) && modCanCall() && !lg2FusionKind) {
		// the player object is set up for the form: changed the game's way
		uint32_t def = modRead32(player + LG2_ACTOR_SPRITE_DEF);
		lg2Char(LG2_GOHAN)->sprite = KID_CELL_SAGA_FORM;
		modCall(LG2_FN_PLAYER_REFRESH | 1, player, 0, 0, 0);
		lg2MoveActors(def, ((const uint32_t*) modRom(LG2_ROM_SPRITE_DEFS))[lg2FormDef(KID_CELL_SAGA_FORM)]);
		modLog("adult_gohan: out of the armour (form 3) into the gi (2), so the Mystic transformation can take");
	}
	if (shown == ADULT && (player >> 24) == 2 && lg2CurrentChar() == LG2_GOHAN && lg2Char(LG2_GOHAN)->hp > 0 &&
	    (lg2Char(LG2_GOHAN)->sprite >= FIRST_SUPER_SAIYAN || getenv("LG2_DEBUG_MYSTIC"))) {
		drawMysticAura(player);
	}
	if (shown == ADULT && (player >> 24) == 2 && lg2CurrentChar() == LG2_GOHAN && !modPaused() && !lg2FusionKind &&
	    lg2Char(LG2_GOHAN)->selectedAttack == TRANSFORM_SLOT && modScreen()[13 * 240 + 66] != 0) { // (the box's frame is up: not a fade)
		lg2DrawSlotLetter(mystic);
	}
	if (modFrame() < messageUntil) {
		modTextScale(2);
		modText(8, 30, 0x40FF40, wanted == ADULT ? "GOHAN: ADULT" : wanted == SAIYAMAN ? "GREAT SAIYAMAN" :
		        wanted == SAIYAMAN_BARE ? "SAIYAMAN, NO HELMET" : "GOHAN: KID");
		modTextScale(1);
	}
}

// Kid Gohan's Super Saiyan 2 sprite has no powering-down animation (the game
// never let him power down from that form; see looks.c): a copy of it with
// the last rows of his Super Saiyan sprite, the powering down among them,
// stands in. The two are both blond and the same size.
#define SSJ2_DEF 58
#define SSJ_DEF 59
#define SSJ2_DEF_COPY_ROM 0xFCF000

static void givePowerDown(void) {
	uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
	const uint8_t* ssj2 = modRom(table[SSJ2_DEF] - 0x08000000);
	const uint8_t* ssj = modRom(table[SSJ_DEF] - 0x08000000);
	uint8_t* copy = modRom(SSJ2_DEF_COPY_ROM);
	int count = ssj2[2], want = ssj[2], r;
	if (count >= want || ssj2[1] != 4 || ssj[1] != 4) {
		return;
	}
	memcpy(copy, ssj2, LG2_SPRITE_DEF_ANIMATIONS + count * 16);
	for (r = count; r < want; ++r) {
		memcpy(copy + LG2_SPRITE_DEF_ANIMATIONS + r * 16, ssj + LG2_SPRITE_DEF_ANIMATIONS + r * 16, 16);
	}
	// and the rows it has but left empty (the transformation pose, the
	// cheer that picking something up plays, getting up): the Super Saiyan
	// sprite's, so that those things work as a Super Saiyan 2
	for (r = 0; r < count; ++r) {
		static const uint8_t zero[16];
		if (!memcmp(copy + LG2_SPRITE_DEF_ANIMATIONS + r * 16, zero, 16) && memcmp(ssj + LG2_SPRITE_DEF_ANIMATIONS + r * 16, zero, 16)) {
			memcpy(copy + LG2_SPRITE_DEF_ANIMATIONS + r * 16, ssj + LG2_SPRITE_DEF_ANIMATIONS + r * 16, 16);
		}
	}
	copy[2] = (uint8_t) want;
	table[SSJ2_DEF] = 0x08000000 + SSJ2_DEF_COPY_ROM;
}

MOD_INIT(adult_gohan) {
	givePowerDown();
	// Kid Gohan's transformation goes straight to Super Saiyan 2, the form the
	// story gives him against Cell: his Cell-saga form (2) transformed into
	// Super Saiyan (5), which transformed again into Super Saiyan 2 (4)
	modRom(LG2_ROM_FORMS)[KID_CELL_SAGA_FORM * 20 + 4] = SSJ2_FORM;
	memcpy(modRom(adultGohanRomOffset), adultGohanData, adultGohanSize);
	memcpy(modRom(saiyamanRomOffset), saiyamanData, saiyamanSize);
	memcpy(modRom(saiyamanBareRomOffset), saiyamanBareData, saiyamanBareSize);
	memcpy(modRom(saiyamanBareSsjRomOffset), saiyamanBareSsjData, saiyamanBareSsjSize);
	wanted = modGetSetting("adult_gohan", 0) % LOOKS;
	modOnFrame(frame);
}
