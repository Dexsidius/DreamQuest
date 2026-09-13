# DreamQuest

A top-down adventure RPG in C++ and SDL3, in the Dragon Quest Monsters mould:
a scrolling overworld with several biomes, a village you can walk into and out
of, houses and a guild hall you can enter, mountain mines and a barrow to raid,
Old School RuneScape-style skills, weighted loot tables, three melee attacks
built around a hold-to-charge heavy swing, bows that fire real arrows, a
four-element spell system with an effectiveness triangle, and a soundscape
synthesised entirely in code.

Built on [SDL3-Project-Template](https://github.com/Dexsidius/SDL3-Project-Template),
and the maps are authored in the format exported by
[LevelEdit-Plus](https://github.com/TheSardonicals/LevelEdit-Plus).

![DreamQuest](docs/screenshots.png)

---

## Building

### Windows (MSYS2 UCRT64)

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-sdl3-image mingw-w64-ucrt-x86_64-sdl3-ttf
```

```powershell
.\tools\import_assets.ps1 -GameAssets "E:\Game Assets"   # once, to build assets/
.\build.ps1 -Run
```

### Linux / macOS

Install SDL3, SDL3_image and SDL3_ttf, then:

```bash
./compile_and_run.sh
```

### Other build targets

| Command | What it does |
| --- | --- |
| `.\build.ps1` | Build the game |
| `.\build.ps1 -Run` | Build and launch |
| `.\build.ps1 -Debug` | Unoptimised build with symbols |
| `.\build.ps1 -Test` | Build and run the self-test |
| `.\build.ps1 -Maps` | Regenerate `maps/*.mx` |
| `.\build.ps1 -Tools` | Build `tilecut`, `genmaps` and `selftest` |

`./compile.sh test`, `./compile.sh maps` and `./compile.sh tools` do the same on
Linux.

### Running it outside MSYS2

`bin\DreamQuest.exe` can be double-clicked. Two things make that work, and both
had to be dealt with explicitly:

- **The runtime libraries travel with the exe.** Nothing is on the PATH outside
  an MSYS2 shell, so the build walks the DLL dependency tree with `objdump` and
  copies everything that resolves inside MSYS2 next to the executable — 18
  libraries, because SDL3_ttf pulls in FreeType and HarfBuzz, which between them
  pull in libpng, zlib, bzip2, Brotli, GLib, PCRE2 and Graphite. A hand-written
  list of these was wrong, and the symptom is Windows refusing to start the
  program with no message at all.
- **The game finds its own data.** `data/` and `assets/` are opened by relative
  path, so started from Explorer the working directory is `bin\` and every file
  fails to open. On startup it locates the directory holding `data/sprites.json`
  — beside the exe, then one above it — and moves there, so saves and settings
  land in the project root wherever it was launched from. If it genuinely cannot
  find them it says so in a message box rather than closing silently.

---

## Assets

The art is [CraftPix](https://craftpix.net) free content. That licence permits
using the assets in a game but not redistributing the files, so **no art is
committed here**. `tools/import_assets.ps1` rebuilds `assets/` from the `.zip`
packs you downloaded:

1. Unpacks each pack into `assets/_raw/`
2. Copies the character animation sheets under short, stable names
3. Cuts the flat ground fills out of the packed tilesets with `tilecut`
4. Cuts buildings, decorations and item icons out of the packed sheets
5. Copies the individually-shipped props (trees, rocks, bushes)
6. Regenerates `data/sprites.json` and `data/asset_manifest.json` to match

Everything lands on the exact paths the committed data and maps refer to, so
the game runs as soon as it finishes. The packs used are listed in
[docs/ASSETS.md](docs/ASSETS.md).

---

## Controls

Both schemes are live at once by default; the game switches to whichever you
last touched. Options → Input Device pins it to one if you would rather.

| Action | Keyboard & mouse | Controller |
| --- | --- | --- |
| Move | WASD / arrows | Left stick or d-pad |
| Light attack | `Z` or left mouse | X (west) |
| Strong / charged attack | `X` or right mouse | Y (north) |
| Sprint (hold) | `Shift` | Left trigger |
| Jump / climb | `Space` | Left stick click |
| Interact | `E` | A (south) |
| Inventory | `I` or tab | LB |
| Skills | `K` | RB |
| Quest journal | `Q` | Back |
| Select element | `1` `2` `3` `4` | — |
| Cycle element | `R` | Right stick click |
| Pause / back | `Esc` | Start / B |

### Sprinting

Hold `Shift` (or the left trigger) while moving to sprint at 1.6 times running
speed, for crossing the world rather than for fighting: it cannot start during
a swing or a charge, and taking a hit knocks you out of it for most of a second.
The camera leads a sprint by a few steps so you can see what you are running
into, footfalls come further apart, and each one kicks up dust outdoors. The
hero has a sprint animation of its own; the CraftPix characters, which do not,
play their run faster instead.

### What the attack buttons do

The buttons never change; the weapon in your hand decides what comes out of
them. A sword swings, a bow shoots, a staff casts. All three run through the
same attack state machine, so the charge mechanic works for every style.

### The three attacks

- **Light** — fast, cheap, and it chains. Three hits in a row, each a little
  slower and a little harder than the last.
- **Strong** — tap the heavy button. Slower, hits considerably harder.
- **Charged** — *hold* the heavy button. Past about a fifth of a second the
  swing starts charging and a meter appears under your feet; it turns bright
  when it is full. Release to fire. A full charge is worth roughly three times
  a normal strong hit and reaches further, but it roots you while it winds up.

With a mouse, shots and spells fly toward the cursor. On a controller they
follow the way you are facing.

---

## Projectiles and magic

Arrows and spells are the same system. Everything that separates a bowshot
from a firebolt — speed, reach, how many bodies it passes through, what it
leaves on the ground — is data in `data/projectiles.json`, and the art is one
sprite drawn turned along its direction of travel, so a single arrow image
covers every angle.

### The four elements

You select an **element**, not a spell. Your Magic level decides which tier of
that element actually comes out, so training Magic upgrades what the same
button does instead of adding another thing to remember.

| Element | Signature behaviour | Feel |
| --- | --- | --- |
| **Fire** | Leaves the ground burning where it lands, ticking damage on anything standing in it | Area denial |
| **Water** | Runs straight through a line of bodies | Piercing |
| **Earth** | Lands heavy and bursts a moment later, after a visible wind-up | Slow, high commitment |
| **Air** | Very fast, long range, and it carries what it hits backwards | Kiting |

### The effectiveness cycle

```
Water  →  Fire  →  Earth  →  Air  →  Water
```

Each element beats the next: water douses fire, fire scorches earth, earth
smothers air, air disperses water. Hitting a creature with the element that
beats it does **1.6×** damage and the damage number comes up in that element's
colour with an exclamation mark; being on the wrong end of the cycle does
**0.6×** and reads grey. An element resists itself at **0.75×**, and anything
untyped — most wildlife — takes normal damage from everything, so the matchup
is a reward for paying attention rather than a tax for not.

Monsters are aligned in `data/enemies.json`: orcs and boar are earth, foxes
are air, the Warchief is fire.

### Mana

Casting costs mana, which comes from the Magic level (`12 + level × 2`) and
refills on its own. The bar only appears once you have some, so a pure melee
character is never told about a resource they do not spend.

---

## Skills

Ten skills on the Old School RuneScape XP curve — the real one, so level 92 is
half the experience of 99, and the self-test checks the table against known
values.

| Skill | Trained by |
| --- | --- |
| Attack | Landing light attacks |
| Strength | Landing strong and charged attacks |
| Defence | Taking hits |
| Hitpoints | All damage dealt |
| Woodcutting | Chopping trees in the greenwood |
| Mining | Working ore seams in the foothills and the Mire |
| Cooking | Using a fire with something raw in your pack |
| Crafting | Workbenches in Havenbrook and Mossvale, and the anvil in Halda's forge |
| Ranged | Landing arrows with a bow equipped |
| Magic | Casting spells with a staff equipped |

Combat level uses the OSRS formula across the melee/ranged/magic triangle.

Ranged and Magic read their own level and their own equipment bonus for both
accuracy and damage, exactly as OSRS does, so a bow does nothing for a
character who never trained Ranged and Strength does nothing for a bow. The
self-test checks that.

### Where things are made

Each recipe is made at one station, decided by its materials. **Anything that
needs metal is smithed at an anvil**, the one in Halda's forge in Havenbrook:
the Copper Ring and the Iron Shield. **Everything else is made at a
workbench**, in Havenbrook or Mossvale: the Wooden Shield, the Leather Jerkin
and the Oak Shortbow. The two used to share one list, so a village workbench
could smith an iron shield.

Nothing declares its station. Materials carry `"metal": true` in
`data/items.json` (copper and iron ore), and a recipe with any metal input
belongs at the anvil, so a new recipe cannot be filed in the wrong place. A
crafting object in a map names the station it is with `"station"`; the
self-test checks every recipe against its materials, and that every station in
the world is drawn as what it works as. Each station's screen says what is made
at the other, so a missing recipe reads as elsewhere rather than gone.

---

## Worn equipment

The CraftPix character packs ship their frames already split into layers —
shadow, the weapon behind the body, the body, the head, the weapon in front —
all frame-aligned, with a number in each filename giving the draw order. The
importer keeps that split, so the player is drawn as a paperdoll rather than a
flattened sheet.

- **Weapons are real layers.** What you are holding is drawn from its own
  layers and coloured to match the item, so a bronze sword, a steel longsword,
  a bow and a staff all look different in your hand. An empty hand hides the
  weapon layers entirely.
- **Armour draws, and can also tint.** A worn piece carries a `worn` overlay:
  art drawn on top of the character, positioned by a rectangle given in **frame
  pixels** so it lands on the rig correctly at any camera zoom. A piece with no
  overlay art falls back to colouring the body and head layers instead, so
  plain items still read as armour.

```json
"plumed_helm": {
  "worn": {
    "sprite": "assets/icons/armour/worn/plumed_helm.png",
    "after": "head",
    "rect": [26.2, 21.5, 10.5, 13.0],
    "facings": [true, true, true, true]
  }
}
```

There are nine slots — weapon, shield, head, body, hands, legs, feet, amulet,
ring — drawn from the feet up, so a helmet ends up over a gorget and a gauntlet
over a sleeve.

A bow takes both hands. Equipping one puts the shield in the bag, and taking up
a shield puts the bow away; if the bag has no room for what would come off, the
swap is refused rather than the item being lost. The first playtests had a
character loosing arrows with a shield on the other arm. A weapon that brings its own overlay hides the rig’s built-in
sword layers, and is mirrored when the character faces right so it is not held
backwards.

### On using icon packs as armour

CraftPix icon packs (fantasy knight armour, RPG boots, mage outfits, daggers)
are 512×512 painted inventory art. Three things follow, and they are different
problems:

- **Proportion is solved, by measurement.** The rig was measured rather than
  guessed: in an idle frame the head layer covers x25..38 y22..35, the torso
  x25..38 y32..44, the shadow under the feet y40..47. Each slot gets a box in
  those coordinates, every icon is trimmed to its drawn pixels at import, and
  the art is fitted inside its box the way a picture fits a frame. Sizing by
  height alone would make a broad pair of gauntlets narrower than an arm;
  sizing by width alone would run a tall tasseted skirt up to the chin.
- **Facing is solved, by testing it.** A single painted view means that in
  profile the character wears a front-on breastplate. Restricting armour to the
  facings it "really" reads in was the obvious answer and the wrong one: at
  twenty-odd pixels tall the front-on view does not read as wrong, whereas a
  character who strips naked the moment they walk sideways very much does. So
  everything is worn from every angle.
- **Style is not solved.** A smooth, anti-aliased 512px icon shrunk to 24px
  reads as a soft blob against 16px-grid pixel art. The importer hardens the
  worn copy — alpha cut to on-or-off, colours stepped to eight levels per
  channel — which gives it a defined edge and a flatter palette, and helps a
  great deal. It still will not pass for hand-drawn pixel armour.

So icons are used where they are strongest: full size in the inventory, and as
a worn overlay you can judge for yourself. Proper pixel-art armour layers drop
into the same slots with no code change.

---

## Projectiles and walls

An arrow or a bolt is stepped in slices no longer than half its own radius, so
nothing passes through a wall between frames. When a step would end inside
geometry, `Map::SweepPoint` answers two questions instead of one:

- **Where it actually stopped** — the last position that was clear, so an
  impact is drawn on the surface and a fire patch burns in front of a wall
  rather than half inside it.
- **Which way that wall faces** — worked out by trying each axis on its own,
  the same way `MoveWithCollision` decides which axis to stop a walking
  character on. If moving in X alone is still clear, it was the Y movement that
  hit something, so the surface is horizontal. Both axes blocked means a
  corner.

The normal is what makes a response possible. Air bolts ricochet — twice for
`gust`, three times for `galewind` — each bounce reflecting about the surface
and costing some speed, so a shot rattling down a corridor settles rather than
pinging forever. Air is the element that gets this because it is the one with
no ground effect: glancing off a wall is what makes it read as air rather than
as a weaker fire bolt.

Everything else stops, and leaves a mark: a flash in the element's colour and
three shards thrown back off the face. Those shards are fixed rather than
random — a spray that reshuffles every frame reads as noise. Without any of
it, a bolt that hit a wall and one that ran out of range looked identical.

A projectile spawned inside geometry, which happens if you fire with your back
against a wall, reports a zero normal rather than an invented one and simply
stops.

---

## Original assets

Every pack in this project is someone else’s art, and the packs do not cover
everything. `tools/blender_props.py` builds props from primitives in code and
renders them headlessly:

```powershell
.\tools\make_props.ps1                    # render, then convert
.\tools\make_props.ps1 -SkipRender        # convert existing renders
.\tools\make_props.ps1 -Only signpost     # one prop
```

Blender renders each prop eight times larger than needed; `make_props.ps1` then
box-downsamples it, flattens the palette, adds a dark rim around the
silhouette, and draws a contact shadow from the prop’s own base. The outline is
the single biggest thing separating a render from the hand-drawn art it sits
beside.

Three things were learned the hard way and are worth knowing before adding a
prop:

- **The camera angle is measured, not chosen.** The CraftPix interior tables
  show their front edge, their legs, and only a sliver of the top — a little
  over thirty degrees above the floor. Rendering at fifty-five hid every table
  leg behind its own top.
- **Model everything far thicker than life.** At fifty-six pixels across a
  two-metre frame, a realistic signpost is two pixels wide and vanishes under
  the outline pass.
- **Buildings need more height than feels right.** The first Mossvale lodge
  read as a lawn on a box: from above, a roof is most of what you see. Taller
  walls in alternating log tones and a dark shingle roof with only patches of
  moss fixed it.
- **Big renders need Cycles tiling.** Rendering a 192-pixel building at eight
  times scale ran Blender out of memory until `setup_render` turned on
  auto-tiling with 256-pixel tiles.
- **This does not beat hand-drawn art at these sizes.** Simple, chunky shapes
  — a signpost, a barrel, a strongbox — come out well. A bookshelf full of
  books does not. Use it for what the packs genuinely lack.

### The player hero

`tools/blender_character.py` builds the Hollow-born character, poses it and
renders every clip -- idle, walk, run, sprint, attack, jump, hurt and death, in
all four facings -- straight into the layered sheets the game reads:

```powershell
.\tools\make_character.ps1                    # every clip, then data/sprites.json
.\tools\make_character.ps1 -Only walk,sprint  # a couple of clips
```

The first version was bevelled boxes and read as boxes -- a crate of a head on
planks. The second is modelled the way a sprite is drawn rather than the way a
3D model is built:

- **Rounded forms.** Ellipsoids and tapered capsules only, so the silhouette
  curves and limbs join without seams: spiky hair with bangs and side locks,
  big eyes, a flared tunic with a belt and buckle, round-toed boots, and a red
  scarf whose tail streams out behind at speed.
- **Proportions matched to the CraftPix rigs**, so worn armour overlays authored
  against them still land: the head sits in the same band of the frame.
- **Cel shading.** Diffuse light through a three-step constant ramp, the shadow
  band shifted cool, from one fixed sun at the upper left so every facing is lit
  from the same side as the scenery.
- **Majority reduction, not averaging.** The render is four times game size;
  each game pixel takes the most common colour of the sixteen under it, so the
  bands stay flat and a two-pixel eye stays two pixels. Averaging turned the
  same render to mush.
- **A selective outline**: one pixel round each layer in a darkened version of
  the colour it borders, rather than flat black.
- **Occlusion between layers.** Each layer renders with the always-drawn layers
  in front of it as holdouts, so the scarf tail is cut away where the body hides
  it, and the sword is cut where the body stands in front of it -- in all four
  facings, from one weapon layer. Optional layers never cut others, or an empty
  hand would leave a hole.

The animation is poses written as functions of time rather than keyframes: a
shared gait drives walk and run (knees bending most on the passing step, the
body lowest at footfall, arms against legs, shoulders against hips), and the
sprint exaggerates it until it reads as a different gear at twenty-five pixels
-- a hard lean, the trailing leg driven out straight, the leading knee high,
both feet off the ground between strides, fists pumping. Leaning toward or away
from a camera above only slides the head down over the body, so the up and down
rows keep a fraction of the lean that the side rows show in full. Idle blinks.

---

## Elevation

The ground has height. A map may carry a coarse grid under its `dreamquest`
key, one level per cell:

```json
"elevation": {
  "cell": 64, "cols": 64, "rows": 48,
  "levels": [ 0, 0, 1, 2, ... ],
  "ramps":  [ [x, y, w, h], ... ],
  "face":   "assets/tiles/dirt_dark.png"
}
```

Level 0 is the ground everything used to sit on, so a map with no elevation
block behaves exactly as it did before. Each level lifts what stands on it by
`ELEVATION_RISE` pixels; `Map::LevelAt` and `HeightAt` answer for any world
point, and the world tells every entity its lift once a frame rather than
looking it up inside each draw call.

**Movement.** Stepping between cells of different level is blocked — that is
what makes a cliff a cliff. `ramps` are rectangles where the rule is suspended,
and the overworld puts one along the full length of the road and a clearing
around every place you can enter, because a raised map without them is a set of
islands. The self-test checks exactly that: every portal and the starting spawn
sit on flat ground, and every row of the map has a crossing somewhere on it.

**Drawing.** Ground and scenery are lifted by the terrain under them — scenery
by the ground under its base, not its middle, so a tree at the lip of a bank
belongs to the ground its trunk is on. Then the exposed banks are drawn:
soil texture down the face, grass rolling over the lip, a shadow thrown on the
ground below, and a dark line down the east and west edges so a plateau has an
outline all the way round rather than on one side only.

Four things were got wrong first, and all four are the same mistake — assuming
something would read that did not:

- **A face on its own is a brown bar.** Flat-filled faces looked like a stripe
  painted across the grass, because every other surface in view had grain and
  that one did not. They are textured now.
- **Higher ground has to look higher.** With the same grass above and below,
  the face is just a line between two identical fields. `LevelShade` darkens
  the ground floor slightly and gives it back a level at a time, so the top
  terrace is the texture as drawn. Colour modulation cannot brighten past the
  source, so it has to work in that direction.
- **The grid has to be coarser than the tiles.** At one level per 32px tile the
  terraces came out small and their edges fragmented into two- and three-tile
  bars. At 64 the plateaus are broad and their edges run far enough to read.
- **Height from smooth noise is a staircase.** A clean function of latitude
  terraces the whole map into straight bands from edge to edge. The slope is
  broken up with noise stretched along the east-west axis so each contour
  wanders.

Still to do: the faces are drawn procedurally, and dedicated cliff-edge art
would look considerably better than a textured rectangle with a lip on it.

### Getting up and down

**Jump** (`Space`, or left-stick click) is a hop in the direction you are
steering, or facing if you are not. Into a ledge up to `Player::CLIMB_LEVELS`
(two) high it carries you up onto it; off one it drops you down; on flat ground
it is a short hop. It plans the whole jump before leaving the ground -- every
sample along the path must be clear of walls and within two levels of where you
started, so it never passes through a tree or over a sheer cliff to land on the
far side -- and then owns the player until it lands: no steering, no attacks,
no knockback. Airborne, the draw lift is blended between the two terrain
heights plus an arc, so a climb rises smoothly instead of snapping up at the
edge.

A climbable ledge is otherwise indistinguishable from a wall you cannot pass --
you walk into either and stop -- so pushing against one puts **Climb up** or
**Drop down** on screen.

**Stairs.** Where a ramp crosses a level change the face is drawn as a flight
of stairs with stone cheeks, not as a cliff. Ramps used to be invisible
rectangles: walkable, but indistinguishable from the cliff either side.

Without jumping, **923 of the overworld's 3,072 height cells -- 30% of the
map -- could not be reached from the spawn.** The self-test now floods the
height grid from the spawn, allowing walking, ramps and jumps of up to two
levels, and fails if anything is left over.

### A bug worth remembering

The guild hall could not be entered, and it looked like the doorway was on a
cliff. It was: the overworld's. `Map::Unload()` cleared everything except the
height grid, and the parser only ever writes that grid when a map has one, so
a building never overwrote it -- walk from the overworld into any interior and
the interior inherited the hills outside. Every other self-test loads each map
into a fresh `Map`, which can never see that; the new check loads the overworld
and each building into the *same* one, the way the game does, and fails with
the fix reverted.

---

## Ground tiles

`tools/make_ground.ps1` generates them:

```powershell
.\tools\make_ground.ps1
```

They used to be one flat colour each, which was not an accident — they are cut
from CraftPix tilesets by looking for cells that are fully opaque with zero
variance, because those are the palette swatches a tileset is designed to be
laid over. It works, and it is why the overworld read as coloured paper: a
screen of grass was one RGB value repeated four thousand times.

The generated ones carry speckle, blades and grit. Every mark is placed with
wrapped coordinates, which is what makes them seamless — a blade running off
the right edge continues at the left, so there is no seam to line up. Several
variants per family, because one perfect tile repeated across a 4096-pixel map
is still a visible grid; `genmaps.cpp` picks between them with a hash of the
cell coordinates, and asks the asset manifest how many exist rather than being
told.

The road is generated too. The pack's cobbles are a cool blue-grey, and laid
three tiles wide through green grass the Sunken Road and Havenbrook's street
read as a river. It is warm grey setts in running bond now, a full 32-pixel cell
of sixteen stones in three variants, so it no longer shows as a two-by-two
check.

The other half of the coloured-paper problem was the biome boundaries. The
colour drift is smooth noise, and thresholding smooth noise draws a clean
contour — which on a 32px grid is a staircase of squares. The threshold is
jittered per cell now, which dissolves that edge into a scatter of cells from
both families.

---

## The HUD

Health and mana sit in the top left in brass plates: a bevelled frame lit from
the top left, a sunk track with quarter ticks, rivets at the corners of the
health bar, and a heart or droplet glyph at the left end so the two bars are
told apart at a glance rather than by colour alone.

The top right is a round minimap under a brass bezel with rivets, cardinal
notches and an amber pip at north. It is north-up and centred on the player:
terrain in the average colour of each ground tile, portals green, NPCs blue,
living monsters red, and the player gold with a nose pointing the way they
face. The map's name sits under the dial. Toasts and the quest tracker stack
below it, and the FPS counter moved to the bottom right.

The terrain is baked once per map into a small image -- one pixel per 8 world
pixels outdoors, per 4 in houses so a room is not a thumbnail -- and drawn as a
stack of one-pixel strips, each as wide as the circle at that height. That
keeps the map round without a mask or a shader and pixel-for-pixel crisp; the
game still holds its 72 fps cap.

The bezel is generated by `tools/make_ui.ps1` and the glyphs live in
`tools/icons.txt`; both run as part of `import_assets.ps1`.

---

## Sound

There are no audio files. `src/systems/audio.cpp` synthesises every effect at
start-up -- 35 of them, from tones, filtered noise, struck-metal partials and
Karplus-Strong plucked strings -- and plays them through one SDL3 audio stream
with a small mixer.

- **Combat:** swings (pitched up through a light chain, heavier for strong and
  charged attacks), hits, critical hits, blocked hits, deaths, the bow's twang,
  a cast pitched by element, arrows striking walls, and a monster's swing as it
  winds up. A killing blow is heard as the death rather than a hit on top of it.
- **The world:** footsteps by distance walked -- earth outdoors, planks indoors,
  stone in the mines -- chopping and mining strikes while you work, cooking and
  burning, chests, doors, locked doors, portals, pickups and coins, eating and
  equipping.
- **Progress:** level-up arpeggio, quest start and quest complete fanfares.
- **Menus:** cursor ticks, confirm, back and error, handled once in
  `Game::Update` rather than in every screen.

Sounds in the world are panned and fade with distance from the player, and
anything but a menu sound varies its pitch a few percent so a run of hits does
not sound mechanical.

**Ambience** is generated live, per map kind, and cross-fades on every map
change: wind with slow swells and birdsong in the forest, groves and fields;
a low breathing drone and echoing drips in the mines; a hearth's rumble and
crackle indoors; a quiet wind on the title screen.

Options has Master, Effects and Ambience volume. With no playback device the
game runs silently rather than failing.

---

## Item icons

Most inventory icons are cut from the CraftPix RPG UI icon sheet, but that
sheet has no log, bow, staff, hide, ore or roast, and those items had been
given the nearest cell in spirit. Playing the game showed how far off that was:
the Training Bow was a blue sword, Raw Hide and the Leather Jerkin were a boot,
both staves were an eye on a green tile, Roast Boar was a blue lump, and logs
and ore were metal ingots.

Those thirteen are drawn by hand instead, as text in `tools/icons.txt` — one
character per pixel, with a small palette per icon — and painted by:

```powershell
.\tools\make_icons.ps1
```

Text rather than image files so they can be read, diffed and touched up without
an image editor. `import_assets.ps1` runs this and `make_ground.ps1` at the end
of an import, because both overwrite or add to what the import cuts.

---

## Attack speed and cooldown

Weapons declare a `speed`, a multiplier on swing time, so **lower is faster**:
a dagger is 0.70, a bronze sword 1.00, a steel longsword 1.30. It scales every
phase of the swing and the cooldown after it, so a weapon’s whole rhythm moves
together rather than just the part you can see — and the animation is played at
a matching rate, or the character is still following through when the hitbox
has gone. Reach, width and knockback are deliberately left alone: those are
properties of the weapon’s shape, not of how fast it moves.

The property had been in `data/items.json` since the beginning and nothing ever
read it. `Equipment::AttackSpeed()` existed and was never called.

**Cooldown** is separate from recovery. Recovery is part of the swing and you
are committed during it; the cooldown is the gap *after* it, and it is what
stops the attack button being something you hold down. Mid-chain light attacks
have almost none, which is what makes continuing a combo quicker than starting
one; the finisher, the strong attack and the charged attack each cost more.
Bare-handed that works out at a sustained 2.2 hits a second, which the
self-test measures rather than assumes.

It is shown, because a gate the player cannot see is just an unresponsive
button: a thin bar under the feet that drains, gone inside a fifth of a second
between light attacks. The inventory states the speed as a rate — "1.14x
(fast)" — rather than as the raw multiplier, because a stat where smaller is
better needs explaining every time it is read.

Ranged and magic go through the same state machine, so a bow’s speed is its
rate of fire and a stave’s is its cast rate, with no extra code.

---

## Quests

Quests reach you three ways, all of them live:

- **The mission board** in Havenbrook — four contracts, gated on level and on
  what you have already finished.
- **NPC conversations** — Elder Maren runs the main chain (a letter, a missing
  surveyor, and what is gathering the orcs under Emberfell). The innkeeper,
  the smith, the watchman and the hunter have their own.
- **The woodland villages** -- Mossvale's notice board and the people of
  Mossvale, Fernhollow and the Whisperwood camp give the woodland chain.
- **Notes left in the world** — a water-stained note at the edge of the Mire
  starts the barrow chain, and a torn survey page on the Sunken Road advances
  Maren's.

Dialogue options are gated on quest state, inventory and skill level, so the
same NPC says different things before, during and after a quest.

---

## The world

`maps/overworld.mx` is 4096 × 3072 pixels — about eight screens across — and
the camera scrolls it as a viewport on the player. Biomes: meadow, greenwood,
northern foothills, the Mire, and the Cursed Reach, joined by the Sunken Road,
with the Whisperwood trail leaving from the east.

| Map | What it is |
| --- | --- |
| `overworld` | The Hollowmarch |
| `town_havenbrook` | The village, with four enterable buildings |
| `guild_hall`, `house_elder`, `house_inn`, `house_smith` | Interiors |
| `dungeon_emberfell_1` / `_2` | The mine, upper and lower workings; the lower level is locked until you find the rusted key, and the Warchief holds the last room |
| `dungeon_barrow` | Beneath the Mire |
| `whisperwood_trail` | The forest path east of the Hollowmarch: a woodcutter's camp, a stream with a plank bridge, and a fork |
| `mossvale` | A logging village behind a palisade at the trail's east end |
| `mossvale_lodge_hall`, `mossvale_herbalist` | The reeve's lodge and Oona the herbalist's cottage |
| `fernhollow` | A hamlet on a pond at the north fork, with a shrine and a ferry cottage |
| `fernhollow_cottage` | The ferryman's widow's cottage |

### The Whisperwood

A dirt trail leaves the overworld's east edge ("To the Whisperwood") and
winds through a forest dense enough that the path is the way through. Bram the
woodcutter camps where it crosses a stream; past him it forks, east to Mossvale
and north to Fernhollow. The trail is a map of its own rather than more
overworld because that is what makes it read as *a journey to somewhere* -- you
leave one place, travel, and arrive in another, the way DragonFable and
AdventureQuest Worlds stitch their zones together.

Mossvale and Fernhollow each have their own people, dialogue and quests: a
five-quest woodland chain (clear the trail, carry word to Fernhollow, hides for
Mossvale, the trail wardens, and an offering at the shrine), with kill stages
tied to the map they belong on so a wolf in the Mire does not count toward the
Whisperwood.

### Making zones feel like places

- **Arrival banners.** Entering an outdoor zone or dungeon fades in its name
  and a one-line subtitle over the screen for a few seconds. Walking in and out
  of a house does not re-announce the town you were already in.
- **Ambience.** Each map declares an `ambient` kind and `src/world/ambience.cpp`
  drifts particles through the world to match: falling leaves and fireflies
  under a darkened vignette in the forest, fewer of both in the village groves,
  pollen over the fields, dust in the mines. Particles live in world space and
  respawn on the far edge of the view, so they scroll with the ground rather
  than sliding across the screen.
- **Exit markers.** Near the edge of an outdoor map, each way out is labelled
  with an arrow and its destination -- "To the Whisperwood >" -- so the edges
  of a zone are signposted rather than discovered by walking into them.
- **Warnings at dangerous doors.** A portal can carry the Combat level its
  far side is meant for. Below it, the door prompt says so -- "Enter the
  Emberfell mine - dangerous: Combat 6 advised" -- where a new character used
  to find out by dying in the first room.
- **Legible banners.** The zone name sits on a feathered dark band; gold text
  over the foothills' sand was close to unreadable.
- **Clear streets.** Trees and tall fungus are drawn up from their base, so the
  generator keeps them two tiles back from every road; bushes may still line it.

Maps are big enough to grow: the base layer is bucketed into chunks and culled
against the camera, so adding another biome costs load time and nothing else.

---

## Level editor integration

Maps are the `.mx` format LevelEdit-Plus exports:

```json
{ "name": "...",
  "tiles": { "grass": { "filepath": "assets/tiles/grass.png",
                        "locations": [[cx, cy, w, h], ...] } } }
```

Placements are centre-anchored, matching `GameTile::Render` in the editor, so a
map drawn here lines up pixel-for-pixel with the editor view.

Everything a game needs on top of that — draw layers, collision, portals, spawn
points, enemies, NPCs, objects — lives under a separate `"dreamquest"` key.
**The editor ignores keys it does not recognise**, so these maps open in
LevelEdit-Plus, can be edited by hand, saved, and still run.

`tools/tilecut.cpp` exists because of the editor: the CraftPix tilesets ship as
packed autotile sheets, and the editor works with one image file per tile. It
cuts the atlases into individual tiles and finds each separate drawing on the
packed object sheets, so the same art is usable in both programs.

To rebuild the world from scratch: `.\build.ps1 -Maps`.

---

## Saving

Three slots, plus an autosave every two minutes and one on quitting to the main
menu. A save records the map, your exact position and facing, HP, every skill's
XP, inventory, worn equipment, quest progress, and the one-shot world flags —
which chests you have opened and which notes you have read — so loading puts
you back exactly where you left off. Saves are written to a temporary file and
renamed, so an interrupted write cannot destroy the previous one.

---

## Self-test

```powershell
.\build.ps1 -Test
```

Screenshots prove the game runs; they do not prove that the mission board names
a quest that exists, that every dialogue option leads somewhere, or that a loot
table only drops real items. `tools/selftest.cpp` links the game's own systems
and checks all of it — currently **3992 checks** covering:

- every sprite sheet and item icon exists on disk
- every loot table drops real items, and quest-critical drops are guaranteed
- every quest objective, prerequisite and reward resolves, and every quest has
  a giver somewhere in the world
- the dialogue graph is fully connected
- all sixteen maps load; portals point at real maps; every enemy, NPC and object
  resolves
- the OSRS XP table matches known values
- a starting character can actually win the first fight the level 1 board quest
  sends them into
- every projectile has art on disk, actually moves, is slow enough that its
  sub-steps cannot carry it through a wall, and is not drawn larger than the
  character firing it
- attack speed orders swings correctly, a zero speed is clamped rather than
  swinging instantly, every attack leaves a gap, and a bare-handed chain is
  neither a machine gun nor so slow that combat drags
- a sweep into a wall stops clear of it and reports a normal that sends a
  bounce back the way it came, while open floor reports no contact at all
- no portal or spawn sits on a cliff edge, every row of the overworld has a
  walkable crossing, and no raised ground is sealed off from the spawn once
  ramps and two-level jumps are counted
- no building inherits the overworld's height grid when loaded after it
- every NPC and usable object in every building can be walked up to from the
  door
- every portal in the woodland zones can be walked to from where you arrive,
  and the Whisperwood, Mossvale and Fernhollow journey round-trips
- every portal arrives at a spawn that exists, not inside a wall, and not on a
  step-through portal that sends the player straight back -- which is what a
  flight of stairs between two floors would otherwise do
- every way back through a portal arrives beside the way in, so leaving a
  building puts you on its doorstep rather than in the middle of town
- real fights, run frame by frame through the world update with the buttons
  pressed through the game's own input: a new character fighting back beats a
  fox and a boar, and the monster never stands on top of the player
- a monster's health bar stays hidden until it is attacked, shows for as long
  as it is hurt, and never shows more than it has; its fill is within a pixel
  of hp / max_hp, never empty while alive and never full while wounded
- every corpse despawns within three seconds of death, and a revived monster
  comes back whole with its bar hidden
- the HUD art is on disk, and the minimap clips every row to the edges of the
  map without ever sampling outside it
- a collect objective counts what the player is carrying
- every spell fires a projectile of its own element, all four elements are
  castable, and a level 1 character has the mana to cast one
- the elemental cycle closes and the multipliers point the right way
- Ranged and Magic read their own levels rather than Strength
- ragged animation rows declare a frame count for all four facings, and every
  paperdoll layer is on disk
- every worn overlay has art on disk and a rectangle that lands on the
  character rather than in empty frame, with helmets on the head and boots at
  the feet
- inventory, equipment, skills and quest progress survive a save round-trip
- the hero has every clip including sprint, each split into shadow, body and
  head with its sheets on disk, and its head and feet sit where the CraftPix
  rig's do so worn armour lines up
- a sprint, run through the real world update with Shift held, covers about 1.6
  times the ground of a run, plays the sprint clip, kicks up dust and leads the
  camera; a rig without a sprint clip runs faster instead; an attack stops a
  sprint, and a hit breaks it until the lockout passes
- a bow and a shield cannot be worn together, the swap works both ways, and a
  full bag refuses a swap rather than losing an item -- including the case where
  the only room is the slot the new item leaves
- every recipe that needs metal is smithed at the anvil and every other recipe
  is made at a workbench, and every crafting object in the world is drawn as
  the station it works as
- the doors to the mine and the barrow warn a new character, and the way to
  town and the Whisperwood do not
- every one of the 35 sounds is audible, short, finite and under clipping;
  each ambience is audible, stays in the background and fades out when
  cleared; forty hits at once are voice-capped and never exceed full scale

It exits with the number of failures, so CI can use it directly.

---

## Layout

```
src/
  game.cpp/h            state machine, window, main loop
  input.cpp/h           one action vocabulary for keyboard, mouse and gamepad
  camera.cpp/h          dead-zone follow camera with zoom and bounds clamping
  sprite.cpp/h          4-direction animation, data-driven
  texturecache.cpp/h    path -> texture
  world/
    map.cpp/h           .mx loader, chunked render, collision, portals
    world.cpp/h         entities, combat resolution, interaction, loot
  entity/               player, enemies, NPCs
  systems/              skills, items, loot, combat, quests, dialogue, saves,
                        projectiles and elements, spells
  ui/                   drawing helpers and every screen
tools/
  import_assets.ps1     rebuilds assets/ from the CraftPix zips
  tilecut.cpp           cuts atlases into individual tiles and sprites
  genmaps.cpp           builds the world into maps/*.mx
  selftest.cpp          content and systems validation
  make_ground.ps1       generated ground and interior tiles
  make_icons.ps1        paints the hand-drawn item icons in icons.txt
  make_sprites_json.ps1 / make_manifest.ps1
data/                   items, enemies, loot tables, quests, dialogue, sprites,
                        projectiles, spells
maps/                   generated .mx maps, editable in LevelEdit-Plus
```

About 8,600 lines of C++, excluding the vendored `nlohmann/json`.

---

## Credits

- Art: [CraftPix](https://craftpix.net) free asset packs — see
  [docs/ASSETS.md](docs/ASSETS.md)
- JSON: [nlohmann/json](https://github.com/nlohmann/json) (MIT)
- Base template: [Dexsidius/SDL3-Project-Template](https://github.com/Dexsidius/SDL3-Project-Template)
- Map format: [TheSardonicals/LevelEdit-Plus](https://github.com/TheSardonicals/LevelEdit-Plus)

Code is MIT ([LICENSE](LICENSE)). The art is not covered by that licence and is
not distributed here.
