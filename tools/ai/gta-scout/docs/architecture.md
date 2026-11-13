# Architecture and resource expectations

The independent Python core stores identity, metadata, evidence and FTS5 records in local SQLite. Semantic vectors are an optional table keyed by asset, corpus, encoder revision and description fingerprint. Normalized exact dot products avoid an ANN service but scale linearly with eligible vectors. Hybrid retrieval combines lexical and semantic ranks; images still need review.

Visual descriptions come from a user-selected vision-capable agent or a human. This project does not include an API service or free hosted vision inference. That agent's subscriptions, model tokens, review time and render compute are separate costs. Batches prepare image packets and preserve hashes, not automated semantic truth.

A historical local 50,017-record metadata build took about 2.6 seconds cold and 1.9 seconds unchanged, with an approximately 38 MiB baseline database. These are historical observations from the earlier workspace, not a portable performance guarantee or the public adapter's exact inventory. At 50,000 x 384 float32 vectors, raw vector values alone occupy about 77 MB; SQLite, text, images and caches add overhead. Per-query evidence/freshness validation and CLI model loading can dominate latency. Benchmark your actual source set before a full visual campaign.

Four-view renders and decoded PNG/DFF/TXD caches may consume gigabytes. The chunk runner's free-space check is a starting guard, not capacity planning. Scope work by source/kind, resume in bounded batches, inspect failures, and measure first/warm runs. Existing metadata snapshots and image evidence paths are local; exporting/moving a database alone does not make them portable.

The public alpha adds a PC SA metadata adapter with explicit source reports. It intentionally does not include the previous private full-game inventory, CDN, manually reviewed dataset, global pixel-audit cache, scene generators, traffic nodes or playback utilities.
