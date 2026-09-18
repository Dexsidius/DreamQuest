#pragma once
#include "../headers.h"

// ---------------------------------------------------------------------------
//  Sound
//
//  There are no sound files. Every effect is synthesised once at start-up from
//  tones, noise and plucked strings, and the ambience -- wind, birdsong, cave
//  drips, a crackling hearth -- is generated live by the mixer, so no two
//  minutes in the Whisperwood sound quite the same.
//
//  Everything here is a free function so the world, the player and the menus
//  can make a noise without a pointer threaded through to them. Until Init()
//  or InitOffline() succeeds, every call is a harmless no-op.
// ---------------------------------------------------------------------------

enum class Sfx {
    Swing, SwingHeavy, Hit, HitCrit, Block, EnemyDie, PlayerHurt, PlayerDie,
    BowShot, SpellCast, Impact,
    Pickup, Coins, Chop, Mine, Cook, Burn, ChestOpen, Eat, Equip,
    Footstep, FootstepWood, FootstepStone, Jump, Land,
    Door, Portal, Locked, Winded, Sleep, Wake, Splash,
    UiMove, UiConfirm, UiBack, UiError,
    LevelUp, QuestStart, QuestComplete,
    Count
};

namespace Audio {

bool Init();            // opens the default playback device
bool InitOffline();     // builds the sounds with no device, for the self-test
void Shutdown();
bool Enabled();

// Co-op. Every sound asked for is also told to the tap -- whether or not
// there is a device, so the headless server can pass on what it cannot play
// -- with where it was, if it had a where. And the host can be spared what
// is not its to hear: 1 silences sounds with no place (a friend's pickup
// would otherwise ring in the host's ear), 2 silences everything (a map the
// host is not on).
using Tap = std::function<void(Sfx s, bool placed, float x, float y, float volume, float pitch)>;
void SetTap(Tap tap);
void SetMuted(int level);

void Play(Sfx s, float volume = 1.0f, float pitch = 1.0f);
// Quieter and panned the further it is from the listener; silent off-screen.
void PlayAt(Sfx s, float x, float y, float volume = 1.0f, float pitch = 1.0f);
void SetListener(float x, float y);

// "forest", "grove", "town", "overworld", "dungeon", "dream" or "menu"; an
// interior that is not a dungeon gets a hearth. An empty kind fades to silence.
void SetAmbience(const string& kind, bool interior);
// 0 by day, 1 at night: outdoors the birds fall quiet as it rises and the
// crickets start.
void SetNight(float amount);
void SetVolumes(float master, float sfx, float ambience);

// Offline inspection, used by the self-test.
const vector<float>& Samples(Sfx s);
void Mix(float* stereo, int frames);
int  ActiveVoices();

}
