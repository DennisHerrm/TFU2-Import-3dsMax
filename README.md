# TFU2 Import for 3ds Max

Import characters and animations from **Star Wars: The Force Unleashed II** (PC)
straight from the game files into 3ds Max: skeleton, skinned meshes, materials with
textures, and animations.

> **Disclaimer:** unofficial, free fan project. It is **not** made by, affiliated with,
> endorsed or sponsored by Lucasfilm Ltd., LucasArts, The Walt Disney Company, Aspyr
> or Autodesk. Star Wars and The Force Unleashed are trademarks of Lucasfilm Ltd. /
> The Walt Disney Company; 3ds Max is a trademark of Autodesk. **No game data is
> included** – the plugin reads the files of your own installation of the game.

Version 0.2.1 – for **3ds Max 2016 to 2027** (Windows 10/11, 64-bit).

## Install

1. Close 3ds Max.
2. Download `TFU2Import-<version>-Setup.exe` from the
   [Releases](https://github.com/DennisHerrm/TFU2-Import-3dsMax/releases) page and run it
   (for all users, or only for you). It installs the Microsoft Visual C++ runtime if
   it is missing.
   *Or* download the ZIP, extract it and run `Install.bat`.
3. Start 3ds Max – the menu **TFU2 Tool** is there.

The files are not code-signed yet, so Windows SmartScreen may warn ("Windows protected
your PC" → *More info* → *Run anyway*). `SHA256SUMS.txt` on the release page lets you
check the downloads.

Requirements: *Star Wars: The Force Unleashed II* for PC installed (Steam version
tested); the game folder is found automatically in your Steam libraries.

## Use

Menu **TFU2 Tool** with two windows (or *File → Import* and pick `pak0.lp` /
`SWTFU2.exe` of the game):

* **Import TFU2** – the character window. The game folder is found automatically
  (Steam libraries) or picked with *Browse*. Tabs *Starkiller*, *Characters*,
  *Creatures + droids*, *Other*, *All*; *Static parts* also lists rigid pieces without
  a skeleton. Double-click or *Import*. Characters face the front view (−Y).
* **TFU2 Animations** – pick the character in the scene, then a clip:
  * *Own clips* – what the game assigns to this character (its chore sets and its
    cut-scene clips), default
  * *Same rig* – clips made for its rig
  * *Fits skeleton* – every clip whose bones exist in the skeleton
  * *All* – no filter
  * *Load* puts one clip on the skeleton. *Load all to timeline* puts every listed
    clip one after another into the timeline (bind pose at frame 0, *Gap* frames
    between clips), with a note track and the sequence custom attributes
    `NeoDexSequenceData` on the scene root (as in SWBF2 Import / WhiteoutDex).
    *Sequence* jumps the time slider to one clip.
  * *Root motion* off keeps the character in place.
  * *Face front* (default on) turns every clip so it starts at the origin facing the
    front view. In the game the root motion is relative to where the character
    looks; most clips start with the root turned ±90°.
  * Clips of the other side of a finisher or saber lock (giant `_ga_`, Vader `_mb_` …)
    are not offered as the character's own clips.

Files: `%LOCALAPPDATA%\TFU2Import\` holds the settings, `import.log` and the extracted
textures (normal maps are converted from DXT5nm to regular RGB PNGs).

## Known limits

* Capes and cloth are simulated in the game and are not part of the clips.
* Lightsabers and weapons are not attached yet.
* Facial morphs and cut-scene cameras are not imported.

## Building from source

`BUILD.bat` builds the plugin for every 3ds Max 2016–2027 whose SDK is installed
(Visual Studio with C++ and CMake needed) and installs it for the current user.
`installer\BAUE_RELEASE.bat` makes the Setup.exe (Inno Setup 6) and the ZIP.
File formats and design notes: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## License

GPL-3.0 (`LICENSE`) with an additional permission to link with the Autodesk 3ds Max
SDK (`LICENSE-EXCEPTION.md`). Bundled: [miniz](https://github.com/richgel999/miniz)
(MIT) and [bcdec](https://github.com/iOrange/bcdec) (MIT/Unlicense), licenses in
`vendor/`.
