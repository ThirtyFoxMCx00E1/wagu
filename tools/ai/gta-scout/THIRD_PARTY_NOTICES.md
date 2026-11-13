# Third-party notices and provenance

Original catalogue, review, campaign and semantic-retrieval scripts were developed in Dryxio's earlier GTA tooling workspace and extracted into a new repository without its history or datasets. The local TXDReader.js and ChunkType.js originated in that same author's code; they are not a copy of DragonFF. The standalone PC metadata adapter and packaging were added for this public alpha. Original code is MIT-licensed here.

Separately installed dependencies retain their own licenses:

| Dependency | Purpose | License / upstream |
| --- | --- | --- |
| Pillow | Image evidence and contact sheets | HPND; https://github.com/python-pillow/Pillow |
| NumPy | Embedding vector arithmetic | BSD-3-Clause; https://github.com/numpy/numpy |
| sentence-transformers | Optional local text encoder | Apache-2.0; https://github.com/huggingface/sentence-transformers |
| multilingual-e5-small weights | Optional multilingual text embeddings | MIT model card; https://huggingface.co/intfloat/multilingual-e5-small |
| pngjs | Optional TXD-to-PNG helper | MIT; https://github.com/pngjs/pngjs |
| Blender | Optional CLI renderer | GPL; https://www.blender.org/about/license/ |
| DragonFF | Optional DFF parser / separate Blender addon | GPL-3.0-or-later; https://github.com/Parik27/DragonFF |

These dependencies and model weights are not vendored. Their transitive packages retain their respective notices. Installing/copying/distributing a combined Blender/DragonFF environment requires respecting its GPL terms; this repository's MIT license does not relicense them.

The six README screenshots are renders of author-directed modding scenes created with Blender CLI and a mixture of original geometry and user-provided GTA content. Native game content visible in them belongs to its respective rights holders. The images illustrate the workflow; no game assets, game executable, annotation dataset or scene files are provided. No affiliation or endorsement is implied.
