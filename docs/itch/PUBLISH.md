# Publishing Solace/Reverie on itch.io

The process, start to finish. The words for every field are in
[PAGE.md](PAGE.md); this is the order to do things in and what to check. The
developer uploads and publishes; nothing here creates an account or posts on
anyone's behalf.

## 1. Build the zip

    .\package.cmd

(or `.\package.ps1` from a prompt). It builds the game and the server, builds
and runs the full self-test against the very objects the exe was linked from,
and **refuses to package if a single check fails** — the log is
`dist\selftest.log`. Allow a while: the self-test is over 84,000 checks.

It leaves two things in `dist\`:

- `SolaceReverie-win64-<version>.zip` — what the upload form takes;
- `SolaceReverie-win64-<version>\` — the same files unpacked, which butler
  pushes (step 5).

The version is `git describe`: commit first, so the zip is named after the
code that is in it. `-SkipTests` exists for a quick look at the zip, and the
script says so loudly; never upload one.

What is in the zip: `DreamQuest.exe` (windowed: no console behind it),
`DreamQuestServer.exe`, the 18 runtime DLLs, every tracked file under
`assets\ art\ maps\ data\`, `PLAY.txt`, `LICENSE.txt`,
`THIRD-PARTY-NOTICES.txt` and `licenses\`. Never saves, settings, the
screenplays, `docs\` or anything git ignores.

## 2. Check it the way a player will

Unpack the zip somewhere outside the repository — another drive if there is
one, or another Windows account — and run it from there.

1. Double-click `DreamQuest.exe`. The first time, SmartScreen: More info, Run
   anyway. No console window should appear.
2. Start a new game, play into the prologue, quit from the menu.
3. Start it again and Continue: the save must be there.
4. Look in `%APPDATA%\Dexsidius\DreamQuest\saves\` — that is where it went.

**Careful on this machine:** the first time a build that keeps saves in the
profile runs, it copies over the saves of the folder it was started from. To
test without touching your own saves, put an empty `portable.txt` next to the
exe in the test copy first; it then keeps everything beside itself.

## 3. Make the pictures

All of them come from the repository, so a new set is a command away.

    "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --background --python tools\blender_cover.py
    python tools\make_cover.py
    .\tools\make_titleart.ps1
    python tools\itch_shots.py

- `blender_cover.py` renders the cover's sword, cairn and ruins;
  `make_cover.py` paints the rest and writes the title painting, the logo,
  `docs\itch\cover.png` (630x500), `banner.png` (960x240) and
  `background.png`; `make_titleart.ps1` cuts the icons from the painting.
- `itch_shots.py` runs the game once per scene and writes
  `docs\itch\screenshots\01_…png` onward, numbered in page order. It works in
  its own stage folder (`dist\shots_stage`, with a `portable.txt`), so it never
  reads your settings or touches your saves. `python tools\itch_shots.py town`
  re-takes one scene.

Look at every picture before using it. The cover has to read as a thumbnail;
a screenshot that caught a frame between moments gets its timing changed in
`SCENES` and re-taken.

## 4. Create the project

At itch.io: Dashboard, **Create new project**. Fill it from PAGE.md in order:

1. Title, project URL, tagline, classification (Games), kind (Downloadable),
   release status, pricing.
2. **Uploads**: the zip; tick **Windows**, or the download button never
   appears. Not a demo.
3. **Details**: the description. Paste it rendered, not as Markdown.
4. Genre, tags, **AI generation disclosure** (answer as PAGE.md says, and keep
   the description's "How it was made" in step with it), custom noun,
   community.
5. **Cover image** and **screenshots**, in PAGE.md's order — the first is the
   one at the top of the page.
6. **More information**: the metadata sidebar from PAGE.md.
7. Visibility: **Draft**. Save.
8. **Edit theme**: banner, background, colours from PAGE.md.
9. Open the page logged out (a private window) and read it as a stranger:
   does the first screenshot say what the game is, does the download button
   appear, does the tagline fit on the card? Then set it **Public**.
10. Post the launch devlog from PAGE.md.

## 5. Later versions: butler

itch's own uploader. It sends only what changed, and the itch app updates
players itself.

    butler login
    butler push dist\SolaceReverie-win64-<version> <your-itch-name>/solace-reverie:windows --userversion <version>
    butler status <your-itch-name>/solace-reverie

Push the **folder**, not the zip. The channel name `windows` ticks the
platform. Players who installed with the itch app lose nothing on an update:
saves are in their profile, not in the game folder.

For each release: commit, `.\package.cmd`, check it (step 2), re-take any
picture whose screen changed, push, and post a devlog (PAGE.md has the shape).
Change the description only where the game changed.

## 6. After launch

- Answer comments; the "What would help" questions are there to be answered.
- A crash report needs the steps, and the save: `%APPDATA%\Dexsidius\DreamQuest\saves\`.
- Watch the itch analytics for views against downloads: a page that is seen
  and not downloaded needs a better first screenshot or tagline, not more
  features.

## Before it goes public

- [ ] `.\package.cmd` finished, self-test 0 failures, built from a commit
- [ ] Zip unpacked outside the repository, played, quit, relaunched, save kept
- [ ] `PLAY.txt`, `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt`, `licenses\` in the zip
- [ ] Cover 630x500, still readable as a thumbnail
- [ ] Screenshots uploaded in order, first one a fight
- [ ] Tagline, description, genre, tags, metadata, AI disclosure filled in
- [ ] Price, release status and community chosen
- [ ] Windows ticked on the upload
- [ ] Page read logged out, then set Public
- [ ] Launch devlog posted
