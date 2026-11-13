# Contributing

Start with an open checkpoint issue and a small reproducible case. Use synthetic fixtures in tests; keep game assets, screenshots with personal UI, database dumps, credentials and local metadata/evidence out of pull requests. The six curated README renders are the only game-content demonstration images in this initial source release.

For a retrieval miss, explain the query, constraints, retrieval channel, annotation coverage, source game/version and how you independently discovered the candidate. An exact filename hit is not evidence that appearance search succeeds. Do not tune a benchmark and then report it as held out.

For pipeline changes, test malformed input, reruns, stale evidence and interrupted imports when relevant. Run the default Python tests and Node helper tests. Optional real model/Blender tests need separately installed dependencies and should be reported separately.

Public source snapshots must not contain native asset bytes or privately sourced annotations. Share original synthetic reproductions and aggregate results. The project is alpha; issue completion should cite measurable acceptance evidence and its limitations.
