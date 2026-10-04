# TFU2 Import for 3ds Max

Import characters and animations from **Star Wars: The Force Unleashed II** (PC)
straight from the game files into 3ds Max: skeleton, skinned meshes, materials with
textures, and animations.

*Not affiliated with or endorsed by Lucasfilm, LucasArts, Disney, Aspyr or Autodesk.
You need your own copy of the game.*

## Status

Version 0.1.0 – built and tested for **3ds Max 2026**.

## Install

1. Run `BUILD.bat` (needs Visual Studio with C++ and the 3ds Max 2026 SDK), or copy a
   built package to `%APPDATA%\Autodesk\ApplicationPlugins\TFU2Import`.
2. Restart 3ds Max.

## Use

* Menu **TFU2 Tool → Import TFU2**, or *File → Import* and pick `pak0.lp` (or
  `SWTFU2.exe`) of the game.
* Pick the game folder (the one with `SWTFU2.exe` and `LevelPacks`) and press *Load*.
* **Characters** (left): pick one and press *Import character* (or double-click).
  *Static parts* also lists rigid pieces without a skeleton.
* **Animations** (right): select the character in the scene (any of its bones or
  meshes), pick a clip and press *Apply to skeleton* (or double-click).
  * *Same rig* – clips made for this character's rig (default)
  * *Fits skeleton* – every clip whose bones exist in the skeleton
  * *All* – no filter
  * *Root motion* off keeps the character in place.

Files: `%LOCALAPPDATA%\TFU2Import\` holds the settings, `import.log` and the extracted
textures (normal maps are converted from DXT5nm to regular RGB PNGs).

## Known limits

* Capes and cloth are simulated in the game and are not part of the clips.
* Lightsabers and weapons are not attached yet.
* Facial morphs and cut-scene cameras are not imported.
