# Vendored tools (4.13.19)

Plain copies (no .git) so a new account or agent has everything inside wagu. All MIT, by Dryxio; each
folder keeps its LICENSE / NOTICE files. Update: re-copy from the fork and change the commit below.

| folder | fork (upstream) | commit | left out |
|---|---|---|---|
| gta-scout/ | [ThirtyFoxMCx00E1/gta-scout](https://github.com/ThirtyFoxMCx00E1/gta-scout) ([Dryxio/gta-scout](https://github.com/Dryxio/gta-scout)) | ab2c6f27fd03ffcb934cabfa72b3e151f4273f13 | docs/images (15 MB showcase renders) |
| gta-flow/ | [ThirtyFoxMCx00E1/gta-flow](https://github.com/ThirtyFoxMCx00E1/gta-flow) ([Dryxio/gta-flow](https://github.com/Dryxio/gta-flow)) | c3d9ad40c0facfbe7351a134ba3601e49be8d6be | docs/images (4.8 MB renders) |
| reagent/ | [ThirtyFoxMCx00E1/reagent](https://github.com/ThirtyFoxMCx00E1/reagent) ([Dryxio/reagent](https://github.com/Dryxio/reagent)) | d12cea338c61898b06a86fe8adb25275fa9d5615 | - |
| ghidra-bridge/ | [Dryxio/ghidra-bridge](https://github.com/Dryxio/ghidra-bridge) | faaf05363796cbc43eac7924f3b6981cd5e40619 | - |

Ghidra itself ([NationalSecurityAgency/ghidra](https://github.com/NationalSecurityAgency/ghidra), Apache-2.0)
is not copied: the release zip is ~570 MB (over GitHub's 100 MB file limit). `setup.sh --ghidra`
downloads the latest release (Ghidra_12.2_build when this was written).
