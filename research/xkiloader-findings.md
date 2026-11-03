# XkiLoader DFF / TXD loader: how it works

Binary analysis of `release/libXkiLoader.so` (armeabi-v7a) with Ghidra 12.2 headless (auto-analysis + full decompile, 894 functions).

| File | SHA-256 | Size |
|---|---|---|
| libXkiLoader.so | bb520e6f0b5b7204cae63fc39e6f9d35c6ea2e29de8b2b9f92941fde39d02ce8 | 761208 |
| libXkiLoader64.so | d8fb8b2eeb851b1b8e7417f6b695d8ad1b53d7046cd49dad46dc8ad7821db601 | 1178576 |

The 64-bit build exports the same symbol names, so the logic is the same. Only notes are kept here, no decompiled code.

## Folder layout and lookup

- Root: `<game storage path>/xkiloader` (the game's storage string at `+0x90`), or `/sdcard/xkiloader`. Settings live in `XkiiGnss.xkiloader.ini`.
- `ScanDFFs` / `ScanTXDs` / `ScanTextures` / `ScanWeaponDFFs` / `ScanVehicles` walk subfolders recursively at mod load (`OnModLoad`).
- Each `.dff` is keyed by a hash of its file name without the extension, the same hash the game keeps in `CBaseModelInfo::m_nKey` (offset +4). It is stored in a `std::map<uint32 hash, std::string path>`. A model is replaced when `modelInfo->m_nKey` is in the map, so no IDs are needed and the file name alone picks the model.
- `ShouldLoadFile(path)` is an enable/disable filter driven by the ini and settings menu.

## DFF (models): four hooks

Every path does the same read:
1. `ReadFile(path)` reads the whole file into a vector. The CFileLoader path also copies it into a 16-byte aligned malloc buffer.
2. `RwStreamOpen(rwSTREAMMEMORY=3, rwSTREAMREAD=1, {ptr,size})`
3. `RwStreamFindChunk(stream, rwID_CLUMP=0x10, 0, 0)`
4. `RpClumpStreamRead(stream)`, then `RwStreamClose`.

| Hook | What it does |
|---|---|
| `CPedModelInfo::SetClump(this, clump)` | If the key matches, it reads the mod clump, destroys the game's clump (`RpClumpDestroy`) and calls the original SetClump with the mod clump. |
| `CAtomicModelInfo::SetAtomic(this, atomic)` | For objects: it reads the clump, gets its first atomic (`RpClumpForAllAtomics` + `GetFirstAtomicCB`) and calls the original SetAtomic with it. |
| `CWeaponModelInfo::SetClump` | Same as the ped hook (it uses a fallback offset if the symbol is missing). |
| `CFileLoader::LoadClumpFile(RwStream*, id)` | If `id < 20000` and the key matches, it swaps in a memory stream over the mod file and calls the original. A re-entry flag prevents recursion. |
| `CStreaming::RequestModel(id, flags)` | For "addon" models whose `m_pRwObject` (+0x34) is still null, it loads straight from the mod file, skipping gta3.img. It calls `CFileLoader::LoadAtomicFile(stream,id)` for atomic/time/weapon types (type mask 0x31e) or `LoadClumpFile` for types 5..7 (clump/ped/vehicle). It then sets `ms_aInfoForModel[id].m_nLoadState = 1` (LOADED) and does not call the original. |

Main idea: the game still streams the original model from gta3.img as normal. The mod clump is put in at the moment the model info receives its RW object (SetClump/SetAtomic). Sizes, the streaming memory budget and the img directory are never touched.

## TXD (textures): one global dictionary plus name-based raster swap

1. `InitializeTXDLoader` resolves `RwTexDictionaryCreate`, `RwTexDictionaryStreamRead`, `RwTexDictionaryForAllTextures`, `RwTexDictionaryFindNamedTexture`, `RwTexDictionaryAddTexture`, `RwRasterDestroy`, and hooks `RwTexDictionaryStreamRead` and `GetTextureFromDB` (the mobile texture database).
2. On first use it creates one global "Xki" RwTexDictionary and runs `ScanTXDs`.
3. `LoadTXDFile`: it reads the file into an aligned buffer, opens a memory stream, finds the `rwID_TEXDICTIONARY=0x16` chunk and calls `RwTexDictionaryStreamRead`. It moves every texture into the global dictionary (`ForAllTextures` + `MoveTextureCB` -> AddTexture), then destroys the temporary dictionary.
4. Its `RwTexDictionaryStreamRead` hook calls the original, then runs `ReplaceTextureCB` over every texture the game just read. When the global dictionary has a texture with the same name, it replaces `texture->raster` (RwTexture +0) with the mod raster.
5. Its `GetTextureFromDB(name)` hook returns the global-dictionary texture (`GetXkiTexture`) for the same name, else the original.
6. PNG (`LoadExternalTexture`): `stbi_load(..., 4)`, optional downscale (`TexConfig_GetSize/Downscale`), `RwRasterCreate(w,h,32, 0x8504)`, an `RwImage` struct over the pixels, `RwRasterSetFromImage`, `RwTextureCreate`, `RwTextureSetName`.

Important limit: step 3 uses the game's own `RwTexDictionaryStreamRead`, so it only accepts TXDs already converted to the Android platform (mobile TXD tools). PC Direct3D TXDs, which MTA DayZ uses, fail there. Textures are also replaced globally by name, so two mods with the same texture name collide.

## Compared with our DzModels (src/game/dzmodels.cpp)

| | XkiLoader | DzModels (4.00.2) |
|---|---|---|
| DFF entry point | SetClump / SetAtomic / LoadClumpFile / RequestModel | `CStreaming::ConvertBufferToObject` with a buffer swap and cd-size patch |
| Match key | file name hash = `m_nKey` | manifest id |
| Needs model unloaded first | no (SetClump catches every load) | yes (switches only while unloaded) |
| PC TXD | not supported | own decoder (DXT1/3/5, 8888, 565, 1555, 4444, pal8, L8) |
| Texture binding | global name-based raster swap | per-model TXD slot `dzm_<id>` |

What is worth adopting:
1. A `CPedModelInfo::SetClump` / `CAtomicModelInfo::SetAtomic` hook as the swap point (memory stream, then `RpClumpStreamRead`). This is simpler and safer than patching the streaming buffer and cd size, and it also covers models that load through paths other than ConvertBufferToObject.
2. Keep our own PC-TXD decoder and per-model TXD slot. Before the clump is read, set the model's txd index to our slot (or push our dictionary as current) so the DFF's materials resolve to our textures.
3. Optional: a `RwTexDictionaryStreamRead` post-hook name swap as a fallback for textures shared from other TXDs.
