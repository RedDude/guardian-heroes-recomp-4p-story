// 4-player story mode (experimental). Enabled only when the environment variable GH_4P=1.
//
// Guest data used here (Guardian Heroes XBLA, retail v2 xex):
//   GlobalWork 0x829CBBA8: +0x14 pad_kind[12] (player -> pad, -1 = empty), +0x2c join flag.
//   Story work 0x829CBFE8 (sub_82495B30): +i*0x24 party block i (+0 EXP, +0xc u16, +0xe level),
//     +0x3d8+i party kind (s8, -1 = none), +(slot+0xd1)*8 character registry (weak handles).
//   The story only uses party 0..2 (0 = AI ally, 1 = P1, 2 = P2); the arrays hold 12 entries, so
//   P3/P4 live in registry slots 30/31 (kind at work+0x3d8+30/31, party block 10/11).
//
// Features:
//   * Join: an unassigned pad pressing START while P2 exists runs the game's own "P2 joins" flow
//     (character select + parameter screen + stage restart) with P2 parked aside; the choice is then
//     moved to party 3 or 4 and P2 is restored.
//   * P3/P4 are created next to P1/P2 by the party routine (sub_824E2970) and their level/EXP are
//     saved with the others at the end of the stage (sub_82499F40).
//   * AI: the game attaches a "brain" object to a character with sub_82479E60(chara, code); the
//     character then ignores its pad and the brain drives it (story NPC heroes, the AI ally).
//     GH_AI=1234 (any of the digits) starts those players under the hero's own brain;
//     F5..F8 toggle P1..P4 while the game window has focus.
//   * The camera (sub_82498808) frames every focus point that players register each frame
//     (sub_8246B2D8 -> sub_82496BB0), so P3/P4 are framed once they are flagged as on screen.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

extern "C" REX_FUNC(__imp__sub_824E2970);   // create party (stage load)
extern "C" REX_FUNC(__imp__sub_824E2AC0);   // assign story-hero NPC slots (stage+0x1719c..)
extern "C" REX_FUNC(__imp__sub_824E3618);   // in-stage "P2 joins" check
extern "C" REX_FUNC(__imp__sub_82499F40);   // save party progress / checkpoint
extern "C" REX_FUNC(__imp__sub_82498808);   // camera update (once per frame in a stage)
extern "C" REX_FUNC(__imp__sub_82500658);   // parameter screen: next/previous hero index
extern "C" REX_FUNC(__imp__sub_824E4308);   // continue: downed P1/P2 spend a credit
extern "C" REX_FUNC(__imp__sub_825CF040);   // XamInputGetState(pad, 1, state)
extern "C" REX_FUNC(sub_8243FCE0);  // init create-params struct
extern "C" REX_FUNC(sub_8247ACC0);  // create character (params, kind, slot, x, z)
extern "C" REX_FUNC(sub_824E5758);  // load the sprite pack of a kind now (stage, kind), as for P1/P2
extern "C" REX_FUNC(sub_8241BAF0);  // controller: bind to pad (type 1)
extern "C" REX_FUNC(sub_8249DD68);  // HUD panel for a player
extern "C" REX_FUNC(sub_8247AD30);  // companion for kind 3 (Randy's rabbit)
extern "C" REX_FUNC(sub_82479E60);  // attach brain (chara, code): 0 = none
extern "C" REX_FUNC(sub_82419C38);  // pad connected (pad)
extern "C" REX_FUNC(sub_8241B800);  // pad state pointer (pad); +0 bit 0x400000 = START pressed
extern "C" REX_FUNC(sub_82421B40);  // menu/pause active
extern "C" REX_FUNC(sub_8242B528);  // online session
extern "C" REX_FUNC(sub_82496728);  // set_pad_kind(gw, player, pad)
extern "C" REX_FUNC(__imp__sub_8249CC40);  // HUD: per-panel update (position in +0x888/+0x88c)
extern "C" REX_FUNC(__imp__sub_8249D2C0);  // HUD: layout of all panels (once per frame)
extern "C" REX_FUNC(__imp__sub_8247FE88);  // HUD: "Credit(s) N" sprite quads
extern "C" REX_FUNC(__imp__sub_824E0C10);  // pause menu: constructor (stage+0xa40)
extern "C" REX_FUNC(__imp__sub_824DEA90);  // pause menu: update
extern "C" REX_FUNC(__imp__sub_824E01D0);  // pause menu: draw
extern "C" REX_FUNC(__imp__sub_824A6C40);  // status window: open (vtable +0x40)
extern "C" REX_FUNC(__imp__sub_824A5158);  // window: show again
extern "C" REX_FUNC(__imp__sub_824A5188);  // window: close
extern "C" REX_FUNC(__imp__sub_82468FA8);  // hero: load stats from party block work+slot*0x24
extern "C" REX_FUNC(__imp__sub_82468E90);  // hero: store level/EXP/HP into work+slot*0x24
extern "C" REX_FUNC(__imp__sub_82512B90);  // Han: init (resets work+slot*0x24+0x1b)
extern "C" REX_FUNC(__imp__sub_824C0470);  // window: kind of the pad's player = work+0x3d8+(player+1)
extern "C" REX_FUNC(__imp__sub_8243E510);  // collects party kinds 0..11 to preload
extern "C" REX_FUNC(__imp__sub_824EB8D0);  // stage load: collects party kinds 0..11 to preload
extern "C" REX_FUNC(__imp__sub_8247A530);  // create character from a descriptor (+0 slot, +2 kind)
extern "C" REX_FUNC(__imp__sub_82418928);  // file buffer alloc (size, heap type) -> XMemAlloc
extern "C" REX_FUNC(sub_826A3900);  // operator new
extern "C" REX_FUNC(sub_824B2ED0);  // window base constructor
extern "C" REX_FUNC(sub_824B57E8);  // status window setup (window, player index), in-stage flavour
extern "C" REX_FUNC(sub_825122F0);  // window destructor (window, 1 = free)
extern "C" REX_FUNC(sub_824A5218);  // window update (vtable +4)
extern "C" REX_FUNC(sub_824AFC38);  // status window draw (vtable +0x30)

namespace {

constexpr uint32_t kGlobalWork = 0x829CBBA8;
constexpr uint32_t kPadKind = kGlobalWork + 0x14;  // s8[12]
constexpr uint32_t kWork = 0x829CBFE8;
constexpr uint32_t kKinds = kWork + 0x3d8;  // s8[32], indexed by registry slot
constexpr uint32_t kBlock = 0x24;
constexpr uint32_t kCamera = 0x829B2498;

inline uint8_t R8(uint8_t* b, uint32_t a) { return b[a]; }
inline int8_t S8(uint8_t* b, uint32_t a) { return (int8_t)b[a]; }
inline void W8(uint8_t* b, uint32_t a, uint8_t v) { b[a] = v; }
inline uint16_t R16(uint8_t* b, uint32_t a) {
  uint16_t v; std::memcpy(&v, b + a, 2); return __builtin_bswap16(v);
}
inline void W16(uint8_t* b, uint32_t a, uint16_t v) {
  v = __builtin_bswap16(v); std::memcpy(b + a, &v, 2);
}
inline uint32_t R32(uint8_t* b, uint32_t a) {
  uint32_t v; std::memcpy(&v, b + a, 4); return __builtin_bswap32(v);
}
inline void W32(uint8_t* b, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v); std::memcpy(b + a, &v, 4);
}
inline void WF(uint8_t* b, uint32_t a, float f) { uint32_t v; std::memcpy(&v, &f, 4); W32(b, a, v); }

// Weak handle (8 bytes): +4 -> ref record whose +0 is the object (sub_8242AF48).
uint32_t Deref(uint8_t* b, uint32_t handle) {
  uint32_t r = R32(b, handle + 4);
  return r ? R32(b, r) : 0;
}
uint32_t Chara(uint8_t* b, uint32_t slot) { return Deref(b, kWork + (slot + 0xd1) * 8); }

// Calls into guest code from a hook, with its own frame below the hooked function's stack pointer.
struct Guest {
  rex::CallFrame f;
  uint8_t* base;
  Guest(PPCContext& parent, uint8_t* b) : f(parent), base(b) {
    f.ctx.r1.u32 = parent.r1.u32 - 0x200;
    W32(base, f.ctx.r1.u32, parent.r1.u32);  // back chain
  }
  uint32_t stack(uint32_t off) const { return f.ctx.r1.u32 + 0x100 + off; }  // scratch area
  uint32_t operator()(PPCFunc* fn, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0,
                      uint64_t e = 0) {
    const uint32_t sp = f.ctx.r1.u32;
    f.ctx.r3.u64 = a; f.ctx.r4.u64 = b; f.ctx.r5.u64 = c; f.ctx.r6.u64 = d; f.ctx.r7.u64 = e;
    fn(f.ctx, base);
    f.ctx.r1.u32 = sp;
    return f.ctx.r3.u32;
  }
};

const char* Env(const char* n) { const char* s = std::getenv(n); return (s && *s) ? s : nullptr; }

bool Enabled() {
  static int e = -1;
  if (e < 0) { const char* s = Env("GH_4P"); e = (s && *s == '1') ? 1 : 0; }
  return e == 1;
}

// work+0x3ac: 0 = story (the HUD/pause code uses the same test).
inline bool Story(uint8_t* base) { return R8(base, 0x829CBFE8 + 0x3ac) == 0; }

// GH_4P_CREDITS=0 keeps the original credits (finite, shown on the HUD).
bool InfiniteCredits() {
  static int v = -1;
  if (v < 0) { const char* s = std::getenv("GH_4P_CREDITS"); v = (s && *s == '0') ? 0 : 1; }
  return v == 1;
}

int MaxPlayers() {
  const char* s = Env("GH_4P_PLAYERS");
  int n = s ? std::atoi(s) : 4;
  return n < 2 ? 2 : (n > 4 ? 4 : n);
}

// ---------------------------------------------------------------------------------------------
// Extra players
// ---------------------------------------------------------------------------------------------

uint32_t SlotFor(int p) {  // registry slot of player p (2 = P3, 3 = P4)
  const char* es = Env(p == 2 ? "GH_4P_SLOT2" : "GH_4P_SLOT3");
  return es ? (uint32_t)std::atoi(es) : (p == 2 ? 30u : 31u);
}
// The game indexes work+0x3d8 (kind, 32 entries) by registry slot ("is the character in slot s a party
// member?"; stage scripts ask it for the NPC heroes in slots 3..5), and the party blocks
// work+i*0x24 (12 entries) by slot too. P3/P4 keep their kind at their own slots (30/31) and their
// block at 10/11 (no hero ever uses slots 6..11); see the shadow block hooks below.
inline uint32_t KindOf(int p) { return kKinds + SlotFor(p); }
inline uint32_t BlockOf(int p) { return kWork + (p == 2 ? 10u : 11u) * kBlock; }

int g_kind[4] = {-1, -1, -1, -1};  // kind spawned this stage for P3/P4 (diagnostics, hero slots)

// First pad not assigned to P1..P4 (or -1).
int FreePad(uint8_t* base) {
  int used = 0;
  for (int i = 0; i < 4; i++) {
    int8_t k = S8(base, kPadKind + i);
    if (k >= 0) used |= 1 << k;
  }
  for (int c = 0; c < 4; c++)
    if (!(used & (1 << c))) return c;
  return -1;
}

void SpawnExtra(Guest& g, int p) {
  uint8_t* base = g.base;
  const int kind = S8(base, KindOf(p));
  int8_t pad = S8(base, kPadKind + p);
  if (pad < 0) {
    pad = (int8_t)FreePad(base);
    if (pad < 0) { REXKRNL_WARN("story4p: no free pad for P{}", p + 1); return; }
    W8(base, kPadKind + p, (uint8_t)pad);
  }
  const uint32_t slot = SlotFor(p);
  const uint32_t blk = BlockOf(p);
  const uint32_t params = g.stack(0);

  // A stage loads the sprite packs of party 0..2 and of its own cast (the five heroes only where
  // they appear as NPCs); load P3/P4's now, or they (and later creations) find no animation data.
  g(sub_824E5758, 0, (uint64_t)(int64_t)kind);
  g(sub_8243FCE0, params);
  W8(base, params + 1, 0);
  W16(base, params + 8, 0);                   // no brain: driven by its pad
  W8(base, params + 6, R8(base, blk + 0xe));  // level
  const uint32_t ch = g(sub_8247ACC0, params, (uint64_t)kind, slot, 0x400000,
                        p == 2 ? 0x300000 : 0x900000);
  if (!ch) { REXKRNL_WARN("story4p: create failed for P{}", p + 1); return; }

  W8(base, ch + 0x140, 0xFF);
  W8(base, ch + 0xE0, (uint8_t)p);  // player index
  W8(base, ch + 0xDC, 1);           // is a player
  // +0xEF bit 0x40 = "has entered the camera window": the screen-edge clamp and the camera focus
  // (sub_8246B2D8) only engage after it, and P3/P4 may spawn outside the view.
  W8(base, ch + 0xEF, R8(base, ch + 0xEF) | 0x40);
  if (R8(base, blk + 0xe)) {  // carry EXP the way sub_82499F40 stores it
    W32(base, ch + 0xC0, R32(base, blk + 0));
    W16(base, ch + 0xD6, R16(base, blk + 0xc));
  }
  g(sub_8241BAF0, ch + 0x478);  // same order as the party routine: bind, then set the pad
  W8(base, ch + 0x48F, (uint8_t)pad);
  g(sub_8249DD68, ch);
  if (kind == 3) {
    uint32_t pet = g(sub_8247AD30, ch, R32(base, ch + 12), R32(base, ch + 20));
    if (pet) W8(base, pet + 0x140, 0xFF);
  }
  g_kind[p] = kind;
  REXKRNL_INFO("story4p: P{} kind {} level {} slot {} pad {} chara {:08X}", p + 1, kind,
               R8(base, blk + 0xe), slot, (int)pad, ch);
}

int AutoKind(uint8_t* base, int p) {
  const char* s = Env(p == 2 ? "GH_4P_KIND2" : "GH_4P_KIND3");
  if (s) return std::atoi(s);
  int used = 0;
  for (uint32_t slot : {1u, 2u, SlotFor(2), SlotFor(3)}) {
    int k = S8(base, kKinds + slot);
    if (k == 7) k = 2;
    if (k >= 0 && k < 31) used |= 1 << k;
  }
  for (int k = 1; k <= 5; k++)
    if (!(used & (1 << k))) return k;
  return 1;
}

// ---------------------------------------------------------------------------------------------
// Join: P3/P4 borrow the P2 join flow
// ---------------------------------------------------------------------------------------------

struct Parked {
  bool active = false;
  int player = -1;     // 2 = P3, 3 = P4
  int pad = -1;        // joining pad
  int8_t p2_pad = -1;  // P2's pad_kind
  int8_t p2_kind = -1;
  uint8_t p2_block[kBlock] = {};
} g_join;

void Park(uint8_t* base, int player, int pad) {
  g_join.active = true;
  g_join.player = player;
  g_join.pad = pad;
  g_join.p2_pad = S8(base, kPadKind + 1);
  g_join.p2_kind = S8(base, kKinds + 2);
  std::memcpy(g_join.p2_block, base + kWork + 2 * kBlock, kBlock);
  W8(base, kKinds + 2, 0xFF);  // the select screen sees an empty P2 seat
  std::memset(base + kWork + 2 * kBlock, 0, kBlock);
}

// Called before the party is created: move what was chosen for "P2" to the joining player.
void Unpark(uint8_t* base) {
  if (!g_join.active) return;
  const int p = g_join.player;
  const int8_t chosen = S8(base, kKinds + 2);
  if (chosen >= 0) {
    W8(base, KindOf(p), (uint8_t)chosen);
    std::memcpy(base + BlockOf(p), base + kWork + 2 * kBlock, kBlock);
    W8(base, kPadKind + p, (uint8_t)g_join.pad);
    REXKRNL_INFO("story4p: P{} joined with kind {} (pad {})", p + 1, (int)chosen, g_join.pad);
  } else {
    REXKRNL_INFO("story4p: P{} join cancelled", p + 1);
  }
  W8(base, kKinds + 2, (uint8_t)g_join.p2_kind);
  std::memcpy(base + kWork + 2 * kBlock, g_join.p2_block, kBlock);
  W8(base, kPadKind + 1, (uint8_t)g_join.p2_pad);
  g_join.active = false;
}

// Parameter screen hero index (panel +0x81c) -> kind: Han, Randy, Ginjirou, Nicole, Serena.
constexpr int kPanelKind[5] = {2, 3, 4, 5, 1};

// While P3/P4 join through the P2 panel, the heroes of P2/P3/P4 are taken too (the screen itself
// only avoids P1's).
bool PanelTaken(uint8_t* base, int index) {
  if (index < 0 || index > 4) return false;
  const int kind = kPanelKind[index];
  auto same = [&](int k) { return k >= 0 && (k == 7 ? 2 : k) == kind; };
  if (same(g_join.p2_kind)) return true;
  for (int p = 2; p < 4; p++)
    if (p != g_join.player && same(S8(base, KindOf(p)))) return true;
  return false;
}

// ---------------------------------------------------------------------------------------------
// AI
// ---------------------------------------------------------------------------------------------

// Brain code (sub_82479E60) of each hero, from the brain classes that live next to the hero's
// own class: 2 Serena, 3 Han, 4 Randy, 0x21 Ginjirou, 0x28 Nicole.
int BrainFor(int kind) {
  switch (kind) {
    case 1: return 0x02;
    case 2: case 7: return 0x03;
    case 3: return 0x04;
    case 4: return 0x21;
    case 5: return 0x28;
    default: return 0x02;
  }
}

bool g_ai[4];
uint32_t g_brain[4];  // brain we attached (to detach only our own)
int g_retry[4];
uint32_t g_frame;

void InitAi() {
  static bool done = false;
  if (done) return;
  done = true;
  if (const char* s = Env("GH_AI"))
    for (; *s; s++)
      if (*s >= '1' && *s <= '4') g_ai[*s - '1'] = true;
}

uint32_t FindPlayer(uint8_t* base, int p, uint32_t* slot_out = nullptr) {
  for (uint32_t s = 0; s < 32; s++) {
    uint32_t ch = Chara(base, s);
    if (ch && R8(base, ch + 0xDC) == 1 && S8(base, ch + 0xE0) == p) {
      if (slot_out) *slot_out = s;
      return ch;
    }
  }
  return 0;
}

// Debug commands: F9 = enemies down to 1 HP, F10 = clear the stage (go to the next one).
bool g_kill_req, g_skip_req;

void PollKeys() {
#ifdef _WIN32
  static bool prev[4], prev_dbg[2];
  HWND fg = GetForegroundWindow();
  DWORD pid = 0;
  if (fg) GetWindowThreadProcessId(fg, &pid);
  const bool focus = pid == GetCurrentProcessId();
  for (int i = 0; i < 4; i++) {
    bool down = focus && (GetAsyncKeyState(VK_F5 + i) & 0x8000);
    if (down && !prev[i]) {
      g_ai[i] = !g_ai[i];
      REXKRNL_INFO("story4p: AI P{} {}", i + 1, g_ai[i] ? "on" : "off");
    }
    prev[i] = down;
  }
  for (int i = 0; i < 2; i++) {
    bool down = focus && (GetAsyncKeyState(VK_F9 + i) & 0x8000);
    if (down && !prev_dbg[i]) {
      (i == 0 ? g_kill_req : g_skip_req) = true;
      REXKRNL_INFO("story4p: debug {}", i == 0 ? "enemies to 1 HP" : "clear stage");
    }
    prev_dbg[i] = down;
  }
#endif
}

// Enemies = characters on another team (+0xde, -1 = none) than P1. HP 1: the next hit downs them
// through the game's own damage, death and EXP code.
int WeakenEnemies(uint8_t* base) {
  uint32_t p1 = 0;
  for (uint32_t s = 0; s < 32 && !p1; s++) {
    uint32_t ch = Chara(base, s);
    if (ch && R8(base, ch + 0xDC) == 1) p1 = ch;
  }
  if (!p1) return 0;
  const int8_t team = S8(base, p1 + 0xDE);
  int n = 0;
  for (uint32_t s = 0; s < 32; s++) {
    uint32_t ch = Chara(base, s);
    if (!ch || R8(base, ch + 0xDC)) continue;
    const int8_t t = S8(base, ch + 0xDE);
    const int16_t hp = (int16_t)R16(base, ch + 0xCA);
    if (t == team || t == -1 || hp <= 1) continue;
    W16(base, ch + 0xCA, 1);
    n++;
  }
  return n;
}

// GH_AUTO_KILL=1: enemies always at 1 HP. GH_AUTO_SKIP=N: clear each stage after N seconds of play.
void AutoDebug(uint8_t* base) {
  static int kill = -1, skip = -1;
  static uint32_t played;
  if (kill < 0) {
    const char* k = Env("GH_AUTO_KILL");
    const char* s = Env("GH_AUTO_SKIP");
    kill = (k && *k == '1') ? 1 : 0;
    skip = s ? std::atoi(s) : 0;
  }
  const bool event = R8(base, kWork + 0x3b4) != 0;
  if (g_kill_req || (kill && !event)) {
    int n = WeakenEnemies(base);
    if (g_kill_req) REXKRNL_INFO("story4p: {} enemies at 1 HP", n);
    g_kill_req = false;
  }
  if (!event) played++;
  if (skip > 0 && played >= (uint32_t)skip * 60) {
    played = 0;
    g_skip_req = true;
    REXKRNL_INFO("story4p: auto clear after {} s", skip);
  }
}

// Last frame each player was seen (diagnostics: a player that vanishes outside of an event).
struct Seen { uint32_t ch, slot; int hp, x, st; bool brain; } g_seen[4];

void ReportLost(uint8_t* base, int p) {
  const Seen& s = g_seen[p];
  const uint32_t now = Chara(base, s.slot);
  std::string what = "slot empty";
  if (now)
    what = fmt::format("slot holds {:08X} kind {} dc {} e0 {} hp {} st {:02x}", now, R8(base, now + 0x54),
                       R8(base, now + 0xDC), S8(base, now + 0xE0), (int16_t)R16(base, now + 0xCA),
                       R8(base, now + 0xDF));
  REXKRNL_WARN("story4p: P{} lost (event {}): last {:08X} slot {} hp {} x {} st {:02x}{} -> {}", p + 1,
               R8(base, kWork + 0x3b4), s.ch, s.slot, s.hp, s.x, s.st, s.brain ? " ai" : "", what);
}

void UpdateAi(Guest& g) {
  uint8_t* base = g.base;
  for (int p = 0; p < 4; p++) {
    uint32_t slot = 0;
    uint32_t ch = FindPlayer(base, p, &slot);
    if (!ch) {
      if (g_seen[p].ch) ReportLost(base, p);
      g_seen[p].ch = 0;
      g_brain[p] = 0;
      continue;
    }
    g_seen[p] = {ch, slot, (int16_t)R16(base, ch + 0xCA), (int32_t)R32(base, ch + 0xC) >> 16,
                 R8(base, ch + 0xDF), Deref(base, ch + 0x174) != 0};
    uint32_t brain = Deref(base, ch + 0x174);
    // HP (+0xca) below zero = down. The game's continue (sub_824E4308) skips characters with a
    // brain, and a downed character with a brain is removed like an NPC: hand it back to its pad
    // until the continue has revived it.
    const bool down = R16(base, ch + 0xCA) > 0x7fff;
    if (down) {
      if (brain && brain == g_brain[p]) {
        g(sub_82479E60, ch, 0);
        REXKRNL_INFO("story4p: AI P{} down, brain released for the continue", p + 1);
      }
      g_brain[p] = 0;
      g_retry[p] = 30;
      continue;
    }
    if (g_ai[p]) {
      if (brain) continue;  // ours, or a story script is driving this character
      if (g_retry[p] > 0) { g_retry[p]--; continue; }
      const int kind = R8(base, ch + 0x54);
      const char* forced = Env("GH_AI_CODE");
      const int code = forced ? (int)std::strtol(forced, nullptr, 0) : BrainFor(kind);
      g(sub_82479E60, ch, (uint64_t)code);
      g_brain[p] = Deref(base, ch + 0x174);
      g_retry[p] = 30;  // if the brain ends itself, try again in half a second
      REXKRNL_INFO("story4p: AI P{} kind {} brain {:#x} -> {:08X}", p + 1, kind, code, g_brain[p]);
    } else if (brain && brain == g_brain[p]) {
      g(sub_82479E60, ch, 0);
      g_brain[p] = 0;
      REXKRNL_INFO("story4p: AI P{} released", p + 1);
    }
  }
}

void LogPlayers(uint8_t* base) {
  static int every = -1;
  if (every < 0) { const char* s = Env("GH_4P_LOG"); every = s ? std::atoi(s) : 0; }
  if (every <= 0 || g_frame % (uint32_t)(every * 60) != 0) return;
  const int cx = (int32_t)R32(base, kCamera + 0x304) >> 16;
  std::string line = fmt::format("story4p: f{} stage {} cam {} event {}", g_frame,
                                 (int)R8(base, kWork + 0x3af), cx, (int)R8(base, kWork + 0x3b4));
  for (int p = 0; p < 4; p++) {
    uint32_t slot = 0;
    uint32_t ch = FindPlayer(base, p, &slot);
    if (!ch) continue;
    line += fmt::format(" | P{} k{} s{} x{} hp{} st{:02x}{}", p + 1, R8(base, ch + 0x54), slot,
                        (int32_t)R32(base, ch + 0xC) >> 16, (int16_t)R16(base, ch + 0xCA),
                        R8(base, ch + 0xDF), Deref(base, ch + 0x174) ? " ai" : "");
  }
  REXKRNL_INFO("{}", line);
}

}  // namespace

// Party creation: P3/P4 (party 3/4) are created after the original P1/P2.
REX_HOOK_RAW(sub_824E2970) {
  if (!Enabled()) { __imp__sub_824E2970(ctx, base); return; }
  Unpark(base);
  if (Env("GH_4P_AUTO") && S8(base, kKinds + 2) >= 0) {  // test mode: P3/P4 without joining
    for (int p = 2; p < MaxPlayers(); p++) {
      if (S8(base, KindOf(p)) >= 0) continue;
      W8(base, KindOf(p), (uint8_t)AutoKind(base, p));
      // Fresh block with P1's level/EXP (attribute points belong to P1's hero).
      const uint32_t blk = BlockOf(p), p1 = kWork + 1 * kBlock;
      std::memset(base + blk, 0, kBlock);
      W32(base, blk + 0, R32(base, p1 + 0));
      W16(base, blk + 0xc, R16(base, p1 + 0xc));
      W8(base, blk + 0xe, R8(base, p1 + 0xe));
    }
  }
  PPCContext saved = ctx;
  __imp__sub_824E2970(ctx, base);
  for (auto& k : g_kind) k = -1;
  Guest g(saved, base);
  for (int p = 2; p < MaxPlayers(); p++)
    if (S8(base, KindOf(p)) >= 0) SpawnExtra(g, p);
}

// Stage "P2 joins" check (stage state 4): with P2 present, a free pad pressing START joins as P3/P4.
REX_HOOK_RAW(sub_824E3618) {
  const uint32_t stage = ctx.r3.u32;
  if (Enabled() && g_skip_req && R8(base, kWork + 0x3b4) == 0) {
    // The play state (sub_824E4918) ends the stage when the scripts put a negative end code in
    // work+0x3b1; it then saves the party and the story module loads the next stage.
    g_skip_req = false;
    W8(base, kWork + 0x3b1, 0xFF);
    REXKRNL_INFO("story4p: stage {} cleared by debug command", (int)R8(base, kWork + 0x3af));
  }
  // The original join needs stage+0x172ac ("P2 seat open and no event"), which stays 0 while P2
  // plays. Use work+0x3b4 instead (1 while an event/dialogue runs): joining during an event leaves
  // the stage stuck behind it.
  if (Enabled() && R8(base, kWork + 0x3b4) == 0 && S8(base, kPadKind + 1) >= 0 &&
      S8(base, kKinds + 2) >= 0) {
    int player = -1;
    for (int p = 2; p < MaxPlayers(); p++)
      if (S8(base, KindOf(p)) < 0) { player = p; break; }
    if (player >= 0) {
      Guest g(ctx, base);
      if (!(g(sub_82421B40) & 0xff)) {
        for (int pad = 0; pad < 4; pad++) {
          bool taken = false;
          for (int i = 0; i < 4; i++) taken |= S8(base, kPadKind + i) == pad;
          if (taken || !(g(sub_82419C38, pad) & 1)) continue;
          if (!(R32(base, g(sub_8241B800, pad)) & 0x400000)) continue;
          REXKRNL_INFO("story4p: pad {} pressed START, joining as P{}", pad, player + 1);
          Park(base, player, pad);
          g(sub_82496728, kGlobalWork, 1, pad);
          W8(base, kGlobalWork + 0x2c, 0);
          W16(base, stage + 0xe, (g(sub_8242B528) & 0xff) ? 8 : 9);
          ctx.r3.u64 = 0;
          return;
        }
      }
    }
  }
  __imp__sub_824E3618(ctx, base);
}

// Parameter screen hero cycling (sub_82500658(screen, index, next)): skip heroes in use.
REX_HOOK_RAW(sub_82500658) {
  if (!Enabled() || !g_join.active) { __imp__sub_82500658(ctx, base); return; }
  const PPCContext in = ctx;
  __imp__sub_82500658(ctx, base);
  for (int i = 0; i < 5 && PanelTaken(base, ctx.r3.s32); i++) {
    PPCContext c = in;
    c.r4.s64 = ctx.r3.s32;
    __imp__sub_82500658(c, base);
    ctx.r3 = c.r3;
  }
}

// Continue (sub_824E4308(stage), every frame): a downed P1/P2 spends a credit and gets up. It only
// knows party 1/2 (registry slots 1/2, state words stage+0x171b8/bc, timers +0x171c0/c4), so it is
// run a second time with P3/P4 swapped into those places. With no credit left the original ends the
// game; P3/P4 just stay down then.
uint32_t g_cont[2][2];  // P3/P4: continue state, timer

REX_HOOK_RAW(sub_824E4308) {
  const uint32_t stage = ctx.r3.u32;
  PPCContext saved = ctx;
  __imp__sub_824E4308(ctx, base);
  if (!Enabled() || R8(base, stage + 0x171c8) || R8(base, kWork + 0x3ae) == 0) return;
  bool any = false;
  for (int p = 2; p < 4; p++) any |= S8(base, KindOf(p)) >= 0;
  if (!any) return;

  auto swap_bytes = [&](uint32_t a, uint32_t b, uint32_t n) {
    uint8_t t[8];
    std::memcpy(t, base + a, n);
    std::memmove(base + a, base + b, n);
    std::memcpy(base + b, t, n);
  };
  auto swap_all = [&]() {
    for (int i = 0; i < 2; i++) {
      const int p = i + 2;
      swap_bytes(kWork + (1 + i + 0xd1) * 8, kWork + (SlotFor(p) + 0xd1) * 8, 8);
      swap_bytes(kKinds + 1 + i, KindOf(p), 1);
      uint32_t st = R32(base, stage + 0x171b8 + i * 4), tm = R32(base, stage + 0x171c0 + i * 4);
      W32(base, stage + 0x171b8 + i * 4, g_cont[i][0]);
      W32(base, stage + 0x171c0 + i * 4, g_cont[i][1]);
      g_cont[i][0] = st;
      g_cont[i][1] = tm;
    }
  };
  swap_all();
  ctx = saved;
  __imp__sub_824E4308(ctx, base);
  swap_all();
}

// Pad read (sub_825CF040(pad, state), XINPUT_STATE with wButtons at +4). When every player in the
// game runs under the AI (pure autoplay), tap A on their pads every 40 polls so dialogues, the
// parameter screens and the route choice go on by themselves. In combat the brains ignore the pad.
REX_HOOK_RAW(sub_825CF040) {
  const uint32_t pad = ctx.r3.u32, state = ctx.r4.u32;
  __imp__sub_825CF040(ctx, base);
  if (!Enabled() || ctx.r3.u32 != 0 || pad > 3) return;
  InitAi();
  static uint32_t polls;
  if (pad == 0) polls++;
  bool everyone = true, mine = false;
  for (int p = 0; p < 4; p++) {
    const int8_t k = S8(base, kPadKind + p);
    if (k < 0) continue;
    everyone &= g_ai[p];
    mine |= k == (int8_t)pad && g_ai[p];
  }
  const char* menus = Env("GH_AI_MENUS");  // GH_AI_MENUS=0 turns the tapping off
  if (everyone && mine && !(menus && *menus == '0') && polls % 40 < 3)
    W16(base, state + 4, R16(base, state + 4) | 0x1000);  // XINPUT_GAMEPAD_A
}

// End of stage / checkpoint: keep P3/P4 level and EXP in their party blocks.
REX_HOOK_RAW(sub_82499F40) {
  if (Enabled() && (ctx.r4.u32 & 0xff)) {
    for (int p = 2; p < 4; p++) {
      if (S8(base, KindOf(p)) < 0) continue;
      uint32_t ch = Chara(base, SlotFor(p));
      if (!ch || R8(base, ch + 0xDC) != 1) continue;
      const uint32_t blk = BlockOf(p);
      W32(base, blk + 0, R32(base, ch + 0xC0));
      W16(base, blk + 0xc, R16(base, ch + 0xD6));
      W8(base, blk + 0xe, R8(base, ch + 0xE1));
    }
  }
  __imp__sub_82499F40(ctx, base);
}

// Stage scripts only move the hero slots 1..5 in cut scenes; P3/P4 stay where they spawned (often
// far off screen). When an event ends, an off-screen P3/P4 is put next to P1.
static void BringExtrasAfterEvent(uint8_t* base) {
  static uint8_t prev;
  const uint8_t ev = R8(base, kWork + 0x3b4);
  const bool ended = prev && !ev;
  prev = ev;
  if (!ended || !Story(base)) return;
  const uint32_t p1 = Chara(base, 1);
  if (!p1) return;
  const int32_t wl = (int32_t)R32(base, kCamera + 0x318), wr = (int32_t)R32(base, kCamera + 0x31C);
  for (int p = 2; p < 4; p++) {
    const uint32_t ch = Chara(base, SlotFor(p));
    if (!ch || R8(base, ch + 0xDC) != 1) continue;
    const int32_t x = (int32_t)R32(base, ch + 0xC);
    if (x >= wl && x <= wr) continue;
    int32_t nx = (int32_t)R32(base, p1 + 0xC) + (p == 2 ? -0x300000 : 0x300000);
    if (nx < wl + 0x200000) nx = wl + 0x200000;
    if (nx > wr - 0x200000) nx = wr - 0x200000;
    W32(base, ch + 0xC, (uint32_t)nx);
    W32(base, ch + 0x10, R32(base, p1 + 0x10) + (p == 2 ? 0x100000 : (uint32_t)-0x100000));
    W32(base, ch + 0x14, R32(base, p1 + 0x14));
    W8(base, ch + 0xEF, R8(base, ch + 0xEF) | 0x40);
    REXKRNL_INFO("story4p: P{} brought next to P1 after the event (x {} -> {})", p + 1, x >> 16, nx >> 16);
  }
}

// Once per frame while a stage runs: AI toggles and the optional status log.
REX_HOOK_RAW(sub_82498808) {
  if (Enabled()) {
    InitAi();
    PollKeys();
    g_frame++;
    BringExtrasAfterEvent(base);
    // Infinite credits (work+0x3ae): kept at 9; the HUD counter is hidden (sub_8247FE88).
    if (InfiniteCredits() && Story(base) && R8(base, kWork + 0x3ae) < 9) W8(base, kWork + 0x3ae, 9);
    Guest g(ctx, base);
    UpdateAi(g);
    AutoDebug(base);
    LogPlayers(base);
  }
  __imp__sub_82498808(ctx, base);
}

// The story keeps one registry slot per hero (kinds 1..5) in stage+0x1719c+(kind-1)*4: the party
// member's slot when that hero is played, else a free slot 1..5 where the story spawns the hero as an
// NPC. Opt-in (GH_4P_HEROSLOT=1) pointing the heroes of P3/P4 at their slots: the story scripts then
// drive that character in cut scenes (seen: stage 1 turned P4's Ginjirou into an NPC).
REX_HOOK_RAW(sub_824E2AC0) {
  const uint32_t stage = ctx.r3.u32;
  __imp__sub_824E2AC0(ctx, base);
  const char* hs = Env("GH_4P_HEROSLOT");
  if (!Enabled() || !(hs && *hs == '1')) return;
  for (int p = 2; p < 4; p++) {
    int k = g_kind[p];
    if (k == 7) k = 2;
    if (k >= 1 && k <= 5) {
      W32(base, stage + 0x1719c + (k - 1) * 4, SlotFor(p));
      REXKRNL_INFO("story4p: hero {} -> slot {} (P{})", k, SlotFor(p), p + 1);
    }
  }
}

// ---------------------------------------------------------------------------------------------
// HUD: the four player panels on the top row, credits hidden
// ---------------------------------------------------------------------------------------------
// sub_8249D2C0 lays out the HUD panels every frame. In story mode the panels of registry slots 1/2
// (P1/P2) sit at fixed spots on the top row (x 410/688, y 32; "Credit(s)" on the right); every
// other big panel (P3/P4 in slots 30/31, the story heroes) flows on the second row from x 140. With
// three or more players, the four player panels share the top row, using the room of the credits.
// To keep P3/P4 out of the flow, their character's slot (+0xda) reads 1 for the layout test made
// right after sub_8249CC40 returns; the next panel update (or the end of the layout) restores it.
namespace {

int PartyPlayers(uint8_t* base) {
  int n = 0;
  for (uint32_t slot : {1u, 2u, SlotFor(2), SlotFor(3)}) n += S8(base, kKinds + slot) >= 0;
  return n;
}

uint32_t g_da_ch;
uint8_t g_da_old;
void RestoreDa(uint8_t* base) {
  if (g_da_ch) { W8(base, g_da_ch + 0xDA, g_da_old); g_da_ch = 0; }
}

}  // namespace

REX_HOOK_RAW(sub_8249CC40) {
  const uint32_t panel = ctx.r3.u32;
  RestoreDa(base);
  __imp__sub_8249CC40(ctx, base);
  if (!Enabled() || !Story(base) || R8(base, panel + 0x8a4) || R32(base, panel + 0x89c) != 0) return;
  const int n = PartyPlayers(base);
  if (n < 3) return;
  const uint32_t ch = Deref(base, panel + 0x18);
  if (!ch || R8(base, ch + 0xDC) != 1) return;
  const int p = S8(base, ch + 0xE0);
  if (p < 0 || p > 3) return;
  // Panel = portrait + bars, ~225 px wide; same 278 px pitch as P1/P2, centred on the 1280 screen.
  static const float x4[4] = {110, 388, 666, 944}, x3[4] = {250, 528, 806, 944};
  WF(base, panel + 0x888, n >= 4 ? x4[p] : x3[p]);
  WF(base, panel + 0x88c, 32);
  const uint8_t da = R8(base, ch + 0xDA);
  if (da == 0 || da > 2) {
    g_da_ch = ch;
    g_da_old = da;
    W8(base, ch + 0xDA, 1);
  }
}

REX_HOOK_RAW(sub_8249D2C0) {
  __imp__sub_8249D2C0(ctx, base);
  RestoreDa(base);
}

// "Credit(s) N" (up to 3 quads counted in +0x130): nothing to draw while credits are infinite.
REX_HOOK_RAW(sub_8247FE88) {
  const uint32_t obj = ctx.r3.u32;
  __imp__sub_8247FE88(ctx, base);
  if (Enabled() && InfiniteCredits() && Story(base)) W32(base, obj + 0x130, 0);
}

// ---------------------------------------------------------------------------------------------
// Pause: status windows for P3/P4
// ---------------------------------------------------------------------------------------------
// The pause menu (stage+0xa40, constructor sub_824E0C10) owns two status windows (vtable
// 0x8201d928, 0x6f0 bytes) at +0x12f0c (P1) and +0x135fc (P2), set up by sub_824B57E8(window,
// index). A window shows party index+1 and the character in registry slot index+1, centred on
// x = +0x268. Two more windows (index 2/3) are created for P3/P4; they follow every open/close of
// the P2 window and are updated/drawn after the menu's own, with registry slot 3/4 swapped with
// 30/31 for the call. With 3+ players the windows go P1 P2 | menu | P3 P4.
namespace {

constexpr uint32_t kStatusVtable = 0x8201d928;
constexpr uint32_t kStatusSize = 0x6f0;
uint32_t g_pause;
uint32_t g_extra[2];

void SwapRegistry(uint8_t* base, int i) {  // i = 0 (P3) / 1 (P4)
  const uint32_t a = kWork + (3 + i + 0xd1) * 8, b = kWork + (SlotFor(2 + i) + 0xd1) * 8;
  uint8_t t[8];
  std::memcpy(t, base + a, 8);
  std::memcpy(base + a, base + b, 8);
  std::memcpy(base + b, t, 8);
}

void CallExtras(Guest& g, PPCFunc* fn, bool swap) {
  for (int i = 0; i < 2; i++) {
    if (!g_extra[i]) continue;
    // The window reads kind work+0x3d8+(index+1) and the character in slot index+1.
    const uint8_t k = R8(g.base, kKinds + 3 + i);
    if (swap) { SwapRegistry(g.base, i); W8(g.base, kKinds + 3 + i, R8(g.base, KindOf(2 + i))); }
    g(fn, g_extra[i]);
    if (swap) { SwapRegistry(g.base, i); W8(g.base, kKinds + 3 + i, k); }
  }
}

bool IsP2Window(uint32_t w) { return Enabled() && g_pause && w == g_pause + 0x135fc; }

void PlacePauseWindows(uint8_t* base, uint32_t pause) {
  if (PartyPlayers(base) < 3) return;
  WF(base, pause + 0x12f0c + 0x268, 167);
  WF(base, pause + 0x135fc + 0x268, 408);
  if (g_extra[0]) WF(base, g_extra[0] + 0x268, 872);
  if (g_extra[1]) WF(base, g_extra[1] + 0x268, 1113);
}

}  // namespace

REX_HOOK_RAW(sub_824E0C10) {
  const uint32_t pause = ctx.r3.u32;
  const PPCContext saved = ctx;
  __imp__sub_824E0C10(ctx, base);
  if (!Enabled() || !Story(base)) return;
  Guest g(const_cast<PPCContext&>(saved), base);
  for (auto& w : g_extra) {
    if (w) g(sub_825122F0, w, 1);  // the previous stage's pause is gone
    w = 0;
  }
  g_pause = pause;
  for (int i = 0; i < 2; i++) {
    const uint32_t w = g(sub_826A3900, kStatusSize);
    if (!w) continue;
    g(sub_824B2ED0, w);
    W32(base, w, kStatusVtable);
    g(sub_824B57E8, w, (uint64_t)(2 + i));
    g_extra[i] = w;
  }
  REXKRNL_INFO("story4p: pause {:08X}, P3/P4 status windows {:08X} {:08X}", pause, g_extra[0], g_extra[1]);
}

REX_HOOK_RAW(sub_824DEA90) {
  const uint32_t pause = ctx.r3.u32;
  const bool mine = Enabled() && pause == g_pause && Story(base) && R8(base, pause) != 0;
  if (mine) PlacePauseWindows(base, pause);
  const PPCContext saved = ctx;
  __imp__sub_824DEA90(ctx, base);
  if (!mine) return;
  Guest g(const_cast<PPCContext&>(saved), base);
  CallExtras(g, sub_824A5218, true);
}

REX_HOOK_RAW(sub_824E01D0) {
  const uint32_t pause = ctx.r3.u32;
  const bool mine = Enabled() && pause == g_pause && Story(base) && (int16_t)R16(base, pause + 0x9c8) >= 2;
  const PPCContext saved = ctx;
  __imp__sub_824E01D0(ctx, base);
  if (!mine) return;
  Guest g(const_cast<PPCContext&>(saved), base);
  CallExtras(g, sub_824AFC38, true);
}

REX_HOOK_RAW(sub_824A6C40) {
  const uint32_t w = ctx.r3.u32;
  const PPCContext saved = ctx;
  __imp__sub_824A6C40(ctx, base);
  if (!IsP2Window(w)) return;
  Guest g(const_cast<PPCContext&>(saved), base);
  CallExtras(g, __imp__sub_824A6C40, false);
}

REX_HOOK_RAW(sub_824A5158) {
  const uint32_t w = ctx.r3.u32;
  const PPCContext saved = ctx;
  __imp__sub_824A5158(ctx, base);
  if (!IsP2Window(w)) return;
  Guest g(const_cast<PPCContext&>(saved), base);
  CallExtras(g, __imp__sub_824A5158, false);
}

REX_HOOK_RAW(sub_824A5188) {
  const uint32_t w = ctx.r3.u32;
  const PPCContext saved = ctx;
  __imp__sub_824A5188(ctx, base);
  if (!IsP2Window(w)) return;
  Guest g(const_cast<PPCContext&>(saved), base);
  CallExtras(g, __imp__sub_824A5188, false);
}

// ---------------------------------------------------------------------------------------------
// P3/P4 party blocks and kinds where the game expects party index == registry slot
// ---------------------------------------------------------------------------------------------
namespace {

// work+slot*0x24 for slots 30/31 lands on other per-slot arrays (work+0x438.., 0x47a..). While the
// game reads/writes "the block of slot 30/31", that place temporarily holds the P3/P4 block.
struct Shadow {
  uint8_t* base = nullptr;
  uint32_t region = 0, blk = 0;
  uint8_t saved[kBlock];
  Shadow(uint8_t* b, uint32_t ch) {
    if (!Enabled() || !ch) return;
    const uint32_t slot = R8(b, ch + 0xDA);
    const int p = slot == SlotFor(2) ? 2 : (slot == SlotFor(3) ? 3 : -1);
    if (p < 0) return;
    base = b;
    region = kWork + slot * kBlock;
    blk = BlockOf(p);
    std::memcpy(saved, b + region, kBlock);
    std::memcpy(b + region, b + blk, kBlock);
  }
  ~Shadow() {
    if (!base) return;
    std::memcpy(base + blk, base + region, kBlock);
    std::memcpy(base + region, saved, kBlock);
  }
};

// Kinds of P3/P4 copied into kind entries a/b for one call (restored after).
struct Expose {
  uint8_t* base = nullptr;
  uint32_t at[2];
  uint8_t old[2];
  Expose(uint8_t* b, uint32_t a0, uint32_t a1) {
    if (!Enabled() || !Story(b)) return;
    base = b;
    at[0] = a0; at[1] = a1;
    for (int i = 0; i < 2; i++) {
      old[i] = R8(b, kKinds + at[i]);
      W8(b, kKinds + at[i], R8(b, KindOf(2 + i)));
    }
  }
  ~Expose() {
    if (base)
      for (int i = 0; i < 2; i++) W8(base, kKinds + at[i], old[i]);
  }
};

}  // namespace

REX_HOOK_RAW(sub_82468FA8) { Shadow s(base, ctx.r3.u32); __imp__sub_82468FA8(ctx, base); }
REX_HOOK_RAW(sub_82468E90) { Shadow s(base, ctx.r3.u32); __imp__sub_82468E90(ctx, base); }
REX_HOOK_RAW(sub_82512B90) { Shadow s(base, ctx.r3.u32); __imp__sub_82512B90(ctx, base); }

// Player index p -> party p+1 (Repertoire etc.): P3/P4 read entries 3/4.
REX_HOOK_RAW(sub_824C0470) { Expose e(base, 3, 4); __imp__sub_824C0470(ctx, base); }
// Asset preload loops only look at entries 0..11; 10/11 are never hero kinds.
REX_HOOK_RAW(sub_8243E510) { Expose e(base, 10, 11); __imp__sub_8243E510(ctx, base); }
REX_HOOK_RAW(sub_824EB8D0) { Expose e(base, 10, 11); __imp__sub_824EB8D0(ctx, base); }

// Debug (GH_4P_LOGCREATE=1): every character creation (slot, kind, level, brain code).
REX_HOOK_RAW(sub_8247A530) {
  const uint32_t d = ctx.r3.u32;
  static int on = -1;
  if (on < 0) on = Env("GH_4P_LOGCREATE") ? 1 : 0;
  if (on) {
    REXKRNL_INFO("story4p: create slot {} kind {} lvl {} brain {:#x} stage {} event {}", S8(base, d), R8(base, d + 2),
                 R8(base, d + 6), R16(base, d + 8), R8(base, kWork + 0x3af), R8(base, kWork + 0x3b4));
    // Resource manager 0x829CC79C: +0x18/+0x1c vector of packs (+0 data, +4 std::string name, +0x20 purge).
    constexpr uint32_t kRes = 0x829CC79C;
    std::string packs;
    for (uint32_t it = R32(base, kRes + 0x18); it && it < R32(base, kRes + 0x1c); it += 4) {
      const uint32_t e = R32(base, it);
      uint32_t str = e + 4;
      if (R32(base, e + 0x18) > 0xf) str = R32(base, str);
      packs += fmt::format(" {}{}{}", (const char*)(base + str), R32(base, e) ? "" : "(-)", R8(base, e + 0x20) ? "*" : "");
    }
    REXKRNL_INFO("story4p:   packs:{}", packs);
  }
  __imp__sub_8247A530(ctx, base);
}

// Debug: failed file-buffer allocations (the async loader retries them every frame).
REX_HOOK_RAW(sub_82418928) {
  const uint32_t size = ctx.r3.u32, type = ctx.r4.u32;
  __imp__sub_82418928(ctx, base);
  static uint32_t fails;
  if (ctx.r3.u32 == 0 && (fails++ % 300) == 0)
    REXKRNL_WARN("story4p: file buffer alloc failed: {:#x} bytes (type {}), {} failures", size, type, fails);
}

