// The fusions' own attacks, copied from Buu's Fury. While fused, the two
// special attack slots are these instead of Goku's:
//
//   Gogeta   Big Bang Kamehameha (a ball, a ring of light, and a beam across
//            the screen) and a golden dragon that stretches out ahead of him
//   Vegito   a streak of energy, and the Spirit Sword: he spins on the spot
//            with the blade out, cutting all round him
//
// Those four are held: B keeps them going (the beam, the dragon's body, the
// blade stay out and keep hurting whatever they touch, every half second),
// burning energy the while (about 30 a second); letting go, or running out of
// energy, plays the move's ending. The two Gotenks moves are one shot each.
//   Gotenks  a Super Ghost that floats off ahead of him and bursts on the
//            first enemy it meets, and a thin yellow ray
//
// Choose one with L as usual and fire with B. What you see is Buu's Fury's own
// effect, frame for frame: each attack was captured out of that game
// (tools/sprites/capture_fusion_moves.sh) and is drawn over the picture here,
// turned to face the way the character is facing. Whatever the effect is
// touching on a given frame is what it hits, each enemy once.
//
// As with the Masenko (masenko.c), the game's own attack in the slot never
// fires: this mod keeps B from the game while one of these is selected.
#include "lg2.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "generated/fusion_moves.inc"
#include "generated/hold_moves.inc"

enum { BTN_B = 2 };
enum { DOWN, UP, LEFT, RIGHT }; // LG2 facing directions (actor +0x10)
enum { GOGETA = 1, VEGITO = 2, GOTENKS = 3 };
enum { PLAYBACK, GHOST, HOLD };
// A held move hits the same enemy again every half second, and burns energy
// fast enough to show: 1 every 2 frames, 30 a second (the game gives some
// back on its own meanwhile, so the bar drops a little slower than that)
enum { HIT_EVERY = 30, HOLD_ENERGY_EVERY = 2 };
enum { SPIN_ANIMATION = 19, SPIN_FRAMES = 10, SPIN_TICK = 3 }; // Vegito's spin, from Buu's Fury (animation 19 of his sprite)
enum { FIRE_POSE = 5, STILL_FRAMES = 30 };

// travelFrom / speed: the capture ends when the shot leaves Buu's Fury's
// screen, so a shot that flies on is kept going here: the picture captured at
// `travelFrom` carries on at `speed` pixels a frame until it is off the screen
static const struct Move { const char* name; uint8_t art, hitsFrom, cost; int power; uint8_t how, travelFrom, speed; } moves[4][2] = {
	{ { 0 } },
	{ { "BIG BANG KAMEHAMEHA", HM_BIGBANG, 6, 20, 12000, HOLD, 0, 0 }, { "DRAGON RUSH", HM_DRAGON, 18, 20, 14000, HOLD, 0, 0 } },
	{ { "ENERGY STREAK", HM_LINES, 26, 10, 6000, HOLD, 0, 0 }, { "SPIRIT SWORD", HM_SWORD, 20, 10, 9000, HOLD, 0, 0 } },
	{ { "SUPER GHOST", FM_GHOST, 0, 25, 15000, GHOST, 0, 0 }, { "ENERGY RAY", FM_BALL, 20, 12, 8000, PLAYBACK, 0, 0 } },
};

static const struct Move* current; // the move being played, or 0
static int timer, facing, originX, originY; // origin: his hand, in world pixels, when it began
static uint32_t struck[16];
static int nStruck;
static unsigned struckAt[16]; // when each was last hit (held moves hit again after HIT_EVERY)
static int releasing;         // a held move: B has been let go

// The move in the selected slot, if a fusion is on and one of its two is selected
static const struct Move* selected(void) {
	const struct LG2Char* c;
	if (!lg2FusionKind || lg2FusionHost != lg2CurrentChar() || !lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR))) {
		return 0;
	}
	c = lg2Char(lg2FusionHost);
	return c->selectedAttack < 2 ? &moves[lg2FusionKind][c->selectedAttack] : 0;
}

static uint32_t filter(uint32_t keys) {
	if (current && (current->how == HOLD ? !releasing : timer < STILL_FRAMES)) {
		return 0; // he holds still while it goes off (a held move: until he lets go; its ending plays on by itself)
	}
	return selected() ? keys & ~BTN_B : keys;
}

// A point of the effect, given relative to his hand as he faces right, turned to the way he faces
static void turn(int x, int y, int* outX, int* outY) {
	switch (facing) {
	case LEFT: *outX = -x; *outY = y; break;
	case DOWN: *outX = -y; *outY = x; break;
	case UP: *outX = y; *outY = -x; break;
	default: *outX = x; *outY = y; break;
	}
}

static void pose(uint32_t player, int animation, int frameNumber) {
	uint32_t def = modRead32(player + LG2_ACTOR_SPRITE_DEF);
	modWrite16(player + 0x12, 0);
	modWrite16(player + LG2_ACTOR_FRAME, (uint16_t) frameNumber);
	modWrite8(player + 8, 0x28);
	modWrite32(player + LG2_ACTOR_ANIMATION, def + LG2_SPRITE_DEF_ANIMATIONS + 16 * animation);
}

// The attack box in the corner: Buu's Fury's icon for the move, and its name
static void drawChoice(const struct Move* move, int ready) {
	enum { BOX_X = 68, BOX_Y = 10, BOX_W = 18, BOX_H = 8 };
	const uint32_t* icon = move->how == HOLD ? hmIcons[move->art] : fmIcons[move->art];
	int x, y;
	modRect(BOX_X, BOX_Y, BOX_W, BOX_H, 0x000000);
	for (y = 0; y < FM_ICON_H; ++y) {
		for (x = 0; x < FM_ICON_W; ++x) {
			uint32_t color = icon[y * FM_ICON_W + x];
			int sx = BOX_X - 1 + x, sy = BOX_Y - 1 + y;
			if (color && sx >= BOX_X && sx < BOX_X + BOX_W && sy >= BOX_Y && sy < BOX_Y + BOX_H) {
				modScreen()[sy * 240 + sx] = ready ? color : ((color >> 1) & 0x7F7F7F);
			}
		}
	}
	modText(92, 12, ready ? 0x40FFFF : 0x808080, "%s", move->name);
}

// The Super Ghost is not a fixed film like the others. In Buu's Fury it waits
// by him for something to chase; here it drifts off the way he faces, bobbing,
// and bursts on the first enemy it touches or when it has gone far enough.
enum { GHOST_FRAMES = 60, GHOST_BURST = 14 };

static void playGhost(uint32_t player) {
	static int burstAt = -1, burstX, burstY;
	int camX = (int) modRead32(LG2_RAM_CAMERA_X), camY = (int) modRead32(LG2_RAM_CAMERA_Y);
	const struct FmFrame* picture = &fmFrames[fmMoves[FM_GHOST].first + fmMoves[FM_GHOST].count - 1];
	const uint8_t* pixels = &fmPixels[picture->pixels];
	int x, y, i;
	if (timer == 0) {
		burstAt = -1;
	}
	if (timer < 16) {
		pose(player, FIRE_POSE, timer < 6 ? 0 : 1);
	} else if (timer == 16) {
		pose(player, 0, 0);
	}
	if (burstAt < 0) {
		int dx, dy, gx, gy;
		turn(14 + timer * 2, 0, &dx, &dy);
		gx = originX + dx - camX;
		gy = originY + dy - camY + ((timer / 6) & 1) * 2 - 1;
		for (y = 0; y < picture->h; ++y) {
			for (x = 0; x < picture->w; ++x) {
				uint8_t p = pixels[y * picture->w + (facing == LEFT ? picture->w - 1 - x : x)];
				int sx = gx - picture->w / 2 + x, sy = gy - picture->h / 2 + y;
				if (p && sx >= 0 && sx < 240 && sy >= 0 && sy < 160) {
					modScreen()[sy * 240 + sx] = fmPalette[p];
				}
			}
		}
		uint32_t found[16];
		int n = lg2FindEnemies(player, found, 16), touched = timer >= GHOST_FRAMES;
		for (i = 0; i < n && !touched; ++i) {
			int ex, ey, w;
			lg2EnemyPlace(found[i], &ex, &ey, &w);
			touched = abs(ex - gx) <= w / 2 + 8 && ey >= gy - 6 && ey - 26 <= gy + 10;
		}
		if (touched) { // everything near the burst takes it
			burstAt = timer;
			burstX = gx + camX;
			burstY = gy + camY;
			for (i = 0; i < n; ++i) {
				int ex, ey, w;
				lg2EnemyPlace(found[i], &ex, &ey, &w);
				if (abs(ex - gx) <= w / 2 + 26 && abs(ey - 12 - gy) <= 26) {
					lg2HitEnemy(player, found[i], current->power);
				}
			}
		}
	} else {
		int age = timer - burstAt, radius = 6 + age * 2, cx = burstX - camX, cy = burstY - camY;
		for (y = -radius; y <= radius; ++y) {
			for (x = -radius; x <= radius; ++x) {
				int d = x * x + y * y, sx = cx + x, sy = cy + y;
				if (d <= radius * radius && d >= (radius - 4) * (radius - 4) && sx >= 0 && sx < 240 && sy >= 0 && sy < 160) {
					modScreen()[sy * 240 + sx] = age < GHOST_BURST / 2 ? 0xFFFFFF : 0xFFE080;
				}
			}
		}
		if (age >= GHOST_BURST) {
			current = 0;
			return;
		}
	}
	++timer;
}

// Whatever stands in the effect's box this frame is hit; a held move hits the
// same enemy again every HIT_EVERY frames, a one-shot move once
static void hitInBox(uint32_t player, int left, int top, int right, int bottom) {
	uint32_t found[16];
	int n = lg2FindEnemies(player, found, 16), i;
	for (i = 0; i < n; ++i) {
		int ex, ey, w, k, slot = -1;
		for (k = 0; k < nStruck; ++k) {
			if (struck[k] == found[i]) {
				slot = k;
			}
		}
		if (slot >= 0 && (current->how != HOLD || modFrame() - struckAt[slot] < HIT_EVERY)) {
			continue;
		}
		lg2EnemyPlace(found[i], &ex, &ey, &w);
		if (ex + w / 2 >= left - 4 && ex - w / 2 <= right + 4 && ey >= top - 6 && ey - 24 <= bottom + 6) {
			if (slot < 0 && nStruck < 16) {
				slot = nStruck++;
				struck[slot] = found[i];
			}
			if (slot >= 0) {
				struckAt[slot] = modFrame();
			}
			lg2HitEnemy(player, found[i], current->power);
		}
	}
}

// A held move: the lead-in, then the held part over and over while B is down
// and there is energy, then the ending
static void playHold(uint32_t player) {
	int camX = (int) modRead32(LG2_RAM_CAMERA_X), camY = (int) modRead32(LG2_RAM_CAMERA_Y);
	const struct HmFrame* frames = &hmFrames[hmMoves[current->art].first];
	int count = hmMoves[current->art].count, holdFrom = hmMoves[current->art].holdFrom, release = hmMoves[current->art].release;
	struct LG2Char* c = lg2Char(lg2FusionHost);
	int holdStart = frames[holdFrom].at, releaseStart = frames[release].at, lastAt = frames[count - 1].at;
	int held = (modKeys() & BTN_B) != 0, i, x, y;
	if (!releasing && timer >= holdStart) {
		if (!held || c->ep == 0 || !lg2FusionKind) {
			releasing = 1;
			timer = releaseStart;
			pose(player, 0, 0); // free to move again at once; the ending plays on by itself
		} else {
			if (timer % HOLD_ENERGY_EVERY == 0) {
				--c->ep;
			}
			if (timer >= releaseStart) {
				timer = holdStart; // round again
			}
		}
	}
	// the Spirit Sword: once the blade is out he spins, the blade going round with him
	int spin = current->art == HM_SWORD && !releasing && timer >= holdStart;
	int spinStep = spin ? ((timer - holdStart) / SPIN_TICK) % SPIN_FRAMES : 0;
	double spinAngle = spinStep * 6.2831853 / SPIN_FRAMES;
	if (!releasing) {
		if (spin) {
			pose(player, SPIN_ANIMATION, spinStep);
		} else {
			pose(player, FIRE_POSE, timer < 6 ? 0 : 1);
		}
	}
	const struct HmFrame* shown = 0;
	for (i = 0; i < count; ++i) {
		if (frames[i].at == (timer & ~1)) {
			shown = &frames[i];
		}
	}
	if (shown) {
		const uint8_t* pixels = &hmPixels[shown->pixels];
		int left = 9999, right = -9999, top = 9999, bottom = -9999;
		for (y = 0; y < shown->h; ++y) {
			for (x = 0; x < shown->w; ++x) {
				uint8_t p = pixels[y * shown->w + x];
				int sx, sy;
				if (!p) {
					continue;
				}
				if (spin) { // round the hand, whichever way he faces
					double rx = shown->x + x, ry = shown->y + y;
					sx = (int) (rx * cos(spinAngle) - ry * sin(spinAngle));
					sy = (int) (rx * sin(spinAngle) + ry * cos(spinAngle));
				} else {
					turn(shown->x + x, shown->y + y, &sx, &sy);
				}
				sx += originX - camX;
				sy += originY - camY;
				if (sx >= 0 && sx < 240 && sy >= 0 && sy < 160) {
					modScreen()[sy * 240 + sx] = hmPalette[p];
				}
				left = sx < left ? sx : left;
				right = sx > right ? sx : right;
				top = sy < top ? sy : top;
				bottom = sy > bottom ? sy : bottom;
			}
		}
		if (timer >= current->hitsFrom && right >= left) {
			hitInBox(player, left, top, right, bottom);
		}
	}
	++timer;
	if (timer > lastAt + 2) {
		modLog("%s over: %d enemies hit", current->name, nStruck);
		current = 0;
	}
}

static void play(uint32_t player) {
	if (current->how == GHOST) {
		playGhost(player);
		return;
	}
	if (current->how == HOLD) {
		playHold(player);
		return;
	}
	int camX = (int) modRead32(LG2_RAM_CAMERA_X), camY = (int) modRead32(LG2_RAM_CAMERA_Y);
	const struct FmFrame* frames = &fmFrames[fmMoves[current->art].first];
	int count = fmMoves[current->art].count, i, x, y;
	const struct FmFrame* shown = 0;
	int lastAt = frames[count - 1].at, shift = 0, onScreen = 0;
	for (i = 0; i < count; ++i) { // the capture has a frame every other game frame
		if (frames[i].at == (timer & ~1)) {
			shown = &frames[i];
		}
		if (current->speed && timer > lastAt && frames[i].at == current->travelFrom) {
			shown = &frames[i]; // flying on past the end of the capture
			shift = (timer - current->travelFrom) * current->speed;
		}
	}
	if (timer < STILL_FRAMES) {
		pose(player, FIRE_POSE, timer < 6 ? 0 : 1);
	} else if (timer == STILL_FRAMES) {
		pose(player, 0, 0);
	}
	if (shown) {
		const uint8_t* pixels = &fmPixels[shown->pixels];
		int left = 9999, right = -9999, top = 9999, bottom = -9999;
		for (y = 0; y < shown->h; ++y) {
			for (x = 0; x < shown->w; ++x) {
				uint8_t p = pixels[y * shown->w + x];
				int sx, sy;
				if (!p) {
					continue;
				}
				turn(shown->x + shift + x, shown->y + y, &sx, &sy);
				sx += originX - camX;
				sy += originY - camY;
				if (sx >= 0 && sx < 240 && sy >= 0 && sy < 160) {
					modScreen()[sy * 240 + sx] = fmPalette[p];
					++onScreen;
				}
				left = sx < left ? sx : left;
				right = sx > right ? sx : right;
				top = sy < top ? sy : top;
				bottom = sy > bottom ? sy : bottom;
			}
		}
		if (timer >= current->hitsFrom && right >= left) {
			hitInBox(player, left, top, right, bottom);
		}
	}
	++timer;
	if (shift ? !onScreen : (timer > lastAt + 2 && timer > STILL_FRAMES)) {
		modLog("%s over: %d enemies hit", current->name, nStruck);
		current = 0; // the capture is over, or the shot has flown off the screen
	}
}

static void frame(void) {
	static uint32_t lastKeys;
	static unsigned noteUntil;
	uint32_t keys = modKeys(), pressed = keys & ~lastKeys;
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	lastKeys = keys;
	if (modPaused()) {
		return;
	}
	if (!lg2IsPlayer(player) || !lg2FusionKind) {
		current = 0; // a menu, the area changed, or the fusion ended
		return;
	}
	struct LG2Char* c = lg2Char(lg2FusionHost);
	// A fused character always has both special slots and the energy blast, and
	// never the transformation, whatever the game does to the record meanwhile
	c->flags = (c->flags & ~LG2_ATTACK_OWNED(2)) | LG2_ATTACK_OWNED(0) | LG2_ATTACK_OWNED(1) | LG2_ATTACK_OWNED(3);
	const struct Move* move = selected();
	if (move) {
		drawChoice(move, current || c->ep >= move->cost); // (also while one is going off: the game's own icon is never the right one)
	}
	if (move && (!current || (current->how == HOLD && releasing))) { // (a held move's ending does not stop the next one)
		if ((pressed & BTN_B) && c->hp > 0) {
			if (c->ep < move->cost) {
				noteUntil = modFrame() + 60;
			} else {
				uint32_t frameRecord = modRead32(player + LG2_ACTOR_FRAME_RECORD);
				int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16, h = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 3) : 32;
				c->ep -= move->cost;
				current = move;
				timer = 0;
				nStruck = 0;
				releasing = 0;
				modLog("%s fired by %s as %s, facing %d, power %d", move->name, lg2Name(lg2FusionHost), lg2FusionKind == GOGETA ? "Gogeta" : lg2FusionKind == VEGITO ? "Vegito" : "Gotenks",
				       modRead8(player + 0x10) & 3, modRead8(player + LG2_PLAYER_STATS + LG2_POW));
				facing = modRead8(player + 0x10) & 3;
				originX = (int) modRead32(player + LG2_ACTOR_SCREEN_X) + w / 2 + (int) modRead32(LG2_RAM_CAMERA_X);
				originY = (int) modRead32(player + LG2_ACTOR_SCREEN_Y) + h - FM_HAND_UP + (int) modRead32(LG2_RAM_CAMERA_Y);
			}
		}
	}
	if (current) {
		play(player);
	}
	if (modFrame() < noteUntil) {
		modText(92, 20, 0x4040FF, "NOT ENOUGH ENERGY");
	}
}

MOD_INIT(fusion_moves) {
	modFilterKeys(filter);
	modOnFrame(frame);
}
