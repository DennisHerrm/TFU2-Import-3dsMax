# TFU2 Import for 3ds Max

Import characters and animations from **Star Wars: The Force Unleashed II** (PC)
straight from the game files into 3ds Max: skeleton, skinned meshes, materials with
textures, and animations.

*Not affiliated with or endorsed by Lucasfilm, LucasArts, Disney, Aspyr or Autodesk.
You need your own copy of the game.*

## Status

Version 0.2.0 – builds for **3ds Max 2016 to 2027**.

## Install

1. Run `BUILD.bat` (needs Visual Studio with C++ and the 3ds Max SDK of each version you want), or copy a
   built package to `%APPDATA%\Autodesk\ApplicationPlugins\TFU2Import`.
2. Restart 3ds Max.

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

Files: `%LOCALAPPDATA%\TFU2Import\` holds the settings, `import.log` and the extracted
textures (normal maps are converted from DXT5nm to regular RGB PNGs).

## Known limits

* Capes and cloth are simulated in the game and are not part of the clips.
* Lightsabers and weapons are not attached yet.
* Facial morphs and cut-scene cameras are not imported.
