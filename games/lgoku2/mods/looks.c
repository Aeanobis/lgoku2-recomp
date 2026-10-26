// Looks and faces brought over from Buu's Fury that are not Gohan's (his are
// in adult_gohan.c):
//
//   Vegeta  his Buu saga outfit (no armour), and Majin Vegeta when he is a
//           Super Saiyan
//   Trunks  kid Trunks, Super Saiyan and all
//
// Like Gohan's, these are chosen on the Switch Characters screen: with the
// character highlighted, Up or Down changes the look. The choice is remembered.
//
// One thing here is not a look but a transformation: Goku, while a Super
// Saiyan, can go on to Super Saiyan 3 and back (offered on the Start menu's
// Extras page). It adds a quarter to his strength and power and burns energy
// while it lasts; it ends by itself when the energy runs out or he stops being
// a Super Saiyan.
//
// This file also swaps the portraits (status screen and dialogue) for every
// look in the game, Gohan's and the fusions' included: whenever the game goes
// to unpack one of the original portraits, it is handed the converted one.
#include "lg2.h"

#include <string.h>

#include "generated/adult_gohan_portraits.inc"
#include "generated/goku_ssj3_data.inc"
#include "generated/goku_ssj_bf_data.inc"
#include "generated/kid_trunks_data.inc"
#include "generated/kid_trunks_ssj_data.inc"
#include "generated/look_portraits.inc"
#include "generated/vegeta_buu_data.inc"
#include "generated/vegeta_majin_data.inc"

enum { BTN_UP = 64, BTN_DOWN = 128 };

static unsigned messageUntil;
static const char* message = "";

static void say(const char* text) {
	message = text;
	messageUntil = modFrame() + 120;
}

// ---- sprite swaps -----------------------------------------------------------------------
// One entry of the game's sprite table pointed at a converted sprite
struct Swap {
	int def;          // sprite definition number in LG2_ROM_SPRITE_DEFS
	uint32_t with;    // game address of the replacement
	uint32_t own;     // the game's own, while swapped
	int on;
};

// `moveBack`: when undoing, whether actors drawn with the replacement belong to
// this entry (several entries can share one replacement)
static void swap(struct Swap* s, int on, int moveBack) {
	uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
	if (on == s->on) {
		return;
	}
	if (on) {
		s->own = table[s->def];
		table[s->def] = s->with;
		lg2MoveActors(s->own, s->with);
	} else {
		table[s->def] = s->own;
		if (moveBack) {
			lg2MoveActors(s->with, s->own);
		}
	}
	s->on = on;
}

// Vegeta's four sprites: two ordinary outfits, two Super Saiyan
static struct Swap vegeta[4] = { { 137 }, { 138 }, { 139 }, { 140 } };
static int vegetaWanted;

// Trunks's six: three ordinary outfits, then the three Super Saiyan ones
static struct Swap trunks[6] = { { 128 }, { 129 }, { 132 }, { 130 }, { 133 }, { 134 } };
static int trunksWanted;
int lg2TrunksLook;

// Goku's Super Saiyan sprite
static struct Swap gokuSsj = { 61 };
// Buu's Fury's Super Saiyan Goku, which has that game's power-up animation
// (the game's own Super Saiyan Goku has none): it stands in during the
// Super Saiyan 3 transformation scene
static struct Swap gokuSsjScene = { 61 };
static int ssj3;
static int ssj3Pending;        // the transformation scene is playing: Super Saiyan 3 at its flash
static unsigned ssj3PendingUntil;
static int ssj3Ending;         // the power-down scene is playing on the Super Saiyan 3 sprite
static unsigned ssj3EndingUntil;
static uint8_t gokuSsjNext;    // the form table's "next" of Goku's Super Saiyan form (his base form)
#define LG2_FN_CHANGE_FORM 0x08012720 // (player, 1 next / 0 revert): the moment a transformation scene changes the form
#define LG2_FN_CALL_R2 0x080221DC     // bx r2: calls a method (r0 this, r1 argument)
#define ACTION_TRANSFORM 0x23          // the player's action that plays the transformation scene
#define GOKU_SSJ_RECORD (LG2_ROM_FORMS + LG2_FORM_GOKU_SSJ * 20)
#define SSJ3_BOOST_PERCENT 160   // on top of the game's speed: Super Saiyan 3 hits much harder...
#define SECOND_LEVEL_BOOST_PERCENT 140 // Gohan's Super Saiyan 2
#define MYSTIC_BOOST_PERCENT 160       // adult Gohan's Mystic form: as Super Saiyan 3
#define TRANSFORMED_BOOST_PERCENT 120
#define SSJ3_FRAMES_PER_ENERGY 5 // ...and burns energy much faster (this is on top of the Super Saiyan drain, about 3 a second)

static void powerDown(void);

// Super Saiyan 3 ends. In the world, he powers all the way down to his base
// form through the game's power-down scene, played on the Super Saiyan 3
// sprite (Buu's Fury's own power-down animation); the Super Saiyan sprite
// is put back once the scene has changed his form. Anywhere else, at once.
static void endSsj3(const char* why) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	if (!ssj3) {
		return;
	}
	ssj3 = 0;
	if (why) {
		say(why);
	}
	if (why && lg2CurrentChar() == LG2_GOKU && lg2Char(LG2_GOKU)->sprite == LG2_FORM_GOKU_SSJ && lg2IsPlayer(player) && modCanCall() && !lg2FusionKind) {
		powerDown();
		ssj3Ending = 1;
		ssj3EndingUntil = modFrame() + 300;
	} else {
		swap(&gokuSsj, 0, 1);
	}
	if (lg2CurrentChar() == LG2_GOKU) {
		lg2SyncPlayerStats();
	}
}

// The transformation scene: Buu's Fury's Super Saiyan Goku stands in for the
// game's (which has no power-up animation), and the scene is started the
// way the slot starts it. At its flash the form change is caught (below).
static void beginSsj3(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	uint32_t class = modRead32(player);
	uint32_t method = class + modRead32(class + 0x28);
	swap(&gokuSsjScene, 1, 1);
	ssj3Pending = 1;
	ssj3PendingUntil = modFrame() + 400;
	modCall(LG2_FN_CALL_R2 | 1, player, ACTION_TRANSFORM, method, 0);
}

static int onChangeForm(uint32_t* r) {
	uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
	if (!ssj3Pending || r[1] != 1 || lg2CurrentChar() != LG2_GOKU || lg2Char(LG2_GOKU)->sprite != LG2_FORM_GOKU_SSJ) {
		return MOD_CONTINUE;
	}
	// the flash: Super Saiyan 3 from here. The form "changes" to itself, so
	// the game loads the sprite now in the table, the Super Saiyan 3 one
	ssj3Pending = 0;
	table[gokuSsjScene.def] = gokuSsjScene.own;
	gokuSsjScene.on = 0;
	swap(&gokuSsj, 1, 1);
	lg2MoveActors(gokuSsjScene.with, gokuSsj.with);
	modRom(GOKU_SSJ_RECORD)[4] = LG2_FORM_GOKU_SSJ;
	ssj3 = 1;
	say("SUPER SAIYAN 3");
	modLog("looks: Super Saiyan 3 at the flash of the transformation scene");
	return MOD_CONTINUE;
}

// Scenes left unfinished (a hit, a menu, a room change) are tidied here
static void ssj3ScenesFrame(void) {
	int goku = lg2CurrentChar() == LG2_GOKU && lg2Char(LG2_GOKU)->sprite == LG2_FORM_GOKU_SSJ && lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR));
	if (modRom(GOKU_SSJ_RECORD)[4] != gokuSsjNext) {
		modRom(GOKU_SSJ_RECORD)[4] = gokuSsjNext; // (the flash is over)
	}
	if (ssj3Pending && (!goku || modFrame() > ssj3PendingUntil)) {
		ssj3Pending = 0;
		swap(&gokuSsjScene, 0, 1);
		modLog("looks: the Super Saiyan 3 transformation scene did not finish");
	}
	if (ssj3Ending && (!goku || modFrame() > ssj3EndingUntil)) {
		ssj3Ending = 0;
		swap(&gokuSsj, 0, 1);
	}
}

int lg2Ssj3On(void) {
	return ssj3;
}

// A transformation or power-down scene is under way, with the player object
// deliberately on a sprite that is not his form's
int lg2LooksSceneBusy(void) {
	return ssj3Pending || ssj3Ending;
}

// Goku's transformation slot is his from the start of the game; a record found
// without it (an earlier build's stop on L's round could take it away when a
// fusion ended) gets it back
static void keepGokuSlot(void) {
	struct LG2Char* c = lg2Char(LG2_GOKU);
	if ((c->flags & 1) && !(c->flags & LG2_ATTACK_OWNED(2)) && !lg2FusionKind && (c->sprite == LG2_FORM_GOKU_BASE || c->sprite == LG2_FORM_GOKU_SSJ)) {
		c->flags |= LG2_ATTACK_OWNED(2);
		modLog("looks: Goku's transformation slot was missing from his record: given back");
	}
}

int lg2Ssj3Possible(void) {
	return lg2CurrentChar() == LG2_GOKU && lg2Char(LG2_GOKU)->sprite == LG2_FORM_GOKU_SSJ && !lg2FusionKind;
}

void lg2ToggleSsj3(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	if (ssj3) {
		endSsj3("POWERING DOWN");
	} else if (ssj3Pending || ssj3Ending) {
		return; // a scene is still playing
	} else if (lg2Ssj3Possible() && lg2Char(LG2_GOKU)->ep > 0) {
		if (lg2IsPlayer(player) && modCanCall()) {
			beginSsj3();
		} else { // not in the world: at once
			ssj3 = 1;
			swap(&gokuSsj, 1, 1);
			say("SUPER SAIYAN 3");
		}
	}
}

// ---- portraits --------------------------------------------------------------------------
// The game's portrait numbers. 0-17 are the status screen's.
enum {
	STATUS_GOHAN_LAST = 5, STATUS_GOKU = 6, STATUS_GOKU_SSJ = 7, STATUS_VEGETA = 16, STATUS_VEGETA_SSJ = 17, STATUS_PORTRAITS = 18,
	TALK_GOHAN_A = 48, TALK_GOHAN_B = 50, TALK_GOHAN_C = 51, TALK_GOHAN_D = 52, TALK_GOHAN_SSJ_A = 53, TALK_GOHAN_SSJ_B = 54,
	TALK_GOKU = 55, TALK_GOKU_SSJ = 57, TALK_VEGETA = 98, TALK_VEGETA_SSJ = 99,
	STATUS_TRUNKS_FIRST = 11, STATUS_TRUNKS_SSJ_A = 13, STATUS_TRUNKS_SSJ_B = 14, STATUS_TRUNKS_LAST = 15,
	TALK_TRUNKS_FIRST = 90, TALK_TRUNKS_SSJ_A = 93, TALK_TRUNKS_SSJ_B = 94,
};
// Converted portraits in look_portraits.inc, in the order the conversion script lists them
enum { P_SAIYAMAN = 0, P_GOGETA = 2, P_VEGITO = 4, P_SSJ3 = 6, P_VEGETA = 8, P_MAJIN = 10, P_BARE = 12, P_BARE_SSJ = 14,
       P_KID_TRUNKS = 16, P_KID_TRUNKS_SSJ = 18, P_GOTENKS = 20, P_GOTENKS_SSJ = 22 }; // +1 = dialogue version

static uint32_t lookPortrait(int index) {
	return 0x08000000 + lookPortraitsRomOffset + lookPortraitsAt[index];
}

static uint32_t adultGohanPortrait(int index) {
	return 0x08000000 + adultGohanPortraitsRomOffset + adultGohanPortraitsAt[index];
}

// The replacement for one of the game's portraits, or 0 to leave it alone
static uint32_t replacementFor(int portrait) {
	int status = portrait < STATUS_PORTRAITS;
	int who = -1;
	if (portrait <= STATUS_GOHAN_LAST || portrait == TALK_GOHAN_A || (portrait >= TALK_GOHAN_B && portrait <= TALK_GOHAN_SSJ_B)) {
		who = LG2_GOHAN;
	} else if (portrait == STATUS_GOKU || portrait == STATUS_GOKU_SSJ || portrait == TALK_GOKU || portrait == TALK_GOKU_SSJ) {
		who = LG2_GOKU;
	} else if (portrait == STATUS_VEGETA || portrait == STATUS_VEGETA_SSJ || portrait == TALK_VEGETA || portrait == TALK_VEGETA_SSJ) {
		who = LG2_VEGETA;
	} else if ((portrait >= STATUS_TRUNKS_FIRST && portrait <= STATUS_TRUNKS_LAST) || (portrait >= TALK_TRUNKS_FIRST && portrait <= TALK_TRUNKS_SSJ_B)) {
		who = LG2_TRUNKS;
	}
	if (who < 0) {
		return 0;
	}
	if (lg2FusionKind && who == lg2FusionHost) {
		if (lg2FusionKind == 3) {
			int super = portrait == STATUS_TRUNKS_SSJ_A || portrait == STATUS_TRUNKS_SSJ_B || portrait == TALK_TRUNKS_SSJ_A || portrait == TALK_TRUNKS_SSJ_B;
			return lookPortrait((super ? P_GOTENKS_SSJ : P_GOTENKS) + !status);
		}
		return lookPortrait((lg2FusionKind == 2 ? P_VEGITO : P_GOGETA) + !status);
	}
	if (who == LG2_GOHAN) {
		int super = portrait == 4 || portrait == 5 || portrait == TALK_GOHAN_SSJ_A || portrait == TALK_GOHAN_SSJ_B;
		return lg2GohanLook == 1 ? adultGohanPortrait(status ? 0 : 2) : lg2GohanLook == 2 ? lookPortrait(P_SAIYAMAN + !status) :
		       lg2GohanLook == 3 ? lookPortrait((super ? P_BARE_SSJ : P_BARE) + !status) : 0;
	}
	if (who == LG2_GOKU) {
		return ssj3 && (portrait == STATUS_GOKU_SSJ || portrait == TALK_GOKU_SSJ) ? lookPortrait(P_SSJ3 + !status) : 0;
	}
	if (who == LG2_TRUNKS) {
		int super = portrait == STATUS_TRUNKS_SSJ_A || portrait == STATUS_TRUNKS_SSJ_B || portrait == TALK_TRUNKS_SSJ_A || portrait == TALK_TRUNKS_SSJ_B;
		return trunks[0].on ? lookPortrait((super ? P_KID_TRUNKS_SSJ : P_KID_TRUNKS) + !status) : 0;
	}
	if (vegeta[0].on) {
		int super = portrait == STATUS_VEGETA_SSJ || portrait == TALK_VEGETA_SSJ;
		return lookPortrait((super ? P_MAJIN : P_VEGETA) + !status);
	}
	return 0;
}

static int onUnpack(uint32_t* r) {
	const uint32_t* table = (const uint32_t*) modRom(LG2_ROM_PORTRAITS);
	int i;
	for (i = 0; i < 107; ++i) {
		if (r[0] == table[i]) {
			uint32_t with = replacementFor(i);
			if (with) {
				r[0] = with;
			}
			break;
		}
	}
	return MOD_CONTINUE;
}

// ---- every frame --------------------------------------------------------------------------
// ---- letters over the attack box ------------------------------------------------
// The game shows a Z for a transformation. Some get their own letter, drawn in
// the Z's style (yellow, lit along the top, shaded at the foot, leaning) over
// the game's icon: adult Gohan's M (adult_gohan.c), Piccolo's N.
void lg2DrawSlotLetter(const char* const rows[6]) {
	enum { BOX_X = 68, BOX_Y = 10, BOX_W = 18, BOX_H = 8 }; // the inside of the game's attack box
	int x, y;
	modRect(BOX_X, BOX_Y, BOX_W, BOX_H, 0x000000);
	for (y = 0; y < 6; ++y) {
		int slant = (5 - y) / 3;
		for (x = 0; x < 13; ++x) {
			if (rows[y][x] == '#') {
				uint32_t colour = y == 0 ? 0xBDFFFF : y == 5 ? 0x006B6B : 0x00FFFF; // (BGR)
				int sx = BOX_X + 2 + x + slant, sy = BOX_Y + 1 + y;
				if (sx >= BOX_X && sx < BOX_X + BOX_W && sy < BOX_Y + BOX_H) {
					modScreen()[sy * 240 + sx] = colour;
				}
			}
		}
	}
}

// Piccolo's transformation (his Namekian power-up) gets an N
static const char* const namekian[6] = {
	"##.........##",
	"###........##",
	"##.##......##",
	"##...##....##",
	"##.....##..##",
	"##.......####",
};

// ---- turning a transformation off -----------------------------------------------
// A Super Saiyan can press the slot again to go back: the "next" form of a
// Super Saiyan form is the base one, and the slot takes him there (through
// the transformation scene). A second-level form (Gohan's Super Saiyan 2, so
// his Mystic form too) could not use the slot again: it lasted until the
// energy ran out. Now it can; its next form is the base one already.
#define LG2_FN_TRANSFORM_USABLE 0x0801749E // (character record, player) -> 1 if the transformation slot can be used now

// ---- the Super Saiyan 3 stop --------------------------------------------------
// Super Saiyan 3 has a place of its own in L's round of the attack slots:
// right after the transformation (the Z), while Goku is a Super Saiyan. The
// game has only the four slots, so the stop is the Z's slot taken twice: on
// the second visit the box shows the Super Saiyan 3 icon and B turns the
// form on or off, instead of the transformation.
#define LG2_FN_TRANSFORM_USE 0x080174E8 // (character record, player): the transformation slot was used
enum { TRANSFORM_SLOT = 2, BLAST_SLOT = 3 };
static int ssj3Stop;
static int ownUse; // the slot is being used by this file: let the game have it
static uint8_t lastSelected = 255;

static int onTransformUsable(uint32_t* r) {
	int form = modRead8(r[0] + 8);
	if (ssj3Stop || (modRom(LG2_ROM_FORMS)[form * 20] & 2)) {
		r[0] = 1;
		return MOD_RETURN;
	}
	return MOD_CONTINUE;
}

static int onTransformUse(uint32_t* r) {
	if (ssj3Stop && !ownUse) {
		lg2ToggleSsj3();
		return MOD_RETURN;
	}
	return MOD_CONTINUE;
}

// Goku powers down to his base form the game's way: the transformation slot
// used while a Super Saiyan (the power-down scene). Super Saiyan 3 ending
// takes him all the way down.
static void powerDown(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	if (lg2CurrentChar() == LG2_GOKU && lg2Char(LG2_GOKU)->sprite == LG2_FORM_GOKU_SSJ && lg2IsPlayer(player) && modCanCall() && !lg2FusionKind) {
		ownUse = 1;
		modCall(LG2_FN_TRANSFORM_USE | 1, LG2_RAM_CHARDATA + 12 + LG2_GOKU * sizeof(struct LG2Char), player, 0, 0);
		ownUse = 0;
	}
}

// L goes round the slots upwards (R does not move it). The stop is entered
// when L leaves the Z for the blast, and left when L is pressed again.
static void ssj3StopFrame(int playing) {
	struct LG2Char* c = lg2Char(LG2_GOKU);
	int possible = playing && lg2Ssj3Possible() && c->hp > 0;
	uint8_t selected = c->selectedAttack;
	if (!possible) {
		ssj3Stop = 0;
		lastSelected = 255;
		return;
	}
	if (lastSelected != 255 && selected != lastSelected) {
		if (!ssj3Stop && lastSelected == TRANSFORM_SLOT && selected == BLAST_SLOT) {
			ssj3Stop = 1;
			c->selectedAttack = TRANSFORM_SLOT;
		} else {
			ssj3Stop = 0;
		}
	}
	lastSelected = c->selectedAttack;
}

// The Super Saiyan 3 icon over the box while the stop is chosen: Buu's Fury's
// own (its HUD icon bank, ROM 0x718ED8: a Z with a 3; the glyph alone, taken
// against the empty box at 0x714AD8, without that game's box), 16 x 7, in its
// colours: 0 is see-through, else 0xFF000000 | the colour (blue, green, red bytes)
enum { SSJ3_GLYPH_W = 16, SSJ3_GLYPH_H = 7 };
static const uint32_t ssj3Glyph[SSJ3_GLYPH_H][SSJ3_GLYPH_W] = {
	{ 0, 0xFF005858, 0xFF00F8F8, 0xFF48F8F8, 0xFF90F8F8, 0xFFB8F8F8, 0xFFD8F8F8, 0xFFD8F8F8, 0xFFD8F8F8, 0xFFB8F8F8, 0xFF90F8F8, 0xFF48F8F8, 0xFF00F8F8, 0xFF005858, 0xFF000000, 0 },
	{ 0xFF005858, 0xFF00F8F8, 0xFF00A8A8, 0xFF005858, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF909090, 0xFFD8F8F8, 0xFFB8F8F8, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000 },
	{ 0, 0, 0, 0, 0, 0, 0xFF909090, 0xFFF8F8F8, 0xFFF8F8F8, 0xFF909090, 0xFF000000, 0xFF000000, 0xFFF8F860, 0, 0xFFF8F860, 0xFF000000 },
	{ 0xFF000000, 0xFF000000, 0xFF000000, 0xFF909090, 0xFFD8F8F8, 0xFFF8F8F8, 0xFFF8F8F8, 0xFFF8F8F8, 0xFFF8F8F8, 0xFFF8F8F8, 0xFF000000, 0, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFF8F860 },
	{ 0, 0, 0, 0, 0xFF909090, 0xFFF8F8F8, 0xFFF8F8F8, 0xFF909090, 0, 0, 0xFF000000, 0xFF000000, 0xFF000000, 0, 0xFFF8F860, 0xFF000000 },
	{ 0xFF000000, 0xFF000000, 0xFF005858, 0xFF58E8F8, 0xFFB8F8F8, 0xFF909090, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFF8F860 },
	{ 0xFF005858, 0xFF00F8F8, 0xFF00F8F8, 0xFF48F8F8, 0xFF90F8F8, 0xFFB8F8F8, 0xFFD8F8F8, 0xFFD8F8F8, 0xFFD8F8F8, 0xFFB8F8F8, 0, 0, 0xFFF8F860, 0xFFF8F860, 0xFFF8F860, 0xFF000000 },
};

static void drawSsj3Icon(void) {
	enum { BOX_X = 68, BOX_Y = 10, BOX_W = 18, BOX_H = 8 }; // the inside of the game's attack box
	int x, y;
	modRect(BOX_X, BOX_Y, BOX_W, BOX_H, 0x000000);
	for (y = 0; y < SSJ3_GLYPH_H; ++y) {
		for (x = 0; x < SSJ3_GLYPH_W; ++x) {
			if (ssj3Glyph[y][x]) {
				modScreen()[(BOX_Y + (BOX_H - SSJ3_GLYPH_H) / 2 + y) * 240 + BOX_X + (BOX_W - SSJ3_GLYPH_W) / 2 + x] = ssj3Glyph[y][x] & 0xFFFFFF;
			}
		}
	}
}

static void frame(void) {
	keepGokuSlot();
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	int playing = (player >> 24) == 2 && !modPaused();
	if (playing && lg2CurrentChar() == LG2_PICCOLO && !lg2FusionKind && lg2Char(LG2_PICCOLO)->selectedAttack == TRANSFORM_SLOT &&
	    modScreen()[13 * 240 + 66] != 0) {
		lg2DrawSlotLetter(namekian);
	}
	ssj3ScenesFrame();
	ssj3StopFrame(playing);
	{ // the runtime went back to a snapshot after a crash: tell the player
		static unsigned told;
		if (modRecoveredFrame && modRecoveredFrame != told) {
			told = modRecoveredFrame;
			say("CRASH: BACK 10 SECONDS");
		}
	}
	if (ssj3Stop && modScreen()[13 * 240 + 66] != 0) {
		drawSsj3Icon();
	}
	int who = lg2CurrentChar();
	unsigned i;

	// Looks are changed on the Switch Characters screen: Up or Down on the highlighted character
	static uint32_t lastKeys;
	uint32_t pressed = modKeys() & ~lastKeys;
	lastKeys = modKeys();
	if (!modPaused() && lg2SwitchScreenOpen()) {
		int shown = lg2SwitchScreenChar();
		if (shown == LG2_VEGETA && (pressed & (BTN_UP | BTN_DOWN))) {
			vegetaWanted = !vegetaWanted;
			modSetSetting("vegeta_look", vegetaWanted);
			say(vegetaWanted ? "VEGETA: BUU SAGA" : "VEGETA: ARMOUR");
		}
		if (shown == LG2_TRUNKS && (pressed & (BTN_UP | BTN_DOWN))) {
			trunksWanted = !trunksWanted;
			modSetSetting("trunks_look", trunksWanted);
			say(trunksWanted ? "TRUNKS: KID" : "TRUNKS: FUTURE");
		}
		if (shown == LG2_GOHAN || shown == LG2_VEGETA || shown == LG2_TRUNKS) {
			modText(72, 150, 0xC0C0C0, "UP / DOWN: CHANGE LOOK");
		}
	}
	if (!lg2FusionKind) { // a fusion has one of these sprites on loan: leave the table alone meanwhile
		for (i = 0; i < 6; ++i) {
			swap(&trunks[i], trunksWanted, trunks[i].def == lg2FormDef(lg2Char(LG2_TRUNKS)->sprite));
		}
		lg2TrunksLook = trunks[0].on;
		for (i = 0; i < 4; ++i) {
			swap(&vegeta[i], vegetaWanted, vegeta[i].def == lg2FormDef(lg2Char(LG2_VEGETA)->sprite));
		}
	}

	int superGoku = who == LG2_GOKU && lg2Char(LG2_GOKU)->sprite == LG2_FORM_GOKU_SSJ && !lg2FusionKind;
	if (ssj3 && !superGoku) {
		endSsj3(0);
	}
	// Transformations boost the stats (the game's own only make you faster and
	// drain energy), by level: a Super Saiyan +20%, Gohan's Super Saiyan 2
	// (the form table's second-level flag) +40%, Super Saiyan 3 and adult
	// Gohan's Mystic form +60%. The form table's first byte marks the forms.
	int formFlags = lg2FusionKind ? 0 : modRom(LG2_ROM_FORMS)[lg2Char(who)->sprite * 20];
	int mystic = (formFlags & 1) && who == LG2_GOHAN && lg2GohanLook == 1;
	lg2StatBoostPercent = ssj3 ? SSJ3_BOOST_PERCENT : mystic ? MYSTIC_BOOST_PERCENT : (formFlags & 2) ? SECOND_LEVEL_BOOST_PERCENT :
	                      (formFlags & 1) ? TRANSFORMED_BOOST_PERCENT : 100;
	if (ssj3 && playing && !lg2OnSavePad()) { // (a save pad keeps the energy full: no drain there)
		struct LG2Char* c = lg2Char(LG2_GOKU);
		if (modFrame() % SSJ3_FRAMES_PER_ENERGY == 0) {
			if (c->ep > 0) {
				--c->ep;
			}
			if (c->ep == 0) {
				endSsj3("SUPER SAIYAN 3 WORE OFF");
			}
		}
	}

	if (!modPaused() && modFrame() < messageUntil) {
		modTextScale(2);
		modText(8, 44, 0x40FFFF, "%s", message);
		modTextScale(1);
	}
}

MOD_INIT(looks) {
	modHook(LG2_FN_TRANSFORM_USABLE, onTransformUsable);
	modHook(LG2_FN_TRANSFORM_USE, onTransformUse);
	modHook(LG2_FN_CHANGE_FORM, onChangeForm);
	memcpy(modRom(gokuSsjBfRomOffset), gokuSsjBfData, gokuSsjBfSize);
	gokuSsjScene.with = 0x08000000 + gokuSsjBfRomOffset;
	gokuSsjNext = modRom(GOKU_SSJ_RECORD)[4];
	memcpy(modRom(adultGohanPortraitsRomOffset), adultGohanPortraitsData, adultGohanPortraitsSize);
	memcpy(modRom(lookPortraitsRomOffset), lookPortraitsData, lookPortraitsSize);
	memcpy(modRom(vegetaBuuRomOffset), vegetaBuuData, vegetaBuuSize);
	memcpy(modRom(vegetaMajinRomOffset), vegetaMajinData, vegetaMajinSize);
	memcpy(modRom(gokuSsj3RomOffset), gokuSsj3Data, gokuSsj3Size);
	vegeta[0].with = vegeta[1].with = 0x08000000 + vegetaBuuRomOffset;
	vegeta[2].with = vegeta[3].with = 0x08000000 + vegetaMajinRomOffset;
	gokuSsj.with = 0x08000000 + gokuSsj3RomOffset;
	memcpy(modRom(kidTrunksRomOffset), kidTrunksData, kidTrunksSize);
	memcpy(modRom(kidTrunksSsjRomOffset), kidTrunksSsjData, kidTrunksSsjSize);
	for (int i = 0; i < 6; ++i) {
		trunks[i].with = 0x08000000 + (i < 3 ? kidTrunksRomOffset : kidTrunksSsjRomOffset);
	}
	trunksWanted = modGetSetting("trunks_look", 0);
	vegetaWanted = modGetSetting("vegeta_look", 0);
	modHook(LG2_FN_UNPACK, onUnpack);
	modOnFrame(frame);
}
