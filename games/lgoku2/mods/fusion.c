// Fusion, chosen the way Buu's Fury does it: while playing Goku or Vegeta, L
// goes round the attacks as usual and then, after the energy blast, on to
// "Gogeta" and "Vegito" before coming back to the first attack. With one of
// them chosen, B fuses: the other one appears beside you, the two do the
// fusion dance (the Potara fusion skips the dance), and you are Gogeta for a
// minute or Vegito for half a minute.
//
// Both are Goku and Vegeta fused, so the level is their two levels added
// together and multiplied: by 1.25 for Gogeta, by 1.5 for Vegito (up to the
// level cap). If the other one has not joined you yet, the one you are playing
// counts twice. "Gogeta" below means whichever fusion it is.
//
// The game only has room for six characters, so Gogeta does not get a slot of
// his own. Instead the character you are playing is turned into him: Gogeta's
// sprite (converted from Buu's Fury), stats worked out from his level, full
// health and energy, and Goku's attacks. When the minute is up, the character
// comes back exactly as they were, plus any experience Gogeta earned. If
// Gogeta is beaten first, the fusion simply ends early.
//
// What the character was before is also written to the settings file, so that
// if a save or save state is ever made while fused, loading it later puts the
// character back instead of leaving them as Gogeta for good.
#include "lg2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "generated/fusion_icon.inc"
#include "generated/gogeta_data.inc"
#include "generated/goku_dance.inc"
#include "generated/goten_dance.inc"
#include "generated/gotenks_data.inc"
#include "generated/gotenks_ssj_data.inc"
#include "generated/kid_trunks_dance.inc"
#include "generated/vegeta_dance.inc"
#include "generated/vegito_data.inc"

#define COOLDOWN_SECONDS 120
#define FPS 60


// The two fusions. Gogeta is the fusion dance: a minute at 1.25 times the
// two levels. Vegito is the Potara earrings: half again the two
// levels, but only for half a minute.
//
// Gotenks is kid Trunks and Goten: offered to Trunks while he has his kid look
// on. Goten is not in this game, so Trunks's level counts twice.
enum { GOGETA = 1, VEGITO = 2, GOTENKS = 3 };
static const struct Kind { const char* name; double factor; int seconds; } kinds[] = {
	{ "", 0, 0 }, { "GOGETA", 1.25, 60 }, { "VEGITO", 1.5, 30 }, { "GOTENKS", 1.25, 60 },
};
static int gotenksSuper; // Trunks was a Super Saiyan when he fused: Gotenks is one too
int lg2FusionKind;
int lg2FusionHost = -1;

static int fused;             // frames left
static int cooldown;          // frames until fusion is allowed again
static void leaveThird(int resume);
static void repoint(uint32_t actor, uint32_t to);
static int scene;             // frames into the fusion scene, 0 = none (below)
static int kind;              // GOGETA or VEGITO while fused
static int host = -1;         // which character became the fusion
static struct LG2Char original;
static struct LG2Char marker; // what the fused record looked like when made
static uint32_t hostDef;      // the sprite definition that was swapped out (game address)
static int hostDefNumber;
static unsigned messageUntil;
static const char* message = "";

static uint32_t fusedDef(void) {
	return 0x08000000 + (kind == VEGITO ? vegitoRomOffset : kind == GOTENKS ? (gotenksSuper ? gotenksSsjRomOffset : gotenksRomOffset) :
	                     gogetaRomOffset);
}

static void say(const char* text) {
	message = text;
	messageUntil = modFrame() + 150;
}

static void remember(void) {
	const uint32_t* o = (const uint32_t*) &original;
	const uint32_t* m = (const uint32_t*) &marker;
	char name[32];
	int i;
	modSetSetting("fusion_host", host);
	modSetSetting("fusion_kind", kind);
	for (i = 0; i < 7; ++i) {
		snprintf(name, sizeof(name), "fusion_original_%d", i);
		modSetSetting(name, (int) o[i]);
		snprintf(name, sizeof(name), "fusion_marker_%d", i);
		modSetSetting(name, (int) m[i]);
	}
}

static void recall(void) {
	uint32_t* o = (uint32_t*) &original;
	uint32_t* m = (uint32_t*) &marker;
	char name[32];
	int i;
	host = modGetSetting("fusion_host", -1);
	kind = modGetSetting("fusion_kind", 0);
	for (i = 0; i < 7; ++i) {
		snprintf(name, sizeof(name), "fusion_original_%d", i);
		o[i] = (uint32_t) modGetSetting(name, 0);
		snprintf(name, sizeof(name), "fusion_marker_%d", i);
		m[i] = (uint32_t) modGetSetting(name, 0);
	}
}

static int levelFor(int who, int fallback) {
	const struct LG2Char* c = lg2Char(who);
	return (c->flags & 1) ? lg2Level(c) : fallback;
}

// The game writes the host's name (status page, dialogue) from its text
// table. While fused, the English line for that name points at the fusion's
// name instead, kept in the unused top of the ROM.
#define NAME_ROM 0x8F3400
static uint32_t nameLineWas;
static int nameLine = -1;

static void setName(int who, const char* name) {
	// the names as the menus show them ("Vegeta"; lines 54-59 are the spoken "VEGETA:" labels)
	static const int lines[LG2_CHARACTERS] = { 48, 49, 50, 51, 52, 53 };
	uint32_t* line = (uint32_t*) (modRom(LG2_ROM_TEXT) + lines[who] * 20);
	int k; // the name line points at the one string in all five languages
	if (name) {
		strcpy((char*) modRom(NAME_ROM), name);
		nameLineWas = line[0];
		nameLine = who;
		for (k = 0; k < 5; ++k) {
			line[k] = 0x08000000 + NAME_ROM;
		}
	} else if (nameLine == who) {
		for (k = 0; k < 5; ++k) {
			line[k] = nameLineWas;
		}
		nameLine = -1;
	}
}

static void fuse(int which) {
	int who = lg2CurrentChar();
	leaveThird(0);
	struct LG2Char* c = lg2Char(who);
	// the two who fuse: Goku and Vegeta, or for Gotenks, Trunks twice over
	const struct LG2Char* goku = which == GOTENKS ? c : lg2Char(LG2_GOKU);
	const struct LG2Char* vegeta = which == GOTENKS ? c : lg2Char(LG2_VEGETA);
	int own = lg2Level(c);
	const double LEVEL_FACTOR = kinds[which].factor;
	// the fusion's level: the higher of the two who fuse, times the kind's factor
	// (a 39 and a 35 make a Gogeta of 49, a Vegito of 59)
	int gokuLevel = levelFor(LG2_GOKU, own), vegetaLevel = levelFor(LG2_VEGETA, own);
	int level = (int) ((which == GOTENKS ? own : gokuLevel > vegetaLevel ? gokuLevel : vegetaLevel) * LEVEL_FACTOR + 0.5);
	gotenksSuper = modRom(LG2_ROM_FORMS)[c->sprite * 20] & 1; // the form table marks the Super Saiyan forms
	int i;
	if (level > LG2_LEVEL_CAP) {
		level = LG2_LEVEL_CAP;
	}
	host = lg2FusionHost = who;
	kind = lg2FusionKind = which;
	original = *c;

	// A character of that level: health grows about 6.5% a level from 100,
	// stats by four points a level shared between the three
	double hp = 100;
	for (i = 1; i < level; ++i) {
		hp *= i < 50 ? 1.065 : 1.0325;
	}
	c->maxHp = c->hp = hp > 30000 ? 30000 : (uint16_t) hp;
	int ep = 20 + 3 * (level - 1);
	if (ep < original.maxEp) {
		ep = original.maxEp;
	}
	c->maxEp = c->ep = ep;
	// Gogeta and Gotenks are the measure (factor 1.25); Vegito, at 1.5, is
	// that much stronger again in health and stats, since the level cap
	// would otherwise make them all the same
	double edge = LEVEL_FACTOR / 1.25;
	c->maxHp = c->hp = (uint16_t) (c->maxHp * edge > 30000 ? 30000 : c->maxHp * edge);
	for (i = 0; i < LG2_STATS; ++i) {
		int byLevel = 10 + (level - 1) * 4 / 3;
		int byParents = (int) (((goku->stat[i] >> 8) + (vegeta->stat[i] >> 8)) / 2 * LEVEL_FACTOR);
		int value = (int) ((byLevel > byParents ? byLevel : byParents) * edge);
		c->stat[i] = (value > LG2_STAT_CAP ? LG2_STAT_CAP : value) << 8;
	}
	c->xp = lg2XpForLevel(level);
	// Goku's attacks stand in the two special slots (fusion_moves.c plays the
	// fusion's own instead, and keeps the game from firing these), then the
	// energy blast. Slot 2 (transforming) is left out.
	memcpy(c->attack, ((const struct LG2Char*) modRom(LG2_ROM_CHARS) + LG2_GOKU)->attack, 4);
	c->flags = (original.flags & ~0x78u) | LG2_ATTACK_OWNED(0) | LG2_ATTACK_OWNED(1) | LG2_ATTACK_OWNED(3);
	c->selectedAttack = 3;
	marker = *c;
	remember();

	uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
	hostDefNumber = lg2FormDef(c->sprite);
	hostDef = table[hostDefNumber];
	table[hostDefNumber] = fusedDef();
	lg2MoveActors(hostDef, fusedDef());
	{ // whatever sprite the player object was on (a look, say), he is the fusion now
		uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
		if (lg2IsPlayer(player) && modRead32(player + LG2_ACTOR_SPRITE_DEF) != fusedDef()) {
			repoint(player, fusedDef());
		}
	}
	lg2SyncPlayerStats();
	setName(who, kinds[which].name);
	fused = kinds[which].seconds * FPS;
	modLog("fused: %s as %s, level %d, sprite table[%d] %08X -> %08X", lg2Name(who), kinds[which].name, level, hostDefNumber, hostDef, fusedDef());
	say(which == VEGITO ? "FUSION! VEGITO" : which == GOTENKS ? "FUSION! GOTENKS" : "FUSION! GOGETA");
}

// Is this one of the fusions' sprites?
static int isFusedDef(uint32_t def) {
	return def == 0x08000000 + gogetaRomOffset || def == 0x08000000 + vegitoRomOffset || def == 0x08000000 + gotenksRomOffset ||
	       def == 0x08000000 + gotenksSsjRomOffset;
}

// Points an actor at another sprite, keeping its place in the animation if
// the new sprite has that animation, else standing
static void repoint(uint32_t actor, uint32_t to) {
	static const uint8_t zero[16];
	uint32_t from = modRead32(actor + LG2_ACTOR_SPRITE_DEF);
	int number = (int) (modRead32(actor + LG2_ACTOR_ANIMATION) - from - LG2_SPRITE_DEF_ANIMATIONS) / 16;
	int ok = number >= 0 && number < modRom(to - 0x08000000)[2] && memcmp(modRom(to - 0x08000000 + LG2_SPRITE_DEF_ANIMATIONS + number * 16), zero, 16);
	modWrite32(actor + LG2_ACTOR_SPRITE_DEF, to);
	if (ok) {
		modWrite32(actor + LG2_ACTOR_ANIMATION, to + LG2_SPRITE_DEF_ANIMATIONS + 16 * number);
	} else {
		modWrite16(actor + 0x12, 0);
		modWrite16(actor + LG2_ACTOR_FRAME, 0);
		modWrite8(actor + 8, 0x28);
		modWrite32(actor + LG2_ACTOR_ANIMATION, to + LG2_SPRITE_DEF_ANIMATIONS);
	}
}

// Not fused, yet the player is still drawn as a fusion (the swap back missed
// him: he was mid-scene, or the game re-made him in between): put him on his
// own sprite. Checked every frame, so it cannot stay wrong until the next area.
static void keepOwnSprite(void) {
	static int wrongFor; // frames the player has been drawn with a sprite that is not his character's
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	int who = lg2CurrentChar();
	uint32_t def, own;
	if (!lg2IsPlayer(player) || who < 0 || who >= LG2_CHARACTERS || scene || lg2LooksSceneBusy() || !modCanCall() || modPaused()) {
		wrongFor = 0;
		return;
	}
	def = modRead32(player + LG2_ACTOR_SPRITE_DEF);
	own = ((const uint32_t*) modRom(LG2_ROM_SPRITE_DEFS))[lg2FormDef(lg2Char(who)->sprite)];
	if (def == own || isFusedDef(own)) { // (the table itself still saying fusion is not for this to undo)
		wrongFor = 0;
		return;
	}
	// A fusion's sprite goes at once. Any other sprite than his own (another
	// character's, say: one Gohan was once left with after a fusion ended on a
	// switch) goes after half a second, in case a scene is changing him.
	if (isFusedDef(def) || ++wrongFor >= 30) {
		modLog("player drawn with %08X, not %s's own sprite %08X: put on his own", def, lg2Name(who), own);
		repoint(player, own);
		wrongFor = 0;
	}
}

// Puts the host character back. `live` is false when undoing a fusion found in
// a loaded save, where there is no sprite swap to undo.
static void defuse(int live) {
	struct LG2Char* c = lg2Char(host);
	uint32_t earned = c->xp > marker.xp ? c->xp - marker.xp : 0;
	uint8_t formNow = c->sprite;
	setName(host, 0);
	{
		uint32_t pl = modRead32(LG2_RAM_PLAYER_PTR);
		modLog("fusion over (%s): %s back from %s; form %d -> %d; player object %08X drawn with %08X; table[%d] -> %08X", live ? "live" : "loaded",
		       lg2Name(host), kinds[kind].name, original.sprite, formNow, pl, lg2IsPlayer(pl) ? modRead32(pl + LG2_ACTOR_SPRITE_DEF) : 0, hostDefNumber, hostDef);
	}
	*c = original;
	c->xp += earned;
	if (!live) {
		// A save or save state made while fused has been loaded: the record is
		// put back above, and anyone in it still drawn as a fusion goes back to
		// the character's own sprite
		const uint32_t* table = (const uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
		uint32_t own = table[lg2FormDef(c->sprite)];
		lg2MoveActors(0x08000000 + gogetaRomOffset, own);
		lg2MoveActors(0x08000000 + vegitoRomOffset, own);
		lg2MoveActors(0x08000000 + gotenksRomOffset, own);
		lg2MoveActors(0x08000000 + gotenksSsjRomOffset, own);
	}
	if (live) {
		uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
		uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
		// The form can have changed while fused (a Super Saiyan who fused drops
		// back to normal): the character comes back in the form the game now
		// has him in, not the one he fused in, or the two would disagree and he
		// would be left frozen.
		c->sprite = formNow;
		table[hostDefNumber] = hostDef;
		lg2MoveActors(fusedDef(), hostDef);
		// And whatever sprite the player object is on, it ends up on his own,
		// in an animation his own sprite has. When the fusion ends because the
		// player switched to someone else, the player object is that someone's
		// already: it is left alone unless it is still drawn as the fusion.
		if (lg2IsPlayer(player)) {
			int now = lg2CurrentChar();
			if (now == host) {
				repoint(player, hostDef);
			} else if (now >= 0 && now < LG2_CHARACTERS && isFusedDef(modRead32(player + LG2_ACTOR_SPRITE_DEF))) {
				repoint(player, table[lg2FormDef(lg2Char(now)->sprite)]);
			}
		}
		cooldown = COOLDOWN_SECONDS * FPS;
		say("FUSION ENDED");
	}
	fused = 0;
	if (host == lg2CurrentChar()) {
		lg2SyncPlayerStats();
	}
	modSetSetting("fusion_host", -1);
	host = lg2FusionHost = -1;
	kind = lg2FusionKind = 0;
}

// The wait between fusions (one wait, shared by Gogeta and Vegito) is over at
// once on a Capsule Corp pad: regen.c calls this
int lg2FusionWaiting(void) {
	return cooldown > 0;
}

void lg2FusionReady(void) {
	cooldown = 0;
}

// ---- choosing it with L, and the dance ----------------------------------------------------------
enum { BTN_B = 2, BTN_L = 512 };
enum { LAST_ATTACK_SLOT = 3 };              // the energy blast: where L's round ends
// The third special slot (a technique learned in the shop). The game has no
// such slot, so while it is chosen the technique sits in one of the record's
// own special slots (the last one the story has given him: he owns it, so no
// flag need change), which the game then draws and fires as usual; leaving
// the stop puts the own special back. L reaches it right after the own
// specials, before the transformation. A character with no special of his own
// yet gets it after the energy blast, in the second slot, lent for the while.
//
// Save states are written with the own special back in place (and the stop
// taken up again the frame after), so nothing of this ever sits in a state or
// the resume; a state loaded from before this was so is tidied on load.
static int thirdOn, thirdWho, thirdSlot;
static int chosen;        // 0, GOGETA or VEGITO: which fusion L has stopped on
static uint8_t thirdWas;
static uint32_t thirdFlagsWas;
static int thirdResume; // 1 + who: the stop was left for a menu, a room change or a state; back to it after

// The slot L's round visits just before the third stop
static int thirdAfter(const struct LG2Char* c) {
	int k;
	for (k = 1; k >= 0; --k) {
		if (c->flags & LG2_ATTACK_OWNED(k)) {
			return k;
		}
	}
	return LAST_ATTACK_SLOT;
}

// The record slot the technique goes in: the last own special he has, or the
// second slot (lent) when he has none
static int thirdSlotFor(const struct LG2Char* c) {
	int after = thirdAfter(c);
	return after < 2 ? after : 1;
}

static void leaveThird(int resume) {
	if (thirdOn) {
		struct LG2Char* c = lg2Char(thirdWho);
		c->attack[thirdSlot] = thirdWas;
		// (the transformation slot is never the stop's: it stays as it is now,
		// so a stop taken up during a fusion, which has no transformation,
		// does not take the slot away when it is left)
		c->flags = (thirdFlagsWas & ~LG2_ATTACK_OWNED(2)) | (c->flags & LG2_ATTACK_OWNED(2));
		thirdOn = 0;
		thirdResume = resume ? 1 + thirdWho : 0;
		if (!resume && c->selectedAttack == thirdSlot && !(c->flags & LG2_ATTACK_OWNED(thirdSlot))) {
			c->selectedAttack = (uint8_t) thirdAfter(c); // never left on a slot he does not have
		}
		if (!resume) {
			modLog("third slot left: %s back to attack %d in slot %d", lg2Name(thirdWho), thirdWas, thirdSlot);
		}
	}
}

static void enterThird(int who) {
	struct LG2Char* c = lg2Char(who);
	int fresh = !thirdResume;
	thirdWho = who;
	thirdSlot = thirdSlotFor(c);
	thirdWas = c->attack[thirdSlot];
	thirdFlagsWas = c->flags;
	c->attack[thirdSlot] = (uint8_t) lg2ThirdSpecial(who);
	c->flags |= LG2_ATTACK_OWNED(thirdSlot);
	c->selectedAttack = (uint8_t) thirdSlot;
	thirdOn = 1;
	thirdResume = 0;
	if (fresh) {
		modLog("third slot: %s on attack %d in slot %d (own attack %d put aside)", lg2Name(who), c->attack[thirdSlot], thirdSlot, thirdWas);
	}
}

// A save state is about to be written: his own special goes back for it
static void thirdStateWriting(void) {
	leaveThird(1);
}

// A save state was loaded: whatever this knew is stale. A state from before
// states were written clean can hold a technique in an own slot: put the own
// special back (the slot it was lent to is the second one)
static void thirdStateRead(void) {
	thirdOn = 0;
	thirdResume = 0;
	chosen = 0; // nor is a fusion still being chosen
	lg2RepairRecords();
}
enum { APPEAR_FRAMES = 14, DANCE_TICKS = 4, FLASH_FRAMES = 18 };

// (scene: declared above)
static int sceneKind;
static int wantFuse;

static int canChoose(void) {
	int who = lg2CurrentChar();
	int can = who == LG2_GOKU || who == LG2_VEGETA || (who == LG2_TRUNKS && lg2TrunksLook);
	// Never while fused, or while the fusion scene is playing: a fused character's
	// L goes round his own attacks only
	return can && !fused && !lg2FusionKind && !scene && lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR)) && lg2Char(who)->hp > 0;
}

// Fusing in the middle of a transformation or while knocked down leaves the
// character in a muddle when it ends. A press of B at such a moment is kept,
// and the fusion starts as soon as he is free again (for up to three seconds).
enum { TRANSFORM_ANIMATION = 9, KNOCKED_DOWN_ANIMATION = 3, WANT_FRAMES = 180 };
static int wantFor; // frames the press has been waiting

static int standing(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	int animation = (int) (modRead32(player + LG2_ACTOR_ANIMATION) - modRead32(player + LG2_ACTOR_SPRITE_DEF) - LG2_SPRITE_DEF_ANIMATIONS) / 16;
	return animation != TRANSFORM_ANIMATION && animation != KNOCKED_DOWN_ANIMATION;
}

// L is the game's "next attack" button. After the last attack two more stops
// are slipped in, which the game never hears about; from the second, L goes
// through to the game again and it moves on to its first attack.
static uint32_t filter(uint32_t keys) {
	static uint32_t last, hiddenL;
	uint32_t pressed = keys & ~last;
	int who = lg2CurrentChar();
	last = keys;
	hiddenL &= keys;
	if (scene) {
		return 0; // nobody moves during the dance
	}
	// L's round for anyone with a third special: the game's slots, then it,
	// then (Goku, Vegeta, kid Trunks) the fusions, then the first slot again
	if (thirdOn && (fused || lg2FusionKind || who != thirdWho ||
	                lg2Char(who)->attack[thirdSlot] != lg2ThirdSpecial(who) || lg2Char(who)->selectedAttack != thirdSlot)) {
		leaveThird(0); // something else changed the record meanwhile (a switch, a choice in the shop): let it be
	} else if (thirdOn && !lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR))) {
		leaveThird(1); // a menu, or a change of room: the record must be his own meanwhile; back to the stop after
	}
	if (thirdResume && lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR))) {
		struct LG2Char* c = lg2Char(thirdResume - 1);
		int same = who == thirdResume - 1 && !fused && !lg2FusionKind && c->selectedAttack == thirdSlotFor(c) && lg2ThirdSpecial(who) >= 0;
		thirdResume = 0;
		if (same) {
			enterThird(who);
		} else if (!(c->flags & LG2_ATTACK_OWNED(c->selectedAttack & 3))) {
			c->selectedAttack = (uint8_t) thirdAfter(c);
		}
	}
	if ((pressed & BTN_L) && !chosen && !fused && !lg2FusionKind && lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR))) {
		struct LG2Char* c = lg2Char(who);
		if (!thirdOn && c->selectedAttack == thirdAfter(c) && lg2ThirdSpecial(who) >= 0) {
			enterThird(who);
			hiddenL = BTN_L; // the game never sees this press (nor the rest of its hold)
			return keys & ~hiddenL;
		}
		if (thirdOn) {
			leaveThird(0);
			c->selectedAttack = (uint8_t) thirdAfter(c); // the game takes this press on from there: to the next slot it owns
			if (c->selectedAttack != LAST_ATTACK_SLOT || !canChoose()) {
				return keys;
			}
		}
	}
	if (!canChoose()) {
		chosen = 0;
		return keys & ~hiddenL;
	}
	if (pressed & BTN_L) {
		if (!chosen && lg2Char(who)->selectedAttack == LAST_ATTACK_SLOT) {
			chosen = who == LG2_TRUNKS ? GOTENKS : GOGETA; // kid Trunks has the one fusion
			hiddenL = BTN_L;
		} else if (chosen == GOGETA) {
			chosen = VEGITO;
			hiddenL = BTN_L;
		} else {
			chosen = 0;
		}
	}
	if (chosen) {
		if (pressed & BTN_B) {
			wantFuse = chosen;
		}
		keys &= ~BTN_B; // B is "fuse" now, not the energy blast
	}
	return keys & ~hiddenL;
}

static void blendPixel(int x, int y, uint32_t color, int amount) {
	if (x < 0 || x >= 240 || y < 0 || y >= 160 || amount <= 0) {
		return;
	}
	uint32_t* p = &modScreen()[y * 240 + x];
	int r = *p & 0xFF, g = (*p >> 8) & 0xFF, b = (*p >> 16) & 0xFF;
	if (amount > 256) {
		amount = 256;
	}
	r += (((int) (color & 0xFF)) - r) * amount / 256;
	g += (((int) ((color >> 8) & 0xFF)) - g) * amount / 256;
	b += (((int) ((color >> 16) & 0xFF)) - b) * amount / 256;
	*p = r | (g << 8) | (b << 16);
}

// One dance picture with its feet at (x, y); `mirror` faces it the other way
static void drawDancer(const uint32_t* picture, int cell, int x, int y, int mirror, int amount) {
	int i, j;
	for (j = 0; j < cell; ++j) {
		for (i = 0; i < cell; ++i) {
			uint32_t color = picture[j * cell + (mirror ? cell - 1 - i : i)];
			if (color != 0xFFFFFFFF) {
				blendPixel(x - cell / 2 + i, y - (cell - 4) + j, color, amount);
			}
		}
	}
}

// What the attack box in the corner shows while a fusion is chosen: Buu's
// Fury's own fusion icon, the two figures mid-dance (dimmed while it is not
// ready), with which fusion it is written beside the box
static void drawChoice(void) {
	enum { BOX_X = 68, BOX_Y = 10, BOX_W = 18, BOX_H = 8 }; // the inside of the game's attack box
	int x, y;
	modRect(BOX_X, BOX_Y, BOX_W, BOX_H, 0x000000);
	for (y = 0; y < FUSION_ICON_H; ++y) {
		for (x = 0; x < FUSION_ICON_W; ++x) {
			uint32_t color = fusionIcon[y * FUSION_ICON_W + x];
			int sx = BOX_X + x, sy = BOX_Y - 1 + y; // the icon sits one pixel in from the capture's corner
			if (color && sx >= BOX_X && sx < BOX_X + BOX_W && sy >= BOX_Y && sy < BOX_Y + BOX_H) {
				modScreen()[sy * 240 + sx] = cooldown ? ((color >> 1) & 0x7F7F7F) : color;
			}
		}
	}
	modText(92, 12, cooldown ? 0x808080 : 0x40FFFF, cooldown ? "%s (%ds)" : "%s", kinds[chosen].name, (cooldown + FPS - 1) / FPS);
}

// The scene: the partner appears, the dance, a flash, and it is done
static void playScene(void) {
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	if (!lg2IsPlayer(player)) {
		modLog("fusion: dance dropped: no player object");
		scene = 0;
		return;
	}
	uint32_t frameRecord = modRead32(player + LG2_ACTOR_FRAME_RECORD);
	int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16, h = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 3) : 32;
	int px = (int) modRead32(player + LG2_ACTOR_SCREEN_X) + w / 2, feet = (int) modRead32(player + LG2_ACTOR_SCREEN_Y) + h;
	int isGoku = lg2CurrentChar() == LG2_GOKU;
	const uint32_t (*mine)[GOKUDANCE_CELL * GOKUDANCE_CELL] = sceneKind == GOTENKS ? kidTrunksDance : isGoku ? gokuDance : vegetaDance;
	const uint32_t (*theirs)[GOKUDANCE_CELL * GOKUDANCE_CELL] = sceneKind == GOTENKS ? gotenDance : isGoku ? vegetaDance : gokuDance;
	int danceLength = sceneKind != VEGITO ? GOKUDANCE_FRAMES * DANCE_TICKS : 20;
	int t = scene - 1, x, y;
	// a pool of light under and around the player, which also hides his own sprite
	for (y = -40; y <= 6; ++y) {
		for (x = -34; x <= 34; ++x) {
			int d = x * x * 2 + (y + 16) * (y + 16) * 3;
			if (d < 34 * 34 * 2) {
				int full = 34 * 34 * 2;
				blendPixel(px + x, feet + y, 0xC0FFFF, d < full / 3 ? 250 : 250 * (full - d) / (full * 2 / 3)); // solid over him, fading out
			}
		}
	}
	// the pictures face right: the one on the left as they are, the one on the right mirrored, so the two face each other
	if (t < APPEAR_FRAMES) { // the partner flickers into being a step away
		drawDancer(mine[0], GOKUDANCE_CELL, px - 12, feet, 0, 256);
		if (t & 1) {
			drawDancer(theirs[0], GOKUDANCE_CELL, px + 12, feet, 1, 60 + t * 14);
		}
	} else if (t < APPEAR_FRAMES + danceLength) {
		int step = sceneKind != VEGITO ? (t - APPEAR_FRAMES) / DANCE_TICKS : 0;
		drawDancer(mine[step], GOKUDANCE_CELL, px - 12, feet, 0, 256);
		drawDancer(theirs[step], GOKUDANCE_CELL, px + 12, feet, 1, 256);
	} else {
		int f = t - APPEAR_FRAMES - danceLength; // the flash: the whole screen to white and back
		int amount = f < FLASH_FRAMES / 2 ? f * 512 / FLASH_FRAMES : (FLASH_FRAMES - f) * 512 / FLASH_FRAMES;
		for (y = 0; y < 160; ++y) {
			for (x = 0; x < 240; ++x) {
				blendPixel(x, y, 0xFFFFFF, amount);
			}
		}
		if (f == FLASH_FRAMES / 2) {
			fuse(sceneKind);
		}
		if (f >= FLASH_FRAMES) {
			scene = 0;
			return;
		}
	}
	++scene;
}

// A hit that would knock Gogeta out ends the fusion instead, and the hit is
// spent doing so: the character underneath is not harmed by it.
static void onHit(int32_t* damage) {
	if (fused && *damage >= lg2Char(host)->hp) {
		int was = kind;
		*damage = 0;
		defuse(1);
		say(was == VEGITO ? "VEGITO WAS BEATEN" : was == GOTENKS ? "GOTENKS WAS BEATEN" : "GOGETA WAS BEATEN");
	}
}

// The game is about to write its save: it must not store Gogeta as the host,
// nor a borrowed technique in a slot
static int onSave(uint32_t* r) {
	(void) r;
	if (fused) {
		defuse(1);
	}
	leaveThird(1);
	return MOD_CONTINUE;
}

static void frame(void) {
	int playing = (modRead32(LG2_RAM_PLAYER_PTR) >> 24) == 2;
	if (cooldown) {
		--cooldown;
	}
	if (!fused && host >= 0 && host < LG2_CHARACTERS && playing) {
		// Loaded something that was saved mid-fusion: undo it
		struct LG2Char* c = lg2Char(host);
		static int traced;
		if (!traced++) {
			modLog("fusion check on load: host %d kind %d; stats %d %d %d vs marker %d %d %d; attacks %d %d %d %d vs %d %d %d %d; xp %u vs %u", host, kind,
			       c->stat[0], c->stat[1], c->stat[2], marker.stat[0], marker.stat[1], marker.stat[2], c->attack[0], c->attack[1], c->attack[2], c->attack[3],
			       marker.attack[0], marker.attack[1], marker.attack[2], marker.attack[3], c->xp, marker.xp);
		}
		// (the stats and attacks are what mark it: the game can change the
		// health and energy maxima meanwhile)
		if (!memcmp(c->stat, marker.stat, sizeof(c->stat)) && !memcmp(c->attack, marker.attack, 4)) {
			defuse(0);
		}
	}
	if (!fused && playing) {
		keepOwnSprite();
	}
	if (fused && lg2Char(host)->xp < marker.xp) {
		// The character's record is not the fused one any more: a save state from
		// before the fusion was loaded, or the game reloaded its save (after a
		// defeat, say). The fusion is simply over; the record is left as it is.
		// (A fused character's experience only ever goes up from what it was
		// given, which is how this is told.) Carrying on regardless left
		// "Gogeta" on the clock over a plain Vegeta with plain Vegeta's strength.
		uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
		modLog("fusion dropped: %s's record was replaced under %s (experience %u < %u)", lg2Name(host), kinds[kind].name, lg2Char(host)->xp, marker.xp);
		table[hostDefNumber] = hostDef;
		lg2MoveActors(fusedDef(), hostDef);
		setName(host, 0);
		fused = 0;
		modSetSetting("fusion_host", -1);
		host = lg2FusionHost = -1;
		kind = lg2FusionKind = 0;
	}
	if (fused) {
		struct LG2Char* c = lg2Char(host);
		if (lg2FormDef(c->sprite) != hostDefNumber) {
			// The host changed form underneath (a transformation that was already
			// under way finished): the fused sprite goes on the new form instead
			uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
			table[hostDefNumber] = hostDef;
			hostDefNumber = lg2FormDef(c->sprite);
			hostDef = table[hostDefNumber];
			table[hostDefNumber] = fusedDef();
			lg2MoveActors(hostDef, fusedDef());
		}
		if (lg2CurrentChar() != host || c->hp == 0) {
			defuse(1); // switched to someone else
		} else if (playing && !modPaused() && --fused == 0) {
			defuse(1);
		}
	}
	if (modPaused()) {
		return;
	}
	if (wantFuse) {
		if (cooldown) {
			say("FUSION NOT READY YET");
			wantFuse = 0;
		} else if (!canChoose() || scene || ++wantFor > WANT_FRAMES) {
			wantFuse = wantFor = 0;
		} else if (standing()) {
			modLog("fusion: dance begins (%s)", kinds[wantFuse].name);
			scene = 1;
			sceneKind = wantFuse;
			chosen = 0;
			wantFuse = wantFor = 0;
		}
	}
	if (scene) {
		playScene();
	} else if (chosen && canChoose()) {
		drawChoice();
	} else if (thirdOn && playing && !modPaused() && modScreen()[12 * 240 + 70] != 0) { // the attack box is up (not a fade)
		modText(92, 12, 0x40FFFF, "SLOT 3"); // beside the box, where a fusion's name goes
	}
	if (fused) {
		int seconds = (fused + FPS - 1) / FPS;
		modText(96, 2, seconds <= 10 ? 0x4040FF : 0x40FFFF, "%s %d:%02d", kinds[kind].name, seconds / 60, seconds % 60);
	}
	if (modFrame() < messageUntil) {
		modTextScale(2);
		modText(8, 30, 0x40FFFF, "%s", message);
		modTextScale(1);
	}
}

MOD_INIT(fusion) {
	{ // Hercule's name line has only its English entry; give it all five, as the others have
		uint32_t* line = (uint32_t*) (modRom(LG2_ROM_TEXT) + 53 * 20);
		int k;
		for (k = 1; k < 5; ++k) {
			if (!line[k]) {
				line[k] = line[0];
			}
		}
	}
	memcpy(modRom(gogetaRomOffset), gogetaData, gogetaSize);
	memcpy(modRom(vegitoRomOffset), vegitoData, vegitoSize);
	memcpy(modRom(gotenksRomOffset), gotenksData, gotenksSize);
	memcpy(modRom(gotenksSsjRomOffset), gotenksSsjData, gotenksSsjSize);
	recall();
	modHook(LG2_FN_SAVE_CHARDATA, onSave);
	lg2AddPlayerHitFilter(onHit);
	modFilterKeys(filter);
	modOnStateWrite(thirdStateWriting);
	modOnStateRead(thirdStateRead);
	modOnFrame(frame);
}
