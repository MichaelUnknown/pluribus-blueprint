# Model import (milestone 1)

The standalone C++20 library and `blueprint` CLI implement the v2 model contract
in [technical specification §3](blueprint-implementation.md#3-data-interface-of-the-existing-abstraction-program).
There is no dependency on the producer repository or its HTTP service.
The poker engine, trainer and dashboard remain future milestones.

## Build and test

Requirements: CMake 3.20+, a C++20 compiler, OpenSSL development headers/library
(for SHA-256 through libcrypto), and Python 3.9+ when tests are enabled.
On Ubuntu: `sudo apt-get install cmake g++ libssl-dev python3`.
On macOS, install CMake and OpenSSL and, if necessary, pass
`-DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"` at configuration.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/backend/blueprint --help
```

Tests generate small, deterministic synthetic files in a temporary directory.
They exercise all three rounds and both occupancy weighting modes, canonical-key
vectors, all suit permutations and board/hole reorderings, all 1,326 preflop
hands, SHA-256 against Python hashlib, configuration, and malformed artifacts.
Semantic corruption tests recompute the envelope checksum so that they reach the
relevant validation. Tests use explicit checks that remain active in Release.
CI runs Debug, Release, and AddressSanitizer/UndefinedBehaviorSanitizer builds.
No complete datasets are downloaded or committed.

## Configure complete models

Create a local `models.conf` with exactly three entries:

```ini
# Relative paths resolve against this configuration file's directory.
flop = /data/flop/model.bin
turn = /data/turn/model.bin
river = /data/river/model.bin
```

This intentionally small configuration is specific to model import. Blank lines,
leading/trailing whitespace and whole-line `#` comments are supported. Values are
literal paths, without quoting, expansion or inline comments. Unknown/duplicate
keys and missing/empty values are errors. A later trainer configuration can bind
these paths and the reported identities to its own versioned run profile.

```sh
build/backend/blueprint verify-models --config models.conf
# Equivalent explicit paths:
build/backend/blueprint verify-models --flop /data/flop/model.bin --turn /data/turn/model.bin --river /data/river/model.bin
# Load one street independently:
build/backend/blueprint inspect-model --model /data/turn/model.bin --street turn
# Query only the currently revealed board (unordered for bucket lookup):
build/backend/blueprint bucket --model /data/flop/model.bin --street flop --hand Ac,Ad --board Kh,7s,2d
build/backend/blueprint bucket --street preflop --hand Ac,Ad
```

The CLI prints model round (0/1/2), dimensions, state count, completeness, whole-file
SHA-256 and the source-catalogue fingerprint (hex, preserving all 64 bits).
`verify-models` also queries each specification vector through `BucketProvider`.
Bucket labels depend on the supplied models. Errors go to stderr with exit code 1;
there is no missing-key fallback. No command enables partial-model imports.

## Separate full-model integration check

Supply complete producer-generated v2 models, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBLUEPRINT_FULL_MODEL_CONFIG=/absolute/path/models.conf
cmake --build build --parallel
ctest --test-dir build -L integration --output-on-failure
```

This loads all three models, checks complete catalogue counts and orbit totals,
validates every canonical key/orbit and all assignments, and queries the three
canonical vectors. It requires external datasets and substantially more RAM and
time than CI. It is registered only when the configuration path is supplied.
To return to dataset-free CI, configure with `-DBLUEPRINT_FULL_MODEL_CONFIG=`.
The initial implementation has **not** been validated with complete external models
or producer-supplied golden files; synthetic fixtures establish parser behavior,
not producer interoperability. Run this integration check on delivered models
before relying on them for later training milestones.

## Library contracts and limits

Public headers live in `backend/include/blueprint/`; link `blueprint_core`.
`ModelReader::load(path, expected_street)` validates a single immutable model and
retains only sorted keys, assignments and identity metadata. `BucketProvider`
loads all three street models and dispatches preflop to the fixed `preflop-169-v1`
numbering: pairs `rank`; suited `13 + high*(high-1)/2 + low`; offsuit the same with
base 91. Card IDs are `13*suit + rank`, suits `cdhs`, ranks `23456789TJQKA`.
Its return type is uint16 to cover both preflop and postflop classes.

`CoveragePolicy::test_fixture_only` is an explicit library-level escape hatch for
tests and future bounded pilots. Never use it to start unrestricted training.
Even this mode validates every present record and requires 200 nonempty buckets;
only complete-coverage requirements are relaxed. All CLI commands use production
coverage. v1 models are rejected with an explicit v2-only error.

Import streams the envelope and payload while calculating FNV-1a and SHA-256;
it does not load a second copy of the entire file. Counts are checked against
remaining bytes and known catalogue bounds before allocation. Peak arrays for a
single model require about `10*N` bytes (keys, assignments, temporary orbit
weights), plus small input/stream buffers. A provider retains approximately
1.25 GB of key/assignment arrays for all three complete models; the river import
adds about 123 MB of temporary orbit weights. Allocator/library overhead is extra.
Every key is independently recanonicalized using all 24 permutations during
validation. Full-model import is intentionally exhaustive and may take significant
time; performance has not yet been measured on full datasets. Files must remain
unchanged throughout import. After load, queries are const and perform no I/O.

Checks include provenance, size/EOF, FNV checksum, SHA identity, rounds, feature and
training dimensions/enums, key ordering/uniqueness, card validity, canonical form,
orbit weights, CDF/equity centers, history bounds, finite/bounded diagnostics and
reconstructed canonical/weighted occupancies. Source catalogue fingerprints are
preserved without assuming they equal the embedded catalogue hash. Nearest-center
correctness cannot be checked without producer feature files and remains a
separate producer-side validation. SHA-256 identifies the input; it does not
establish trusted provenance without an independently trusted expected digest.
