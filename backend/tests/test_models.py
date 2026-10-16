"""Independent little-endian v2 fixture writer; no producer or datasets needed."""
import hashlib
import itertools
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

CLI, CORE = sys.argv[1:]
PERMS = list(itertools.permutations(range(4)))
GROUPS = bytes(map(int, '''
4 1 1 1 1 1 2 2 2 4 4 4 6
1 6 1 1 1 1 2 2 3 4 4 6 6
1 1 6 1 1 2 2 2 3 3 4 4 6
1 1 1 6 3 3 3 3 3 3 4 4 6
1 1 1 1 7 3 3 3 3 4 5 6 6
1 1 1 2 3 7 3 3 5 5 5 6 7
1 1 2 2 3 3 8 3 5 5 5 6 7
2 2 2 2 3 3 3 8 5 5 5 7 7
2 2 2 2 3 3 3 5 8 5 7 7 7
2 2 4 4 4 4 5 5 5 8 7 7 7
4 4 4 4 4 4 5 5 5 5 8 7 7
4 4 4 6 6 6 6 6 7 7 7 8 7
6 6 6 6 6 6 6 7 7 7 7 7 8
'''.split()))


def fnv(data):
    value = 14695981039346656037
    for b in data:
        value = ((value ^ b) * 1099511628211) & ((1 << 64) - 1)
    return value


def canonical(hand, board):
    keys = set()
    for perm in PERMS:
        mapped = [sorted(13 * perm[c // 13] + c % 13 for c in group)
                  for group in (hand, board)]
        key = 0
        for c in mapped[0] + mapped[1]:
            key = key * 64 + c
        keys.add(key)
    return min(keys), len(keys)


def envelope(payload, kind=4, version=2):
    return b'FLOPABS1' + struct.pack('<IIQQ', kind, version, len(payload), fnv(payload)) + payload


def fixture(round_id, weighting=1):
    dim = 8 if round_id == 2 else 3
    board = [37, 44, 13, 1, 28][:round_id + 3]
    vector = canonical([12, 25], board)
    records = {vector}
    rng = random.Random(492 + round_id)
    while len(records) < 205:
        deal = rng.sample(range(52), round_id + 5)
        records.add(canonical(deal[:2], deal[2:]))
    records = sorted(records)
    payload = bytearray()
    offsets = {}

    def put(name, fmt, *values):
        offsets[name] = len(payload)
        payload.extend(struct.pack('<' + fmt, *values))

    for i, text in enumerate(('c6b6963670623e21166e185832dd3a775eb75b6d',
                              'suit24-private2-unordered-board-lex6-v2',
                              'uniform-heads-up-half-ties-v2')):
        put(f'provenance{i}', 'I', len(text))
        payload.extend(text.encode())
    put('round', 'B', round_id)
    put('complete', 'B', 0)
    put('count', 'Q', len(records))
    offsets['records'] = len(payload)
    for key, weight in records:
        payload.extend(struct.pack('<QB', key, weight))
    put('source', 'Q', 0xFEDCBA9876543210)  # deliberately unrelated to embedded catalogue
    put('feature_round', 'B', round_id)
    put('dimension', 'I', dim)
    put('completions', 'I', (1081, 46, 1)[round_id])
    put('opponents', 'I', 990)
    put('legacy_seed', 'Q', 72)  # intentionally ignored
    put('grouping', 'Q', fnv(GROUPS) if round_id == 2 else 0)
    put('buckets', 'I', 200)
    put('restarts', 'I', 2)
    put('updates', 'I', 10)
    put('tolerance', 'd', .01)
    put('seed', 'Q', 1)
    put('update', 'B', 1 if round_id == 2 else 0)
    put('weighting', 'B', weighting)
    put('metric', 'B', 1 if round_id == 2 else 0)
    put('selected_seed', 'Q', 5)
    offsets['centers'] = len(payload)
    for _ in range(200):
        for j in range(dim):
            payload.extend(struct.pack('<f', .5 if round_id == 2 else (j + 1) / dim))
    assignments = [i % 200 for i in range(len(records))]
    offsets['assignments'] = len(payload)
    payload.extend(assignments)
    put('history_length', 'Q', 1)
    put('history_restart', 'I', 1)
    put('history_iteration', 'I', 10)
    put('repairs', 'I', 0)
    put('history_objective', 'd', 0)
    for name in ('objective', 'mean', 'median', 'p95', 'maximum'):
        put(name, 'd', 0)
    put('occupancy_length', 'Q', 200)
    offsets['occupancy'] = len(payload)
    for b in range(200):
        payload.extend(struct.pack('<Q', assignments.count(b)))
    offsets['weighted'] = len(payload)
    for b in range(200):
        payload.extend(struct.pack('<Q', sum((w if weighting else 1) for i, (_, w) in enumerate(records) if assignments[i] == b)))
    return payload, offsets, records.index(vector) % 200


def run(args, ok=True):
    result = subprocess.run(list(map(str, args)), text=True, capture_output=True)
    if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
        raise AssertionError(f'Sanitizer failure: {result.stderr}')
    if (result.returncode == 0) != ok:
        raise AssertionError(f'{args}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')
    return result


with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    tested = 0
    for round_id in range(3):
        for weighting in (0, 1):
            payload, offsets, bucket = fixture(round_id, weighting)
            path = root / f'model-{round_id}-{weighting}.bin'
            blob = envelope(payload)
            path.write_bytes(blob)
            result = run([CORE, path, round_id + 1, bucket])
            assert result.stdout.strip() == hashlib.sha256(blob).hexdigest()
            street = ('flop', 'turn', 'river')[round_id]
            assert 'partial model' in run([CLI, 'inspect-model', '--model', path, '--street', street], False).stderr
            mutations = [
                ('complete', 'B', 2), ('complete', 'B', 1), ('count', 'Q', 2**64 - 1),
                ('count', 'Q', 0), ('round', 'B', (round_id + 1) % 3),
                ('feature_round', 'B', (round_id + 1) % 3), ('dimension', 'I', 2**32 - 1),
                ('completions', 'I', 0), ('opponents', 'I', 989), ('grouping', 'Q', 1),
                ('buckets', 'I', 199), ('restarts', 'I', 0), ('updates', 'I', 100001),
                ('tolerance', 'd', float('nan')), ('tolerance', 'd', 1), ('tolerance', 'd', -1),
                ('update', 'B', 2), ('weighting', 'B', 2), ('metric', 'B', 1 if round_id != 2 else 0),
                ('centers', 'f', float('inf')), ('centers', 'f', -1),
                ('assignments', 'B', 255), ('history_length', 'Q', 2**64 - 1),
                ('history_length', 'Q', 0), ('history_restart', 'I', 2),
                ('history_iteration', 'I', 11), ('repairs', 'I', 201),
                ('history_objective', 'd', -1), ('objective', 'd', float('inf')),
                ('mean', 'd', 4), ('median', 'd', .5), ('p95', 'd', .5),
                ('maximum', 'd', 4), ('occupancy_length', 'Q', 201),
                ('occupancy', 'Q', 0), ('weighted', 'Q', 0),
                ('provenance0', 'I', 2**32 - 1), ('records', 'Q', 2**63),
            ]
            if round_id != 2:
                mutations.extend([('centers', 'f', .99)])  # violates CDF ordering
            else:
                mutations.extend([('update', 'B', 0)])
            for name, fmt, value in mutations:
                changed = bytearray(payload)
                struct.pack_into('<' + fmt, changed, offsets[name], value)
                path.write_bytes(envelope(changed))  # checksum remains valid
                run([CORE, path, round_id + 1, bucket], False)
                tested += 1
            # Structural key validation, including duplicates and incorrect orbit.
            changed = bytearray(payload)
            changed[offsets['records'] + 9:offsets['records'] + 18] = changed[offsets['records']:offsets['records'] + 9]
            path.write_bytes(envelope(changed))
            run([CORE, path, round_id + 1, bucket], False)
            changed = bytearray(payload)
            changed[offsets['records'] + 8] = 1
            path.write_bytes(envelope(changed))
            run([CORE, path, round_id + 1, bucket], False)
            for bad in (b'', blob[:31], blob[:-1], blob + b'x', envelope(payload, version=1),
                        envelope(payload, kind=1), envelope(payload + b'x'), b'INVALID!' + blob[8:], blob[:24] + b'\0' * 8 + blob[32:]):
                path.write_bytes(bad)
                run([CORE, path, round_id + 1, bucket], False)
            path.write_bytes(blob)
            run([CORE, path, ((round_id + 1) % 3) + 1, bucket], False)
    # Strict configuration / CLI behavior. Test helper exercises the real provider below.
    for round_id in range(3):
        payload, _, _ = fixture(round_id)
        (root / f'{round_id}.bin').write_bytes(envelope(payload))
    config = root / 'models.conf'
    config.write_text('# relative paths\nflop = 0.bin\nturn = 1.bin\nriver = 2.bin\n')
    run([CORE, config], True)
    assert 'partial model' in run([CLI, 'verify-models', '--config', config], False).stderr
    for text in ('flop=0.bin\n', 'flop=0.bin\nturn=1.bin\nriver=2.bin\nextra=x',
                 'flop=0.bin\nturn=1.bin\nriver=2.bin\nflop=x', 'flop=', 'invalid'):
        config.write_text(text)
        run([CLI, 'verify-models', '--config', config], False)
    run([CLI, 'verify-models', '--config', root / 'absent'], False)
    run([CLI, 'bucket', '--street', 'preflop', '--hand', 'Ac,Ad'])
    run([CLI, 'bucket', '--street', 'preflop', '--hand', 'Ac,Ac'], False)
    run([CLI, 'bucket', '--street', 'preflop', '--hand', 'Ac,Ad', '--unknown', 'x'], False)
    run([CLI, '--help'])
    run([CLI], False)
    print(f'Validated all streets/weightings and {tested} semantic corruptions plus envelope/key/config errors')
