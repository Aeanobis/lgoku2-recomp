// Gohan has Kamehameha from the start, and the Masenko too (which works the
// Buu's Fury way: see masenko.c).
//
// Kamehameha plays animation 18. Not every outfit Gohan wears has one: the
// outfit he starts the game in stops at animation 17, and the next one has the
// slot but it is empty. Firing Kamehameha in those reads a missing animation and
// crashes, which is why the original game only teaches it later. So this mod
// also fills the gap in every Gohan sprite, reusing that sprite's own "fire"
// pose (animation 5, the one energy blast and Masenko use).
//
// One more thing stood in the way. The beam's damage is small and applied many
// times a second, and the game rounds each application down. Until a character's
// Power is around 10 that rounds to zero every time, so an early Kamehameha did
// no damage at all. Here the fractions are kept and added up instead, and the
// result is multiplied by KAMEHAMEHA_BOOST.
//
// The beam is shown as an "Electric Kamehameha" in the spirit of Buu's Fury:
// the game's own blue beam sprites are redrawn in gold with sparks around them.
// (This is drawn over the finished frame, so it also covers anything that
// would have been in front of the beam.)
#include "lg2.h"

#include <stdio.h>
#include <string.h>

#define FIRE_ANIMATION 5
#define KAMEHAMEHA_ANIMATION 18
#define LAST_ANIMATION 20
#define NEW_DEFS_OFFSET 0x7F8000 // unused space near the end of the ROM
#define NEW_DEF_SPACE 0x200

// An animation row is four pointers, one per facing, each to a run of 12-byte
// frame entries with no count or end mark: an attack steps through as many
// entries as its own animation has (the Spirit Bomb's has seven), so a stand-in
// animation must be at least that long or the last facing runs off the row
// into unrelated data and draws from a garbage pointer (which once trampled
// the heap). Stand-ins are therefore built as fresh records of RECORD_ENTRIES
// entries each, the fire pose's frames with its last one repeated, in free
// space in the enlarged ROM.
#define RECORDS_ROM 0xBC0000
#define RECORD_ENTRIES 8
#define RECORD_ENTRY 12
static uint32_t recordsUsed;
static int recordsBad;

// Fill `row` with an animation standing in for a missing one, built from the
// fire pose's row `fire` (both 16-byte rows of a definition)
static void standIn(uint8_t* row, const uint8_t* fire) {
	const uint32_t* ptr = (const uint32_t*) fire;
	uint32_t out[4];
	int f, e, n[4];
	if (!recordsBad && !recordsUsed) {
		unsigned i;
		const uint8_t* at = modRom(RECORDS_ROM);
		for (i = 0; i < 64; ++i) {
			recordsBad |= at[i] != 0xFF && at[i] != 0x00;
		}
		if (recordsBad) {
			fprintf(stderr, "gohan_attacks: no free ROM space at %X for animation records\n", RECORDS_ROM);
		}
	}
	for (f = 0; f < 4; ++f) { // how many entries each facing's record has: up to where the next one starts
		n[f] = f < 3 ? (int) ((ptr[f + 1] - ptr[f]) / RECORD_ENTRY) : n[2];
		if ((ptr[f] >> 24) != 8 || n[f] < 1 || n[f] > RECORD_ENTRIES) {
			recordsBad = 1;
		}
	}
	if (recordsBad || recordsUsed + 4 * RECORD_ENTRIES * RECORD_ENTRY > 0x40000) {
		memcpy(row, fire, 16); // as it was: the fire pose itself
		return;
	}
	for (f = 0; f < 4; ++f) {
		uint8_t* record = modRom(RECORDS_ROM + recordsUsed);
		const uint8_t* from = modRom(ptr[f] - 0x08000000);
		for (e = 0; e < RECORD_ENTRIES; ++e) {
			memcpy(record + e * RECORD_ENTRY, from + (e < n[f] ? e : n[f] - 1) * RECORD_ENTRY, RECORD_ENTRY);
		}
		out[f] = 0x08000000 + RECORDS_ROM + recordsUsed;
		recordsUsed += RECORD_ENTRIES * RECORD_ENTRY;
	}
	memcpy(row, out, 16);
}
#define MASENKO_SLOT 0
#define KAMEHAMEHA_SLOT 1
#define BLAST_SLOT 3
#define KAMEHAMEHA_BOOST 3.0   // damage multiplier for Gohan's Kamehameha
#define ELECTRIC 1             // 0 = leave the beam blue
#define BEAM_SCALE 2           // how many times wider the electric beam is drawn (level 50 and up)
#define THICK_PERCENT 150      // the thicker blue one of levels 25 to 49
#define THICK_LEVEL 25
#define ELECTRIC_LEVEL 50
#define LOOK_FRAMES 90         // how long after the firing pose the look is kept up (the balls fly on)

// Sprite definition numbers of every playable form (see LG2_ROM_FORMS):
// Gohan's six first, then Piccolo, Vegeta, Trunks, Goku. Any of them can be
// handed another character's attack (the shop teaches them), so each needs
// every attack animation: 16 (Masenko), 18 (beams), 19 (Spirit Bomb). Where
// one is missing it is filled with the sprite's own fire pose.
static const uint8_t gohanDefs[] = { 0x35, 0x37, 0x38, 0x39, 0x3A, 0x3B, 103, 104, 137, 138, 139, 140, 128, 129, 130, 132, 133, 134, 60, 61 };
#define GOHAN_DEFS (sizeof(gohanDefs) / sizeof(gohanDefs[0]))
#define GOHAN_OWN 6
static const uint8_t attackAnimations[] = { 16, 18, 19 };

// Definitions that had to be replaced by a longer copy: game addresses
static uint32_t movedFrom[GOHAN_DEFS], movedTo[GOHAN_DEFS];
static unsigned nMoved;
static int ready;

static int emptyRow(const uint8_t* row) {
	static const uint8_t zero[16];
	return !memcmp(row, zero, 16);
}

static void fixSprites(void) {
	uint32_t* table = (uint32_t*) modRom(LG2_ROM_SPRITE_DEFS);
	unsigned k;
	int i;
	ready = 1;
	for (k = 0; k < GOHAN_DEFS; ++k) {
		uint8_t* def = modRom(table[gohanDefs[k]] - 0x08000000);
		uint8_t* rows = def + LG2_SPRITE_DEF_ANIMATIONS;
		int animations = def[2];
		unsigned a;
		if (animations <= FIRE_ANIMATION || emptyRow(rows + FIRE_ANIMATION * 16)) {
			if (k < GOHAN_OWN) {
				ready = 0; // not laid out as expected: leave it, and do not hand out the attack
			}
			continue;
		}
		if (animations > LAST_ANIMATION - 1) {
			for (a = 0; a < sizeof(attackAnimations); ++a) { // the slots are there; fill any that are empty
				if (attackAnimations[a] < animations && emptyRow(rows + attackAnimations[a] * 16)) {
					standIn(rows + attackAnimations[a] * 16, rows + FIRE_ANIMATION * 16);
				}
			}
			continue;
		}
		// Too short: build a longer copy in unused ROM space and use that instead
		uint8_t* copy = modRom(NEW_DEFS_OFFSET + nMoved * NEW_DEF_SPACE);
		int size = LG2_SPRITE_DEF_ANIMATIONS + (LAST_ANIMATION + 1) * 16;
		int used = 0;
		for (i = 0; i < NEW_DEF_SPACE; ++i) {
			used |= copy[i] != 0xFF && copy[i] != 0x00;
		}
		if (used || size > NEW_DEF_SPACE) {
			fprintf(stderr, "gohan_attacks: no free ROM space at %X\n", NEW_DEFS_OFFSET + nMoved * NEW_DEF_SPACE);
			ready = 0;
			continue;
		}
		memcpy(copy, def, LG2_SPRITE_DEF_ANIMATIONS + animations * 16);
		for (i = animations; i <= LAST_ANIMATION; ++i) {
			standIn(copy + LG2_SPRITE_DEF_ANIMATIONS + i * 16, rows + FIRE_ANIMATION * 16);
		}
		for (a = 0; a < sizeof(attackAnimations); ++a) { // and the ones that were there but empty
			if (attackAnimations[a] < animations && emptyRow(copy + LG2_SPRITE_DEF_ANIMATIONS + attackAnimations[a] * 16)) {
				standIn(copy + LG2_SPRITE_DEF_ANIMATIONS + attackAnimations[a] * 16, rows + FIRE_ANIMATION * 16);
			}
		}
		copy[2] = LAST_ANIMATION + 1;
		movedFrom[nMoved] = table[gohanDefs[k]];
		movedTo[nMoved] = 0x08000000 + NEW_DEFS_OFFSET + nMoved * NEW_DEF_SPACE;
		table[gohanDefs[k]] = movedTo[nMoved];
		++nMoved;
	}
}

// The game's damage formula for an energy attack, without the rounding down
static double exactDamage(int power, int targetEndurance, int basePower, int multiplier) {
	double f = (26.0 * power * power - 701.0 * power + 115618.0) * power / 256.0;
	double defended = (128 - targetEndurance) * basePower / 128.0;
	return defended * f / 262144.0 * (multiplier / 128.0) * 1.05; // 1.05: middle of the random spread
}

static int onHit(uint32_t* r) {
	static double owed;
	uint32_t hit = r[1], player = modRead32(LG2_RAM_PLAYER_PTR);
	if (!player || r[0] == player || modRead32(hit - 4) != player || modRead32(hit - 8) != LG2_ROM_KAMEHAMEHA_HIT) {
		return MOD_CONTINUE;
	}
	int32_t gameFigure = (int32_t) modRead32(hit + 8);
	if (gameFigure > 0) { // strong enough for the game's own rounding: just boost it
		modWrite32(hit + 4, (uint32_t) (gameFigure * KAMEHAMEHA_BOOST));
		modWrite32(hit + 8, (uint32_t) (gameFigure * KAMEHAMEHA_BOOST));
		return MOD_CONTINUE;
	}
	owed += KAMEHAMEHA_BOOST * exactDamage(modRead8(player + LG2_PLAYER_STATS + LG2_POW), modRead8(r[0] + LG2_PLAYER_STATS + LG2_END),
	                    (int32_t) modRead32(LG2_ROM_KAMEHAMEHA_HIT + 4), modRead8(player + LG2_PLAYER_STATS + 4));
	if (owed >= 1.0) {
		int whole = (int) owed;
		owed -= whole;
		modWrite32(hit + 4, whole);
		modWrite32(hit + 8, whole);
	}
	return MOD_CONTINUE;
}

// ---- electric look ----------------------------------------------------------------
static const uint8_t objSizes[4][4][2] = {
	{ { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
	{ { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
	{ { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
	{ { 8, 8 }, { 8, 8 }, { 8, 8 }, { 8, 8 } },
};

// Blue beam colour -> gold; white stays white
static uint32_t electricColor(int r, int g, int b) {
	if (r > 200 && g > 200 && b > 200) {
		return 0xC8FFFF;
	}
	int nr = b + 30 > 255 ? 255 : b + 30;
	int ng = (2 * g + b) / 3 + 20 > 255 ? 255 : (2 * g + b) / 3 + 20;
	int nb = r / 3;
	return nr | (ng << 8) | (nb << 16);
}

static void spark(int x, int y) {
	uint32_t* screen = modScreen();
	int i, steps = 3 + modRandom(4);
	for (i = 0; i < steps; ++i) {
		if (x >= 0 && x < 240 && y >= 0 && y < 160) {
			screen[y * 240 + x] = i == 0 ? 0xFFFFFF : 0x60FFFF;
		}
		x += (int) modRandom(3) - 1;
		y += (int) modRandom(3) - 1;
	}
}

// Whether a pixel belongs to the beam being drawn: the blue ones (Kamehameha,
// Big Bang, the Spirit Bomb: light blue and white) or the Special Beam
// Cannon's (its yellow and orange spiral, pink and white)
enum { BLUE_BEAM, SPIRAL_BEAM };
static int beamPixel(int kind, int r, int g, int b) {
	if (r > 200 && g > 200 && b > 200) {
		return 1;
	}
	if (kind == SPIRAL_BEAM) {
		return (r > 150 && g > 120 && b < 120) || (r > 150 && b > 100 && g < 130);
	}
	return b >= 160 && b >= r + 30;
}

// Redraws every sprite that is made of the beam's colours: enlarged about
// its centre by percent, and when gold, recoloured gold with sparks (the
// "Electric Kamehameha").
static void drawBeamLook(int playerX, int playerY, int kind, int gold, int percent) {
	uint32_t* screen = modScreen();
	int oneD = modRead16(0x04000000) & 0x40; // sprite tile mapping mode
	int i;
	for (i = 0; i < 128; ++i) {
		uint16_t a0 = modRead16(0x07000000 + i * 8), a1 = modRead16(0x07000002 + i * 8), a2 = modRead16(0x07000004 + i * 8);
		if ((a0 & 0x300) == 0x200 || (a0 & 0x100)) {
			continue; // hidden, or rotated/scaled
		}
		int w = objSizes[a0 >> 14][a1 >> 14][0], h = objSizes[a0 >> 14][a1 >> 14][1];
		int y = a0 & 0xFF, x = a1 & 0x1FF;
		if (y >= 160) y -= 256;
		if (x >= 240) x -= 512;
		if (y + h <= 26 && (x + w <= 92 || x >= 156)) {
			continue; // the HUD
		}
		int bpp8 = (a0 >> 13) & 1, tile = a2 & 0x3FF, pal = a2 >> 12, step = bpp8 ? 2 : 1;
		static uint32_t px[64 * 64];
		int tx, ty, solid = 0, beam = 0;
		for (ty = 0; ty < h; ++ty) {
			for (tx = 0; tx < w; ++tx) {
				int sx = (a1 & 0x1000) ? w - 1 - tx : tx, sy = (a1 & 0x2000) ? h - 1 - ty : ty;
				int t = oneD ? tile + ((sy / 8) * (w / 8) + sx / 8) * step : tile + (sy / 8) * 32 + (sx / 8) * step;
				int c;
				if (bpp8) {
					c = modRead8(0x06010000 + t * 32 + (sy & 7) * 8 + (sx & 7));
				} else {
					c = modRead8(0x06010000 + t * 32 + (sy & 7) * 4 + (sx & 7) / 2);
					c = (sx & 1) ? c >> 4 : c & 15;
				}
				px[ty * w + tx] = 0xFF000000;
				if (!c) {
					continue;
				}
				uint16_t rgb = modRead16(0x05000200 + ((bpp8 ? 0 : pal * 16) + c) * 2);
				int r = (rgb & 31) << 3, g = ((rgb >> 5) & 31) << 3, b = ((rgb >> 10) & 31) << 3;
				++solid;
				if (beamPixel(kind, r, g, b)) {
					++beam;
				}
				px[ty * w + tx] = gold ? electricColor(r, g, b) : (uint32_t) (r | (g << 8) | (b << 16));
			}
		}
		if (solid < 12 || beam * 10 < solid * 7) {
			continue; // not a beam sprite
		}
		if (!gold) { // thicker only: the game's own stays as it is underneath
			goto enlarge;
		}
		// First at its real size, which recolours the game's own beam where
		// it stands. Then enlarged about its centre, except over Gohan
		// himself so that the wide beam does not hide him. The enlarged
		// pieces overlap their neighbours, which keeps it one continuous beam.
		for (ty = 0; ty < h; ++ty) {
			for (tx = 0; tx < w; ++tx) {
				uint32_t c = px[ty * w + tx];
				if (!(c >> 24) && x + tx >= 0 && x + tx < 240 && y + ty >= 0 && y + ty < 160) {
					screen[(y + ty) * 240 + x + tx] = c;
				}
			}
		}
	enlarge:;
		int bw = w * percent / 100, bh = h * percent / 100;
		int bx = x + w / 2 - bw / 2, by = y + h / 2 - bh / 2;
		for (ty = 0; ty < bh; ++ty) {
			for (tx = 0; tx < bw; ++tx) {
				uint32_t c = px[(ty * 100 / percent) * w + tx * 100 / percent];
				int sx = bx + tx, sy = by + ty;
				if ((c >> 24) || sx < 0 || sx >= 240 || sy < 0 || sy >= 160) {
					continue;
				}
				if (sx >= playerX && sx < playerX + 16 && sy >= playerY && sy < playerY + 32) {
					continue;
				}
				screen[sy * 240 + sx] = c;
			}
		}
		int sparks = gold ? 1 + bw * bh / 128 : 0, k;
		for (k = 0; k < sparks; ++k) {
			spark(bx - 2 + (int) modRandom(bw + 4), by - 2 + (int) modRandom(bh + 4));
		}
	}
}

// The look of the beams and balls grows with the level of whoever fires them:
// the game's own up to level 24, half as thick again and still blue from 25,
// and the gold electric one from 50
static int lookTier(void) {
	int who = lg2CurrentChar(), level;
	if (who < 0 || who >= LG2_CHARACTERS) {
		return 0;
	}
	level = lg2Level(lg2Char(who));
	return level >= ELECTRIC_LEVEL ? 2 : level >= THICK_LEVEL ? 1 : 0;
}

// The attacks whose look grows: Big Bang, Kamehameha, the Special Beam
// Cannon (thicker only, never gold) and the Spirit Bomb
static int growingAttack(int attack) {
	return attack == 0 || attack == LG2_ATTACK_KAMEHAMEHA || attack == 9 || attack == 10;
}

// For combat.c: the hit's attack grows with level, and the tier the current
// character's level gives it (the damage grows with the look)
int lg2GrowingAttackTier(uint32_t descriptor) {
	if (descriptor != LG2_ROM_KAMEHAMEHA_HIT && descriptor != LG2_ROM_BIG_BANG_HIT && descriptor != LG2_ROM_SPECIAL_BEAM_HIT &&
	    descriptor != LG2_ROM_SPIRIT_BOMB_HIT) {
		return 0;
	}
	return lookTier();
}

static void frame(void) {
	static unsigned lookUntil;
	static int lookTierShown, lookKind;
	struct LG2Char* gohan = lg2Char(LG2_GOHAN);
	if (!gohan) {
		return;
	}
	if (gohan->attack[KAMEHAMEHA_SLOT] == LG2_ATTACK_KAMEHAMEHA && ready) {
		gohan->flags |= LG2_ATTACK_OWNED(KAMEHAMEHA_SLOT);
	}
	if (gohan->attack[MASENKO_SLOT] == LG2_ATTACK_MASENKO) {
		gohan->flags |= LG2_ATTACK_OWNED(MASENKO_SLOT);
	}
	// A player object made before this mod was in place (a save state, say)
	// may still point at a short sprite definition: move it to the long one
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	if ((player >> 24) == 2) {
		uint32_t def = modRead32(player + LG2_ACTOR_SPRITE_DEF);
		unsigned k;
		// While the character is in a firing pose (the fire pose, 5, or a
		// special's own from 16 up: Kamehameha is 18) with one of the growing
		// attacks chosen, and for a while after, so the balls that fly on are
		// covered too
		int animation = (int) (modRead32(player + LG2_ACTOR_ANIMATION) - def - LG2_SPRITE_DEF_ANIMATIONS) / 16;
		int who = lg2CurrentChar();
		int attack = who >= 0 && who < LG2_CHARACTERS ? lg2Char(who)->attack[lg2Char(who)->selectedAttack & 3] : -1;
		if (ELECTRIC && (animation == FIRE_ANIMATION || (animation >= 16 && animation <= LAST_ANIMATION)) && growingAttack(attack) && !lg2FusionKind) {
			int tier = lookTier();
			if (tier) {
				lookUntil = modFrame() + (animation == KAMEHAMEHA_ANIMATION ? 24 : LOOK_FRAMES);
				lookTierShown = tier;
				lookKind = attack == 9 ? SPIRAL_BEAM : BLUE_BEAM;
			}
		}
		if (modFrame() < lookUntil && !modPaused()) {
			int gold = lookTierShown == 2 && lookKind == BLUE_BEAM;
			drawBeamLook((int) modRead32(player + LG2_ACTOR_SCREEN_X), (int) modRead32(player + LG2_ACTOR_SCREEN_Y), lookKind, gold,
			             lookTierShown == 2 ? BEAM_SCALE * 100 : THICK_PERCENT);
		}
		for (k = 0; k < nMoved; ++k) {
			if (def == movedFrom[k]) {
				modWrite32(player + LG2_ACTOR_SPRITE_DEF, movedTo[k]);
				modWrite32(player + LG2_ACTOR_ANIMATION, modRead32(player + LG2_ACTOR_ANIMATION) - movedFrom[k] + movedTo[k]);
			}
		}
	}
}

MOD_INIT(gohan_attacks) {
	fixSprites();
	modHook(LG2_FN_ACTOR_TAKE_HIT, onHit);
	modOnFrame(frame);
}
