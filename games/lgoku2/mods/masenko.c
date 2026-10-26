// Gohan's Masenko, the way Buu's Fury does it: hold B and he raises a ball of
// energy over his head while a target marker runs out along the ground ahead of
// him; let go and the ball is lobbed in an arc onto the marker, where it bursts
// and hurts every enemy near it.
//
// The game's own Masenko (a straight shot) never fires: while the Masenko is
// the selected attack this mod keeps B from the game and does everything
// itself. The burst's damage goes through the game's own "enemy takes damage"
// routine, so enemies react, die and give experience as usual.
#include "lg2.h"

#include <math.h>
#include <string.h>

enum { BTN_B = 2 };
enum { MASENKO_SLOT = 0 };
enum { DOWN, UP, LEFT, RIGHT };              // LG2 facing directions (actor +0x10)

#define ENERGY_COST 12
#define NEAREST 30          // how far out the marker starts, in pixels
#define FARTHEST 112
#define MARKER_SPEED 1.6    // pixels a frame while B is held
#define FLIGHT_FRAMES 26
#define BURST_FRAMES 20
#define BURST_RADIUS 40
#define BASE_POWER 9000     // an energy blast is 1024
#define POSE_ANIMATION 16   // the overhead throw
#define SPARE_ANIMATION 4   // arms raised, for the outfits that have no overhead throw

enum { IDLE, CHARGING, FLYING, BURSTING };
static int state, timer, facing;
static double reach;
static int fromX, fromY, toX, toY; // world pixels

static int selected(void) {
	const struct LG2Char* c = lg2Char(LG2_GOHAN);
	return lg2CurrentChar() == LG2_GOHAN && c->attack[MASENKO_SLOT] == LG2_ATTACK_MASENKO && c->selectedAttack == MASENKO_SLOT &&
	       (c->flags & LG2_ATTACK_OWNED(MASENKO_SLOT)) && lg2IsPlayer(modRead32(LG2_RAM_PLAYER_PTR));
}

// While it is selected the game never gets B; while he is winding up, nothing at all
static uint32_t filter(uint32_t keys) {
	if (state == CHARGING) {
		return 0;
	}
	return selected() ? keys & ~BTN_B : keys;
}

// ---- drawing ------------------------------------------------------------------------------
static void blend(int x, int y, uint32_t color, int amount) {
	if (x < 0 || x >= 240 || y < 0 || y >= 160 || amount <= 0) {
		return;
	}
	if (amount > 256) {
		amount = 256;
	}
	uint32_t* p = &modScreen()[y * 240 + x];
	int r = *p & 0xFF, g = (*p >> 8) & 0xFF, b = (*p >> 16) & 0xFF;
	r += (((int) (color & 0xFF)) - r) * amount / 256;
	g += (((int) ((color >> 8) & 0xFF)) - g) * amount / 256;
	b += (((int) ((color >> 16) & 0xFF)) - b) * amount / 256;
	*p = r | (g << 8) | (b << 16);
}

// The ball: white-hot middle, yellow body, a soft glow round it
static void drawBall(int cx, int cy, double radius) {
	int reachOut = (int) radius + 4, x, y;
	for (y = -reachOut; y <= reachOut; ++y) {
		for (x = -reachOut; x <= reachOut; ++x) {
			double d = sqrt((double) (x * x + y * y));
			if (d <= radius * 0.45) {
				blend(cx + x, cy + y, 0xFFFFFF, 256);
			} else if (d <= radius) {
				blend(cx + x, cy + y, 0x40F0FF, 256);
			} else if (d <= radius + 1) {
				blend(cx + x, cy + y, 0x0098F0, 220);
			} else if (d <= radius + 4) {
				blend(cx + x, cy + y, 0x60E8FF, (int) (110 * (radius + 4 - d) / 3));
			}
		}
	}
}

// The target marker: four corner brackets, as in Buu's Fury, pulsing
static void drawMarker(int cx, int cy) {
	int half = 7 + (int) (modFrame() / 8) % 2, i, sx, sy;
	for (sx = -1; sx <= 1; sx += 2) {
		for (sy = -1; sy <= 1; sy += 2) {
			for (i = 0; i < 4; ++i) {
				blend(cx + sx * (half - i), cy + sy * half * 2 / 3, 0xFFFF40, 256);
				blend(cx + sx * half, cy + sy * (half * 2 / 3 - i), 0xFFFF40, 256);
			}
		}
	}
}

static void drawBurst(int cx, int cy, int age) {
	double grow = (double) age / BURST_FRAMES;
	int radius = (int) (BURST_RADIUS * (0.35 + 0.65 * grow)), x, y;
	for (y = -radius; y <= radius; ++y) {
		for (x = -radius; x <= radius; ++x) {
			double d = sqrt((double) (x * x + y * y * 2)) / radius; // flattened: it spreads over the ground
			if (d > 1.0) {
				continue;
			}
			int fade = (int) (256 * (1.0 - grow));
			if (d < 0.5 * (1.0 - grow)) {
				blend(cx + x, cy + y, 0xFFFFFF, fade + 60);
			} else if (d > 0.8) {
				blend(cx + x, cy + y, 0x00A0FF, fade);
			} else {
				blend(cx + x, cy + y, 0x40F0FF, fade * 3 / 4);
			}
		}
	}
}

// ---- the game side ----------------------------------------------------------------------------
// Hold the player in a frame of the throwing pose
static void pose(uint32_t player, int frameNumber) {
	uint32_t def = modRead32(player + LG2_ACTOR_SPRITE_DEF);
	static const uint8_t zero[16];
	int animation = POSE_ANIMATION;
	const uint8_t* rows = (def >> 24) == 8 ? modRom((def & 0xFFFFFF) + LG2_SPRITE_DEF_ANIMATIONS) : 0;
	if (!rows) {
		return;
	}
	if (modRom(def & 0xFFFFFF)[2] <= POSE_ANIMATION || !memcmp(rows + POSE_ANIMATION * 16, zero, 16)) {
		animation = SPARE_ANIMATION;
	}
	modWrite16(player + 0x12, 0);
	modWrite16(player + LG2_ACTOR_FRAME, (uint16_t) frameNumber);
	modWrite8(player + 8, 0x28);
	modWrite32(player + LG2_ACTOR_ANIMATION, def + LG2_SPRITE_DEF_ANIMATIONS + 16 * animation);
}

static void stand(uint32_t player) {
	uint32_t def = modRead32(player + LG2_ACTOR_SPRITE_DEF);
	modWrite16(player + 0x12, 0);
	modWrite16(player + LG2_ACTOR_FRAME, 0);
	modWrite8(player + 8, 0x28);
	modWrite32(player + LG2_ACTOR_ANIMATION, def + LG2_SPRITE_DEF_ANIMATIONS);
}

// Every enemy near the marker takes the hit
static void burst(uint32_t player) {
	uint32_t found[16];
	int n = lg2FindEnemies(player, found, 16), i;
	int cx = toX - (int) modRead32(LG2_RAM_CAMERA_X), cy = toY - (int) modRead32(LG2_RAM_CAMERA_Y), hits = 0;
	for (i = 0; i < n; ++i) {
		int ex, ey, w;
		lg2EnemyPlace(found[i], &ex, &ey, &w);
		if ((ex - cx) * (ex - cx) + (ey - cy) * (ey - cy) * 2 <= (BURST_RADIUS + w / 2) * (BURST_RADIUS + w / 2)) {
			lg2HitEnemy(player, found[i], BASE_POWER);
			++hits;
		}
	}
	modLog("Masenko burst at %d,%d: %d enemies about, %d hit", cx, cy, n, hits);
}

// ---- every frame ------------------------------------------------------------------------------
static void frame(void) {
	static uint32_t lastKeys;
	static unsigned noteUntil;
	uint32_t keys = modKeys(), pressed = keys & ~lastKeys;
	uint32_t player = modRead32(LG2_RAM_PLAYER_PTR);
	lastKeys = keys;
	if (modPaused()) {
		return;
	}
	if (!lg2IsPlayer(player)) {
		state = IDLE; // a menu, the world map, or the area changed
		return;
	}
	int camX = (int) modRead32(LG2_RAM_CAMERA_X), camY = (int) modRead32(LG2_RAM_CAMERA_Y);
	uint32_t frameRecord = modRead32(player + LG2_ACTOR_FRAME_RECORD);
	int w = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 2) : 16, h = (frameRecord >> 24) == 8 ? modRead8(frameRecord + 3) : 32;
	int px = (int) modRead32(player + LG2_ACTOR_SCREEN_X) + w / 2, top = (int) modRead32(player + LG2_ACTOR_SCREEN_Y);
	struct LG2Char* c = lg2Char(LG2_GOHAN);

	if (state == IDLE && selected() && (pressed & BTN_B) && c->hp > 0) {
		if (c->ep < ENERGY_COST) {
			noteUntil = modFrame() + 60;
		} else {
			state = CHARGING;
			timer = 0;
			reach = NEAREST;
			facing = modRead8(player + 0x10) & 3;
		}
	}
	if (state == CHARGING) {
		static const int8_t dirX[4] = { 0, 0, -1, 1 }, dirY[4] = { 1, -1, 0, 0 };
		if (!selected() || c->hp == 0) {
			state = IDLE;
			stand(player);
		} else {
			++timer;
			if (reach < FARTHEST) {
				reach += MARKER_SPEED;
			}
			pose(player, timer < 8 ? 0 : timer < 16 ? 1 : 2);
			int feetY = top + h - 4;
			toX = px + (int) (dirX[facing] * reach) + camX;
			toY = feetY + (int) (dirY[facing] * reach * 0.8) + camY;
			fromX = px + camX;
			fromY = top - 6 + camY;
			double radius = 3.0 + (timer > 30 ? 30 : timer) * 0.12 + ((modFrame() / 3) & 1) * 0.6;
			drawBall(px, top - 6, radius);
			drawMarker(toX - camX, toY - camY);
			if (!(keys & BTN_B) && timer >= 10) { // let go: throw it
				c->ep -= ENERGY_COST;
				state = FLYING;
				timer = 0;
				pose(player, 5);
			}
		}
	} else if (state == FLYING) {
		double t = (double) ++timer / FLIGHT_FRAMES;
		int x = fromX + (int) ((toX - fromX) * t) - camX;
		int y = fromY + (int) ((toY - fromY) * t) - camY - (int) (38 * sin(t * 3.14159265));
		drawMarker(toX - camX, toY - camY);
		drawBall(x, y, 5.5);
		if (timer == 8) {
			stand(player);
		}
		if (timer >= FLIGHT_FRAMES) {
			state = BURSTING;
			timer = 0;
			burst(player);
		}
	} else if (state == BURSTING) {
		drawBurst(toX - camX, toY - camY, timer);
		if (++timer >= BURST_FRAMES) {
			state = IDLE;
		}
	}
	if (modFrame() < noteUntil) {
		modText(px - 22, top - 10, 0x4040FF, "NO ENERGY");
	}
}

MOD_INIT(masenko) {
	modFilterKeys(filter);
	modOnFrame(frame);
}
