#pragma once
#include "../headers.h"

// =============================================================================
//  A character's look: the colour of their hair, their skin and their clothes.
//  Chosen between the calling and the first step (Game::UpdateCharacterLooks),
//  kept with the character, and told to friends in their Outfit.
//
//  The art is not repainted. Every character sheet is cel-shaded -- each
//  material rendered in exactly three flat colours, shade, mid and light, from
//  its base colour by the ramp in tools/blender_character.py -- so a pixel in
//  one of the hair's three colours is hair, and becomes the same band of the
//  new hair colour, which is what Blender would have rendered had the hair
//  been that colour. The one-pixel outline beside it was drawn as a share of
//  the colours it borders, and moves by that share of their change. What each
//  rig is made of, and the ramp, come from its palette.json, which
//  tools/character_palettes.py writes from the Blender script's own numbers.
// =============================================================================

enum LookPart { LOOK_HAIR, LOOK_SKIN, LOOK_CLOTHES, LOOK_PARTS };

const char* LookPartName(int part);   // "Hair", "Skin", "Clothes"
const char* LookPartKey(int part);    // "hair", "skin", "clothes": the save's and the palette's words

struct Looks {
    // 0xRRGGBB, or -1 for the calling's own colour.
    int32_t part[LOOK_PARTS] = {-1, -1, -1};

    bool Any() const;
    bool Own(int p) const { return part[p] < 0; }
    // A name for the textures drawn in these colours: the same looks, the same key.
    string Key() const;
    // {"hair": "#e8cc78", ...}, the calling's own left out.
    json ToJson() const;
    static Looks FromJson(const json& j);
    bool operator==(const Looks& o) const;
    bool operator!=(const Looks& o) const { return !(*this == o); }
};

// The colours offered for each part, in the order they are shown. The
// calling's own is offered before them all, and is not in the list.
struct LookSwatch {
    const char* name;
    uint32_t rgb;
};
const vector<LookSwatch>& LookSwatches(int part);
// Where a look's colour is in the part's list: 0 is the calling's own, 1 the
// first swatch. A colour that is not a swatch (an old save, a friend's newer
// build) counts as the calling's own for stepping from.
int LookSwatchIndex(const Looks& looks, int part);
void SetLookSwatch(Looks& looks, int part, int index);

// What one rig's sheets are drawn in, and how to draw them in other colours.
class DyeTable {
public:
    bool Load(const string& path);
    bool Loaded() const { return !materials.empty(); }
    // The calling's own colour for a part -- its first material's -- as 0xRRGGBB.
    uint32_t Own(int part) const;
    // Recolours an RGBA32 surface in place: the parts the looks name, and the
    // outline beside them. False when nothing in it changed.
    bool Apply(SDL_Surface* s, const Looks& looks) const;
    // Of the surface's opaque pixels, how many are one of the rig's colours,
    // and how many of each part's (per_part, LOOK_PARTS long, may be null).
    // The self-test's proof that the palette still matches the art.
    void Count(SDL_Surface* s, int& opaque, int& known, int* per_part = nullptr) const;
    // Which part a colour is drawn as -- LOOK_HAIR and the rest -- or -1 for
    // one of the rig's other colours, -2 for none of them (the outline).
    int PartOf(Uint8 r, Uint8 g, Uint8 b) const;

private:
    struct Material {
        string name;
        float  base[3] = {0, 0, 0};    // sRGB, 0..1
        bool   flat = false;            // emitted as it is, not through the ramp
        int    part = -1;               // the part that recolours it, or -1
        bool   first = false;           // the part's own colour: the one chosen is this
        // Its colour over its part's first material's, channel by channel: the
        // trim follows the tunic at this distance, so a red tunic has a darker
        // red trim, and the tunic's own colour chosen leaves the trim as drawn.
        float  ratio[3] = {1.0f, 1.0f, 1.0f};
    };
    struct Hit { uint8_t material = 0, band = 0, distance = 0; };

    // A band of a base colour, as tools/blender_character.py's material() makes it.
    SDL_Color Band(const float base[3], int band) const;
    // The colour a material is drawn in under these looks, or false for its own.
    bool Target(const Material& m, const Looks& looks, float out[3]) const;
    const Hit* Find(Uint8 r, Uint8 g, Uint8 b) const;

    vector<Material> materials;
    float bands[3] = {0.64f, 0.86f, 1.0f};
    float tint[3] = {0.36f, 0.33f, 0.52f};
    float tint_amount[3] = {0.22f, 0.06f, 0.0f};
    float outline = 0.42f;
    // Every colour within a couple of steps of one of the rig's bands, to the
    // nearest: an 8-bit render lands a step or two from the exact value.
    unordered_map<uint32_t, Hit> hits;
};
