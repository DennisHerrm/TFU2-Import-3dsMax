# TFU Import – development notes

How the plugin reads *Star Wars: The Force Unleashed II* and *The Force Unleashed*
(PC) and how the 3ds Max side is built. Everything here was worked out from the game data itself.

## Building

| Script | What it does |
|---|---|
| `BUILD.bat` | builds `TFU2Import.dlu` for every 3ds Max 2016–2027 whose SDK is installed (`C:\Program Files\Autodesk\3ds Max <year> SDK`), then runs `INSTALLIERE.bat` |
| `BUILD.bat 2026` | only that version |
| `INSTALLIERE.bat` | copies `output\` to `%APPDATA%\Autodesk\ApplicationPlugins\TFU2Import`; versions whose Max is running are skipped |
| `installer\BAUE_RELEASE.bat` | Setup.exe (Inno Setup 6) + ZIP + SHA256SUMS in `dist\` |

Needs Visual Studio with C++ and CMake. The reader also builds without the Max SDK
as `tfudump.exe` (`cmake -S . -B build_tool -A x64 -DTFU2_BUILD_TOOL=ON`):

    tfudump <game folder> list | model <name> | anim <path> [n] | materials <name>
                          checkanims | checkmodels | fit <name> | stretch <name>
                          animhash | texture <path> <dir> [normal]

The game folder can be either game. `checkanims` decodes all animation files
(TFU2 6 516, TFU1 5 007), `checkmodels` all models. `animhash` prints a checksum over
every decoded value (compare before/after decoder changes), `stretch` lists a
character's own clips that change bone lengths by more than 15 % (clips of another
rig).

Source layout: `src/tfu_*` is the reader (no Max SDK), `src/tfu2import_*` the plugin,
`src/tfu2_ui.h` the window styling, `vendor/` miniz (gzip/PNG) and bcdec (DXT).

## Game data

`LevelPacks\pak0.lp … pak3.lp` are plain ZIP archives, every entry *stored*
(method 0), 48 461 files. Paths are mixed case in the archive and lower case in
material files – look them up case-insensitively.

| Extension | Content |
|---|---|
| `.gto` | models, gzip + a compact variant of Tweak GTO |
| `.animations` | animations, "R2D2"/"mina" format |
| `.actor.xml` | character: `mkeyGTOModel`, `ActorMaterialMapping`, `mKeyBaseActor`, chore resources |
| `.material` | XML; textures as `<property name="colorMap" type="texture" default="….dds">` |
| `.choreset.xml`, `.choresetgroup.xml` | which animations a character uses (`AnimID` = clip file name) |
| `.dds` | DXT1/DXT5; normal maps are **DXT5nm** (X in alpha, Y in green, R=255, B=0) |

## The Force Unleashed (TFU1)

Same engine family, same file formats inside, different packs. `LevelPacks\` holds
159 `*.lp` files – one per level, costume (`player*.actor.xml_pc.lp`) and menu – in
the **kaPA** format. The same file is stored in many packs (identical copies);
57 900 distinct files in total, nothing compressed, GTOs without gzip.

    header 0x50 bytes, u32[20]:  [0] "kaPA"  [1] 5  [2] entry count
        [6] size of an extra header (level name) after 0x50
        [7] size of the name table   [8] offset table   [12] start of data
    name table at 0x50 + [6]: zero-terminated paths ("Scum/characters/PCDX/...")
    entry table right after it (16-aligned), 64 bytes per entry:
        +0x04 type hash   +0x14 offset into the name table   +0x28 size
    offset table, 16 bytes per entry: u32 index, u32 offset from [12]

Differences that matter:

* Paths: `Scum/characters/PCDX/<rig>/rigs/<name>/<name>.gto`, clips in
  `Scum/animation/characters/PCDX/<rig>/...` and `Scum/vignettes/...` (cut-scenes).
* `mDefMaterial` in the actor is only a name; the `.material` of that name is looked
  up (preferably below the character folder). Texture properties are `Diffuse`,
  `DiffuseAndAlpha`, `Normal`, `SpecularandAO`. Every GTO material also embeds a
  `<materialDefinition>` (`Data`), used as a fallback.
* Animations: additionally fixed encoding `0x08` with rotation type 8–15 – floats
  only for the channels in the `1xyz` mask (vehicles, cut-scene props).
* Rig tokens in clip names: `ma mb fa md frs/frc` (female rancor) `brs` (bull rancor)
  `kz` (Kazdan) `fb gsl gnk jkt dh am wk wb trix tdl tu tum dm spk`.
* Imported nodes carry `tfu2_spiel` = 1 or 2; the animation window loads that game.

## GTO models

Magic 0x29F, version 3, gzip-compressed. The headers are **shorter** than in open GTO:

    header     5 x u32   magic, string count, object count, version, flags
    strings    zero-terminated
    object     8 bytes   u16 name, u16 protocol, u8, u8, u16 component count
    component  4 bytes   u16 name, u16 property count
    property  12 bytes   u16 name, u16 junk, u32 count, u8 type, u8 width, u16 junk
    data       all properties back to back (count x width values)

Types: 0 int, 1 float, 2 double, 3 half, 4 string (u32 index), 5 bool, 6 short, 7 byte.

A character has `Info`, `Geometry/Model`, `Materials`, `Skeleton`, and per sub-mesh a
`RenderMesh` (MeshInfo: MaterialName, BonePalette, NumWeights) followed by a
`PlatformGeometry` (GeometryInfo, Indices, Stream0/1, VertexDecl).

* **Skeleton:** BoneNames, BoneCRCs, BoneParents, BasePoseMatrices – 4x4 **local**
  matrices, row vectors (row 3 = translation), metres, Y up. Local rotation =
  R(q) transposed. BoneCRCs are a hash of the bone name (not standard CRC32/FNV);
  all humanoids share the same bone names. Some skeletons have several roots
  (`root[0]` plus `shape_0…n`).
* **Vertex declaration:** D3D9; usage stored as the bit `1 << D3DDECLUSAGE`
  (1 position, 2 weights, 4 indices, 8 normal, 32 UV, 64 tangent, 128 binormal,
  1024 colour). Weights and indices are D3DCOLOR, byte i pairs with byte i; an index
  points into the sub-mesh's BonePalette. Weights always sum to 255.
* **Shared vertex buffers:** a stream with `SharedVertexData = k` instead of
  `VertexData` uses the buffer of the k-th PlatformGeometry (common in level models).
* Primitive type 5 = triangle strip (degenerate triangles as seams), 4 = list.
  UV V points down (D3D).

## Animations

Container `R2D2pack` (with an entry list) or directly `R2D2mult`, containing a
`mina` block. Base = mina + 0x40.

    mina+0x14 u32 track count     mina+0x18 f32 duration (30 fps)
    mina+0x30 u32 track table     mina+0x34 u32 key-time lists   (both relative to base)

Track table, 16 bytes per track: u32 bone CRC, u32 key-time offset, u32 data offset,
u16 key count, u8 f0, u8 f1. Key-time lists are shared between tracks and padded to
16 bytes (the padding is uninitialised memory).

**f1:** bit 0 translation present, bit 1 rotation present, bit 6 key times are u8
(else u16, for clips longer than 255 frames), bits 2–5 the encoding:

* **0x3C quantised** (most tracks). Translation: f32 range (usually 3.2767), per
  channel an int16 centre and a u8 bit width. Rotation: per channel int16 centre and
  u8 bit width. Then a **bit stream, MSB first**, keys interleaved (translation
  channels, then rotation channels). Value = centre + raw − 2^(bits−1).
  Translation = value · range / 32767; rotation x, y, z = value / 32767,
  w = √(1 − x² − y² − z²).
* **0x28** raw floats per stored channel (cameras, cut-scenes).
* **0x00 / 0x20** fixed types, the f0 nibble is the type: rotation 1 = float4,
  2 = int16 x4, 3 = int16 x3 (w rebuilt); translation 1 = float4, 2 = f32 scale +
  int16 x3, 8–15 = float3.

**f0:** high nibble rotation, low nibble translation; for 0x3C/0x28 a mask `1xyz`
of the stored channels (missing channels are 0).

Values are full **local** rotations/translations. A missing channel means the bind
pose of the model – that is how one clip fits characters of different proportions.

Verified against 56 clips the game also ships as Maya XML: largest difference
0.6° / 0.6 mm (the compression itself). All 6 516 files decode without overrun.

## Which clips belong to a character

* `AnimID`s from the chore sets of every actor that uses the model (chore and moveset
  resources, also through `mKeyBaseActor`), chore set groups resolved, plus the
  `ChoreData/<name>` folder whose name the character folder starts with
  (DarthVader, Juno, ewok, Player …).
* Cut-scene clips whose last name part names the character (`…_darthVader`, `…_player`).
* Only clips whose tracks exist in the skeleton, and whose **rig token** matches:
  names carry `_ma_` (maleAverage), `_mb_` (maleBrute), `_fa_`, `_md_`, `_ga_`
  (giant), `_gb_`, `_td_`, `_am_`, `_ds_` … Finisher and saber-lock chore sets list
  both participants; the opponent's clips share bone names but not bone lengths.

## 3ds Max side

* Axes: game (x, y, z) → Max (x, −z, y) at the root only; scale 1 m = 39.37 units
  (`EinheitenJeMeter=` in `%LOCALAPPDATA%\TFU2Import\einstellungen.txt`).
* The model's `root[0]` is turned −90° about Y; the whole character (bones and
  vertices) is rotated by the inverse so it faces the front view (−Y).
* `holster[0/1]` and `l/rUpperLegWeight[0]` hang on the hips in the model but on the
  thigh in the animation data – re-parented to the thigh.
* Every node carries `tfu2_id` (one per import) and `tfu2_figur`; bones also
  `tfu2_crc` and `tfu2_rest` (local bind pose in game space).
* Normals from the file as explicit normals; Standard material with diffuse (DDS),
  Normal Bump (DXT5nm converted to an RGB PNG) and specular level.
* Animation: linear controllers, keys at the stored frames, 30 fps,
  `SetValue(CTRL_ABSOLUTE)` in animate mode. *Face front*: removes the yaw and
  horizontal position of the first root key (most clips start with the root turned
  ±90°; in the game root motion is relative to the character's heading).
* *Load all to timeline*: bind pose at frame 0, clips one after another with a gap,
  note track on the scene root (two keys per sequence) and custom attributes
  `NeoDexSequenceData` (same definition as WhiteoutDex / SWBF2 Import).

## Known limits

* Capes and cloth are simulated in the game and are not part of the clips.
* Lightsabers and weapons (`Game/Disc/Handheld/…`) are not attached yet.
* Facial morphs (`shape_N`, `morph[N]` tracks) and cut-scene cameras are not imported.
* The meaning of the `_SIRA` / `_SGTA` texture channels is not known yet.
