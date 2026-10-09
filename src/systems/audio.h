#pragma once
#include "../headers.h"

// ---------------------------------------------------------------------------
//  Sound
//
//  There are no sound files. Every effect is synthesised once at start-up from
//  tones, noise, struck metal and a bowstring's thrum, and the ambience -- wind, birdsong, cave
//  drips, a crackling hearth -- is generated live by the mixer, so no two
//  minutes in the Whisperwood sound quite the same.
//
//  Everything here is a free function so the world, the player and the menus
//  can make a noise without a pointer threaded through to them. Until Init()
//  or InitOffline() succeeds, every call is a harmless no-op.
// ---------------------------------------------------------------------------

enum class Sfx {
    Swing, SwingHeavy, Hit, HitCrit, Block, EnemyDie, PlayerHurt, PlayerDie,
    BowShot, KnifeThrow, SpellCast, Impact,
    Pickup, Coins, Chop, Mine, Cook, Burn, ChestOpen, Eat, Equip,
    Footstep, FootstepWood, FootstepStone, Jump, Land,
    Door, Portal, Locked, Winded, Sleep, Wake, Splash,
    UiMove, UiConfirm, UiBack, UiError,
    LevelUp, QuestStart, QuestComplete,
    // Appended, never inserted: the numbers are what co-op sends (Delta::Sound),
    // and everything from UiMove to QuestComplete is taken for a guest's own
    // sound and never sent to her.
    Throw, KnifeHit, Whiff,
    // A dragon's: its breath going out, its roar into a heavy blow, its bite.
    Breath, Roar, Bite,
    // The prologue's: an Echo -- a change carrying from the Reverie into the
    // waking world -- a suit of armour grinding awake, the stranger going to
    // smoke, and thunder over his house.
    Echo, Grind, Vanish, Thunder,
    // Act I's: a Dawn Bell's toll, the Dawn Chimes, the anvil that rings Halda
    // awake, the wolves' last howl, a laugh from nowhere, the Reverie torn
    // open, a fist bumped, a barrier of thread breaking, and a gust of wind.
    Bell, Chime, Anvil, Howl, Laugh, Tear, Bump, Shatter, Gust,
    // Fishing's: the bobber pulled under, the reel's ratchet, a line snapping.
    Plop, Reel, Snap,
    // The clothier's: a pair of shears snipping -- a thread cut, a dream's
    // black thread parted, the Shear Mannequin's blades shutting.
    Snip,
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
// "menu" is the title screen's theme, from its start each time it is set.
void SetAmbience(const string& kind, bool interior);
// The music a story's scene asks for: a cue by name -- "ominous", "town",
// "montage", "hum", "dream", "escape", and Act I's "dream_town", "boss" and
// "trap" -- faded to over `fade` seconds; ""
// fades out whatever is playing. The menu's theme is the menu's own.
void Music(const string& cue, float fade = 1.5f);
const string& MusicCue();
// A cue as the mixer would play it, made now: stereo, interleaved, a loop from
// its first frame to its last. Empty for a name that is no cue. For the self-test.
vector<float> MakeMusic(const string& cue);
// 0 by day, 1 at night: outdoors the birds fall quiet as it rises and the
// crickets start.
void SetNight(float amount);
// A shower: how hard it is coming down where the listener is (0 for none),
// and whether they hear it through a roof. See Weather.
void SetWeather(float rain, bool indoors);
// What is in earshot, 0..1 each: water (it laps, and after dark the frogs
// call), lava (a rumble, and bubbles breaking); and whether this is the woods
// (an owl at night, insects in the warm dark).
void SetNearby(float water, float lava, bool woods);
void SetVolumes(float master, float sfx, float ambience);

// Offline inspection, used by the self-test.
const vector<float>& Samples(Sfx s);
// The menu's theme: stereo samples, interleaved, and the frame it loops to.
const vector<float>& MenuTheme();
size_t MenuThemeLoop();
void Mix(float* stereo, int frames);
int  ActiveVoices();

}
