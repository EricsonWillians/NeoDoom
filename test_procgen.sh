#!/bin/bash
# Headless procedural map generator smoke and structural validation.

set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
RERELEASE_IWAD="/home/ericson-willians/.steam/debian-installation/steamapps/common/Ultimate Doom/rerelease/doom2.wad"
RERELEASE_DOOM_IWAD="/home/ericson-willians/.steam/debian-installation/steamapps/common/Ultimate Doom/rerelease/doom.wad"
CONFIG_IWAD="/home/ericson-willians/.config/biaseddoom/doom2.wad"
BIN="${BIN:-$ROOT/build/biaseddoom}"
IWAD="${IWAD:-}"

if [ -z "$IWAD" ]; then
    if [ -f "$RERELEASE_IWAD" ]; then
        IWAD="$RERELEASE_IWAD"
    else
        IWAD="$CONFIG_IWAD"
    fi
fi

if [ ! -f "$IWAD" ]; then
    echo "ERROR: IWAD not found. Set IWAD=/path/to/doom2.wad"
    exit 1
fi

if [ ! -x "$BIN" ]; then
    echo "ERROR: binary not found at $BIN"
    exit 1
fi

run_test() {
    local seed=$1
    local theme=${2:-techbase}
    local difficulty=${3:-3}
    local size=${4:-3}
    { timeout --signal=KILL 20 "$BIN" -nosound -nomusic -nogui -iwad "$IWAD" \
        +dumpprocudmf "$seed" "$theme" "$difficulty" "$size" +quit 2>&1 || true; } 2>/dev/null
}

count_blocks() {
    local block=$1
    grep -c "^${block}$" /tmp/procmap_test.udmf 2>/dev/null || true
}

measure_exit_room_area() {
    python3 - <<'PY'
import collections
import re

text = open('/tmp/procmap_test.udmf', encoding='utf-8').read()

def blocks(kind):
    return [dict((key, value.strip('"')) for key, value in
                 re.findall(r'^\s*(\w+)\s*=\s*([^;]+);', body, re.M))
            for body in re.findall(r'(?m)^' + kind + r'\s*\n\{(.*?)\n\}', text, re.S)]

vertices = blocks('vertex')
sides = blocks('sidedef')
lines = blocks('linedef')
exit_line = next((line for line in lines if line.get('special') == '243'), None)
if exit_line is None:
    print(0)
    raise SystemExit

trigger_sector = int(sides[int(exit_line['sidefront'])]['sector'])
neighbors = collections.Counter()
for line in lines:
    front = int(sides[int(line['sidefront'])]['sector'])
    back = int(sides[int(line['sideback'])]['sector']) if 'sideback' in line else -1
    if front == trigger_sector and back >= 0 and back != trigger_sector:
        neighbors[back] += 1
    if back == trigger_sector and front != trigger_sector:
        neighbors[front] += 1
if not neighbors:
    print(0)
    raise SystemExit

room_sector = neighbors.most_common(1)[0][0]
double_area = 0.0
for line in lines:
    front = int(sides[int(line['sidefront'])]['sector'])
    back = int(sides[int(line['sideback'])]['sector']) if 'sideback' in line else -1
    if front == room_sector and back == room_sector:
        continue
    first = vertices[int(line['v1'])]
    second = vertices[int(line['v2'])]
    cross = (float(first['x']) * float(second['y']) -
             float(second['x']) * float(first['y']))
    if front == room_sector:
        double_area += cross
    if back == room_sector:
        double_area -= cross
print(round(abs(double_area) * 0.5))
PY
}

report_dump() {
    local sectors things locks keys monsters decorations
    sectors=$(count_blocks sector)
    things=$(count_blocks thing)
    locks=$(grep -c '^\s*locknumber = ' /tmp/procmap_test.udmf 2>/dev/null || true)
    keys=$(grep -Ec '^\s*type = (5|6|13);' /tmp/procmap_test.udmf 2>/dev/null || true)
    monsters=$(grep -Ec '^\s*type = (7|9|16|58|64|65|66|67|68|69|71|72|84|88|89|3001|3002|3003|3004|3005|3006);' \
        /tmp/procmap_test.udmf 2>/dev/null || true)
    decorations=$(grep -Ec '^\s*type = (15|20|35|41|43|44|46|48|55|56|57|85|86|2028);' \
        /tmp/procmap_test.udmf 2>/dev/null || true)
    echo "  sectors=$sectors things=$things monsters=$monsters decorations=$decorations locks=$locks keys=$keys"
}

validate_geometry() {
    python3 - <<'PY'
import collections
import math
import re
import sys

text = open('/tmp/procmap_test.udmf', encoding='utf-8').read()

def blocks(kind):
    result = []
    for body in re.findall(r'(?m)^' + kind + r'\s*\n\{(.*?)\n\}', text, re.S):
        result.append(dict((key, value.strip('"')) for key, value in
                           re.findall(r'^\s*(\w+)\s*=\s*([^;]+);', body, re.M)))
    return result

vertices = blocks('vertex')
sectors = blocks('sector')
sides = blocks('sidedef')
lines = blocks('linedef')
things = blocks('thing')
errors = []
adjacency = collections.defaultdict(set)
referenced = set()
diagonal_lines = 0
boundary_lengths = set()
solid_walls = []

def lround(value):
    return math.floor(value + 0.5) if value >= 0.0 else math.ceil(value - 0.5)

def point_segment_distance(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    length_squared = dx * dx + dy * dy
    if length_squared == 0.0:
        return math.hypot(px - ax, py - ay)
    amount = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length_squared))
    return math.hypot(px - (ax + amount * dx), py - (ay + amount * dy))

for index, line in enumerate(lines):
    try:
        v1, v2 = int(line['v1']), int(line['v2'])
        front = int(line['sidefront'])
    except (KeyError, ValueError):
        errors.append(f'linedef {index} has malformed references')
        continue
    if not (0 <= v1 < len(vertices) and 0 <= v2 < len(vertices)):
        errors.append(f'linedef {index} has an invalid vertex reference')
        continue
    if vertices[v1]['x'] == vertices[v2]['x'] and vertices[v1]['y'] == vertices[v2]['y']:
        errors.append(f'linedef {index} has zero length')
    if (vertices[v1]['x'] != vertices[v2]['x'] and
            vertices[v1]['y'] != vertices[v2]['y']):
        diagonal_lines += 1
    if not 0 <= front < len(sides):
        errors.append(f'linedef {index} has an invalid front side')
        continue
    front_sector = int(sides[front]['sector'])
    referenced.add(front_sector)
    if 'sideback' not in line:
        if line.get('blocking') != 'true':
            errors.append(f'one-sided linedef {index} is not blocking')
        if 'texturemiddle' not in sides[front]:
            errors.append(f'one-sided linedef {index} has no middle texture')
        if line.get('dontpegbottom') != 'true':
            errors.append(f'one-sided linedef {index} is not bottom-pegged')
        x1, y1 = float(vertices[v1]['x']), float(vertices[v1]['y'])
        x2, y2 = float(vertices[v2]['x']), float(vertices[v2]['y'])
        length = math.hypot(x2 - x1, y2 - y1)
        boundary_lengths.add(round(length))
        solid_walls.append((x1, y1, x2, y2))
        expected_x = lround((128.0 - length) * 0.5)
        if int(sides[front].get('offsetx', '0')) != expected_x:
            errors.append(f'one-sided linedef {index} is not symmetrically centered horizontally')
        expected_y = -round(float(sectors[front_sector]['heightfloor']))
        if int(sides[front].get('offsety', '0')) != expected_y:
            errors.append(f'one-sided linedef {index} is not world-aligned vertically')
    else:
        back = int(line['sideback'])
        if not 0 <= back < len(sides):
            errors.append(f'linedef {index} has an invalid back side')
            continue
        if line.get('blocking') == 'true':
            errors.append(f'two-sided linedef {index} incorrectly masquerades as a solid wall')
        back_sector = int(sides[back]['sector'])
        referenced.add(back_sector)
        if front_sector != back_sector:
            adjacency[front_sector].add(back_sector)
            adjacency[back_sector].add(front_sector)

for index, side in enumerate(sides):
    sector = int(side.get('sector', '-1'))
    if not 0 <= sector < len(sectors):
        errors.append(f'sidedef {index} has an invalid sector reference')

doors = [line for line in lines if line.get('special') == '12']
door_sectors = collections.defaultdict(list)
if not doors:
    errors.append('map contains no functional Door_Raise linedefs')
for index, door in enumerate(doors):
    if door.get('playeruse') != 'true' or door.get('repeatspecial') != 'true':
        errors.append(f'door {index} lacks use/repeat activation')
    if door.get('arg0') != '0' or door.get('arg1') != '16' or door.get('arg2') != '150':
        errors.append(f'door {index} has invalid Door_Raise arguments')
    back = int(door['sideback'])
    door_sector = int(sides[back]['sector'])
    door_sectors[door_sector].append(door)
    sector = sectors[door_sector]
    if sector['heightfloor'] != sector['heightceiling']:
        errors.append(f'door sector {door_sector} does not start closed')
    front = int(door['sidefront'])
    if sides[front].get('texturetop') in ('DOORTRAK', 'DOORRED', 'DOORBLU', 'DOORYEL', None):
        errors.append(f'door {index} has an invalid face texture')
    if door.get('dontpegtop') == 'true':
        errors.append(f'door {index} incorrectly pins its moving face')
    a, b = vertices[int(door['v1'])], vertices[int(door['v2'])]
    face_width = math.dist((float(a['x']), float(a['y'])),
                           (float(b['x']), float(b['y'])))
    expected_crop = round(max(0.0, (128.0 - face_width) * 0.5))
    if door.get('secret') != 'true' and int(sides[front].get('offsetx', '0')) != expected_crop:
        errors.append(f'door {index} does not center its 128-unit face texture')
    front_sector = sectors[int(sides[front]['sector'])]
    face_height = float(front_sector['heightceiling']) - float(sector['heightfloor'])
    expected_scale = min(1.0, 128.0 / max(1.0, face_height))
    actual_scale = float(sides[front].get('scaley_top', '1.0'))
    if abs(actual_scale - expected_scale) > 0.001:
        errors.append(f'door {index} vertically repeats instead of fitting once '
                      f'(scale={actual_scale:.3f}, expected={expected_scale:.3f})')
    actual_back_scale = float(sides[back].get('scaley_top', '1.0'))
    if abs(actual_back_scale - expected_scale) > 0.001:
        errors.append(f'door {index} has mismatched back-face vertical scale')

track_for_lock = {0: 'DOORTRAK', 1: 'DOORRED', 2: 'DOORBLU', 3: 'DOORYEL'}
for sector_index, faces in door_sectors.items():
    if len(faces) != 2:
        errors.append(f'door sector {sector_index} has {len(faces)} faces instead of two')
        continue
    centers = []
    for face in faces:
        a, b = vertices[int(face['v1'])], vertices[int(face['v2'])]
        centers.append(((float(a['x']) + float(b['x'])) * 0.5,
                        (float(a['y']) + float(b['y'])) * 0.5))
    if abs(math.dist(centers[0], centers[1]) - 16.0) > 0.01:
        errors.append(f'door sector {sector_index} is not a classic 16-unit slab')
    lock = int(faces[0].get('locknumber', '0'))
    secret_door = all(face.get('secret') == 'true' for face in faces)
    tracks = []
    for line in lines:
        front = int(line['sidefront'])
        if ('sideback' not in line and int(sides[front]['sector']) == sector_index and
                'texturemiddle' in sides[front]):
            tracks.append((line, sides[front]))
    if len(tracks) != 2:
        errors.append(f'door sector {sector_index} has {len(tracks)} track walls instead of two')
    for line, side in tracks:
        if not secret_door and side.get('texturemiddle') != track_for_lock.get(lock, 'DOORTRAK'):
            errors.append(f'door sector {sector_index} has the wrong keyed track texture')
        if line.get('dontpegbottom') != 'true':
            errors.append(f'door sector {sector_index} has a moving track texture')

sector_ids = {}
for index, sector in enumerate(sectors):
    if 'id' not in sector:
        continue
    sector_id = int(sector['id'])
    if sector_id in sector_ids:
        errors.append(f'sector id {sector_id} is duplicated')
    sector_ids[sector_id] = index

remote_openers = [line for line in lines if line.get('special') == '11']
switch_openers = [line for line in remote_openers
                  if line.get('playeruse') == 'true' and
                  1500 <= int(line.get('arg0', '0')) < 2000]
key_triggers = [line for line in remote_openers
                if line.get('playercross') == 'true' and
                1000 <= int(line.get('arg0', '0')) < 1500]
if not switch_openers:
    errors.append('map contains no usable switch-operated reveal area')
if not key_triggers:
    errors.append('map contains no key-platform ambush trigger')
for opener in switch_openers + key_triggers:
    target = int(opener.get('arg0', '0'))
    if target not in sector_ids:
        errors.append(f'Door_Open trigger targets missing sector id {target}')
        continue
    target_sector = sectors[sector_ids[target]]
    if target_sector['heightfloor'] != target_sector['heightceiling']:
        errors.append(f'remote door sector id {target} does not start closed')
    if opener.get('arg1') != '16':
        errors.append(f'remote door sector id {target} uses the wrong speed')
for opener in switch_openers:
    side = sides[int(opener['sidefront'])]
    if not side.get('texturemiddle', '').startswith('SW1'):
        errors.append('remote Door_Open use line is not presented as a switch')

key_border_textures = {'13': 'DOORRED', '5': 'DOORBLU', '6': 'DOORYEL'}
for key_type, border in key_border_textures.items():
    if not any(thing.get('type') == key_type for thing in things):
        continue
    border_count = sum(side.get('texturemiddle') == border for side in sides)
    if border_count < 6:
        errors.append(f'{border} appears on only {border_count} keyed-door border segments')

monster_types = {'7', '9', '16', '58', '64', '65', '66', '67', '68', '69',
                 '71', '72', '84', '88', '89', '3001', '3002', '3003', '3004',
                 '3005', '3006'}
perch_sector_ids = [sector_id for sector_id in sector_ids if 2000 <= sector_id < 3000]
if not perch_sector_ids:
    errors.append('map contains no elevated ranged-monster perch')
for sector_id in perch_sector_ids:
    sector_index = sector_ids[sector_id]
    boundary = []
    adjacent = set()
    points = set()
    for line in lines:
        line_sectors = []
        for side_name in ('sidefront', 'sideback'):
            if side_name in line:
                line_sectors.append(int(sides[int(line[side_name])]['sector']))
        if sector_index not in line_sectors:
            continue
        boundary.append(line)
        points.add(int(line['v1']))
        points.add(int(line['v2']))
        adjacent.update(candidate for candidate in line_sectors if candidate != sector_index)
    if len(boundary) != 4 or any(line.get('blockmonsters') != 'true' for line in boundary):
        errors.append(f'perch sector id {sector_id} lacks four monster-blocking edges')
    if not adjacent:
        errors.append(f'perch sector id {sector_id} has no surrounding room sector')
    else:
        surrounding_floor = min(float(sectors[index]['heightfloor']) for index in adjacent)
        if float(sectors[sector_index]['heightfloor']) - surrounding_floor < 48.0:
            errors.append(f'perch sector id {sector_id} is not meaningfully elevated')
    if points:
        xs = [float(vertices[point]['x']) for point in points]
        ys = [float(vertices[point]['y']) for point in points]
        if not any(thing.get('type') in monster_types and
                   min(xs) < float(thing['x']) < max(xs) and
                   min(ys) < float(thing['y']) < max(ys)
                   for thing in things):
            errors.append(f'perch sector id {sector_id} contains no ranged monster')

ambushers = [thing for thing in things
             if thing.get('type') in monster_types and thing.get('ambush') == 'true']
if len(ambushers) < 2:
    errors.append('key reveal closet contains fewer than two ambush monsters')

exits = [line for line in lines if line.get('special') == '243']
if len(exits) != 1 or exits[0].get('playercross') != 'true':
    errors.append('exit trigger is missing explicit player-cross activation')
if len(exits) == 1:
    exit_sector_index = int(sides[int(exits[0]['sidefront'])]['sector'])
    if sectors[exit_sector_index].get('texturefloor') != 'GATE1':
        errors.append('exit trigger is not placed on the distinctive GATE1 pad')
    exit_borders = 0
    for line in lines:
        for side_name in ('sidefront', 'sideback'):
            if side_name not in line:
                continue
            side = sides[int(line[side_name])]
            if int(side['sector']) == exit_sector_index and (
                    side.get('texturetop') == 'EXITDOOR' or
                    side.get('texturebottom') == 'EXITDOOR'):
                exit_borders += 1
                break
    if exit_borders < 4:
        errors.append(f'exit pad has only {exit_borders} EXITDOOR border segments')
if any(line.get('special') in ('1', '13') for line in lines):
    errors.append('obsolete polyobject/locked-door special remains in generated output')
sky_sectors = [sector for sector in sectors if sector.get('textureceiling') == 'F_SKY1']
if len(sky_sectors) < 2:
    errors.append(f'map has only {len(sky_sectors)} open sky sectors')
large_sky_courtyard = False
for sector_index, sector in enumerate(sectors):
    if sector.get('textureceiling') != 'F_SKY1':
        continue
    if int(sector.get('lightlevel', '0')) < 192:
        errors.append(f'sky sector {sector_index} is too dark to read as outdoors')
    points = set()
    for line in lines:
        for side_name in ('sidefront', 'sideback'):
            if side_name in line and int(sides[int(line[side_name])]['sector']) == sector_index:
                points.add(int(line['v1']))
                points.add(int(line['v2']))
    if points:
        xs = [float(vertices[point]['x']) for point in points]
        ys = [float(vertices[point]['y']) for point in points]
        if max(xs) - min(xs) >= 400.0 or max(ys) - min(ys) >= 400.0:
            large_sky_courtyard = True
if not large_sky_courtyard:
    errors.append('map has no room-scale outdoor courtyard')
if any(int(sector.get('lightlevel', '0')) < 160 for sector in sectors):
    errors.append('map contains a sector below the minimum readable light level')
if not any(sector.get('special') == '9' for sector in sectors):
    errors.append('map contains no optional secret reward sector')
if not any(line.get('special') == '12' and line.get('secret') == 'true' for line in lines):
    errors.append('map contains no wall-aligned secret door')
if lines and diagonal_lines / len(lines) < 0.12:
    errors.append('map silhouette has too few diagonal linedefs to break up the coarse grid')
clear_heights = {
    round(float(sector['heightceiling']) - float(sector['heightfloor']))
    for sector in sectors
    if float(sector['heightceiling']) > float(sector['heightfloor'])
}
if len(boundary_lengths) < 8:
    errors.append(f'room geometry has only {len(boundary_lengths)} distinct boundary lengths')
if len(clear_heights) < 6:
    errors.append(f'room geometry has only {len(clear_heights)} distinct clear heights')
starts = [thing for thing in things if thing.get('type') == '1']
shotguns = [thing for thing in things if thing.get('type') == '2001']
if len(starts) == 1:
    start = starts[0]
    sx, sy = float(start['x']), float(start['y'])
    angle = math.radians(float(start.get('angle', '0')))
    nearby = []
    for thing in shotguns:
        dx, dy = float(thing['x']) - sx, float(thing['y']) - sy
        nearby.append((math.hypot(dx, dy), dx * math.cos(angle) + dy * math.sin(angle)))
    if not any(distance <= 40.0 and forward > 0.0 for distance, forward in nearby):
        errors.append('guaranteed start shotgun is not directly ahead of the player')
if any(thing.get('type') == '64' for thing in things):
    errors.append('random Arch-Vile placement bypasses the encounter roster budget')
if any(thing.get('type') == '7' for thing in things):
    errors.append('Spider Mastermind placement exceeds the coarse-cell clearance contract')
for boss in (thing for thing in things if thing.get('type') == '16'):
    bx, by = float(boss['x']), float(boss['y'])
    clearance = min((point_segment_distance(bx, by, *wall) for wall in solid_walls), default=0.0)
    if clearance < 96.0:
        errors.append(f'Cyberdemon has only {clearance:.1f} units of wall clearance')
decoration_types = {'15', '20', '35', '41', '43', '44', '46', '48',
                    '55', '56', '57', '85', '86', '2028'}
solid_decoration_types = decoration_types - {'15', '20'}
decorations = [thing for thing in things if thing.get('type') in decoration_types]
if len(decorations) < 2:
    errors.append('map contains too few role-aware decorative things')
gameplay_things = [thing for thing in things if thing.get('type') not in decoration_types]
for decoration in decorations:
    if decoration.get('type') not in solid_decoration_types:
        continue
    x, y = float(decoration['x']), float(decoration['y'])
    if any(math.hypot(float(thing['x']) - x, float(thing['y']) - y) < 36.0
           for thing in gameplay_things):
        errors.append('solid decoration obstructs a gameplay actor or pickup')
        break

seen = set()
components = 0
for root in range(len(sectors)):
    if root in seen:
        continue
    components += 1
    seen.add(root)
    queue = [root]
    for current in queue:
        for neighbor in adjacency[current]:
            if neighbor not in seen:
                seen.add(neighbor)
                queue.append(neighbor)
if components != 1:
    errors.append(f'sector graph has {components} disconnected components')
if len(referenced) != len(sectors):
    errors.append(f'only {len(referenced)} of {len(sectors)} sectors are referenced')

for error in errors:
    print(f'    {error}')
sys.exit(1 if errors else 0)
PY
}

validate_dump() {
    local size=$1
    local theme=${2:-techbase}
    local failures=0
    local sectors things players exits locks keys monsters ammo health weapons super_shotguns
    local min_sectors max_sectors max_things min_monsters max_monsters
    local unique_walls unique_floors unique_ceilings
    sectors=$(count_blocks sector)
    things=$(count_blocks thing)
    players=$(grep -c '^\s*type = 1;' /tmp/procmap_test.udmf 2>/dev/null || true)
    exits=$(grep -c '^\s*special = 243;' /tmp/procmap_test.udmf 2>/dev/null || true)
    locks=$(grep -c '^\s*locknumber = ' /tmp/procmap_test.udmf 2>/dev/null || true)
    keys=$(grep -Ec '^\s*type = (5|6|13);' /tmp/procmap_test.udmf 2>/dev/null || true)
    monsters=$(grep -Ec '^\s*type = (7|9|16|58|64|65|66|67|68|69|71|72|84|88|89|3001|3002|3003|3004|3005|3006);' \
        /tmp/procmap_test.udmf 2>/dev/null || true)
    ammo=$(grep -Ec '^\s*type = (17|2007|2008|2010|2046|2047|2048|2049);' /tmp/procmap_test.udmf 2>/dev/null || true)
    health=$(grep -Ec '^\s*type = (2011|2012|2013|2014|2015|2018|2019);' /tmp/procmap_test.udmf 2>/dev/null || true)
    weapons=$(grep -Ec '^\s*type = (82|2001|2002|2003|2004|2005|2006);' /tmp/procmap_test.udmf 2>/dev/null || true)
    super_shotguns=$(grep -c '^\s*type = 82;' /tmp/procmap_test.udmf 2>/dev/null || true)
	unique_walls=$(sed -n 's/^\s*texturemiddle = "\([^"]*\)";/\1/p' /tmp/procmap_test.udmf |
		grep -Ev '^(DOORTRAK|DOORRED|DOORBLU|DOORYEL|STEP1)$' | sort -u | wc -l)
	unique_floors=$(sed -n 's/^\s*texturefloor = "\([^"]*\)";/\1/p' /tmp/procmap_test.udmf | sort -u | wc -l)
	unique_ceilings=$(sed -n 's/^\s*textureceiling = "\([^"]*\)";/\1/p' /tmp/procmap_test.udmf |
		grep -v '^F_SKY1$' | sort -u | wc -l)
    min_sectors=$((18 + size * 6))
    max_sectors=$((35 + size * 20))
    max_things=$((100 + size * 50))
    # Easy compact maps intentionally permit a slightly lighter opening run;
    # arena growth and the upper difficulties are covered by balance_test.
    min_monsters=$((14 + size * 6))
    max_monsters=$((35 + size * 25 + size * 6))

    if [ "$players" -ne 1 ]; then
        echo "    expected one player start, got $players"
        failures=$((failures + 1))
    fi
    if [ "$exits" -ne 1 ]; then
        echo "    expected one exit trigger, got $exits"
        failures=$((failures + 1))
    fi
    if [ "$keys" -lt 1 ] || [ "$keys" -gt 3 ]; then
        echo "    expected one to three keys, got $keys"
        failures=$((failures + 1))
    fi
    if [ "$locks" -ne $((keys * 2)) ]; then
        echo "    expected two lock linedefs per key, got locks=$locks keys=$keys"
        failures=$((failures + 1))
    fi
    if [ "$sectors" -lt "$min_sectors" ] || [ "$sectors" -gt "$max_sectors" ]; then
        echo "    sector budget out of range: $sectors (expected $min_sectors..$max_sectors)"
        failures=$((failures + 1))
    fi
    if [ "$things" -lt 30 ] || [ "$things" -gt "$max_things" ]; then
        echo "    thing budget out of range: $things (expected 30..$max_things)"
        failures=$((failures + 1))
    fi
    if [ "$monsters" -lt "$min_monsters" ] || [ "$monsters" -gt "$max_monsters" ]; then
        echo "    encounter budget out of range: $monsters (expected $min_monsters..$max_monsters)"
        failures=$((failures + 1))
    fi
    if [ $((ammo * 5)) -lt "$monsters" ]; then
        echo "    ammunition support is too sparse: ammo=$ammo monsters=$monsters"
        failures=$((failures + 1))
    fi
    if [ $((health * 5)) -lt "$monsters" ]; then
        echo "    health/armor support is too sparse: health=$health monsters=$monsters"
        failures=$((failures + 1))
    fi
    if [ "$weapons" -lt 2 ]; then
        echo "    weapon progression is missing: weapons=$weapons"
        failures=$((failures + 1))
    fi
	if [ "$size" -ge 3 ] && { [ "$unique_walls" -lt 8 ] || [ "$unique_floors" -lt 6 ] || [ "$unique_ceilings" -lt 5 ]; }; then
		echo "    room surface variation is too low: walls=$unique_walls floors=$unique_floors ceilings=$unique_ceilings"
		failures=$((failures + 1))
	fi
    if [ "$(basename "$IWAD")" = "doom2.wad" ] && [ "$super_shotguns" -lt 1 ]; then
        echo "    Doom II weapon progression is missing the super shotgun"
        failures=$((failures + 1))
    fi
    if [ "$theme" = "hell" ]; then
        if ! grep -q '^\s*type = 41;' /tmp/procmap_test.udmf ||
                ! grep -q '^\s*type = 43;' /tmp/procmap_test.udmf; then
            echo "    Hell finale/outdoor semiotics are missing their evil-eye or torch-tree marker"
            failures=$((failures + 1))
        fi
        if grep -q '^\s*type = 5;' /tmp/procmap_test.udmf &&
                ! grep -q '^\s*type = 44;' /tmp/procmap_test.udmf; then
            echo "    blue key shrine is missing its blue torch marker"
            failures=$((failures + 1))
        fi
        if grep -q '^\s*type = 13;' /tmp/procmap_test.udmf &&
                ! grep -q '^\s*type = 46;' /tmp/procmap_test.udmf; then
            echo "    red key shrine is missing its red torch marker"
            failures=$((failures + 1))
        fi
        if grep -q '^\s*type = 6;' /tmp/procmap_test.udmf &&
                ! grep -q '^\s*type = 35;' /tmp/procmap_test.udmf; then
            echo "    yellow key shrine is missing its gold candelabra marker"
            failures=$((failures + 1))
        fi
    elif [ "$(basename "$IWAD")" = "doom2.wad" ]; then
        if ! grep -q '^\s*type = 85;' /tmp/procmap_test.udmf; then
            echo "    techbase landmarks are missing their Doom II lamp language"
            failures=$((failures + 1))
        fi
    elif ! grep -Eq '^\s*type = (48|2028);' /tmp/procmap_test.udmf; then
        echo "    techbase landmarks are missing their Ultimate Doom pillar fallback"
        failures=$((failures + 1))
    fi
    if grep -q 'texturemiddle = "-"' /tmp/procmap_test.udmf; then
        echo "    explicit missing middle texture found"
        failures=$((failures + 1))
    fi
    if ! validate_geometry; then
        failures=$((failures + 1))
    fi
    return "$failures"
}

case "${1:-validate}" in
    seeds)
        shift
        if [ $# -eq 0 ]; then
            seeds=(1 42 99 123 999 12345 0 7 13 21 501721273)
        else
            seeds=("$@")
        fi
        failures=0
        index=0
        for seed in "${seeds[@]}"; do
            size=$((index % 5 + 1))
            difficulty=$(((index * 2) % 5 + 1))
            if [ $((index % 2)) -eq 0 ]; then theme=techbase; else theme=hell; fi
            if [ "$seed" -eq 501721273 ]; then
                theme=hell
                difficulty=4
                size=1
            fi
            echo "=== seed=$seed theme=$theme difficulty=$difficulty size=$size ==="
            output=$(run_test "$seed" "$theme" "$difficulty" "$size")
            echo "$output" | grep -E "Dumped UDMF|Generation failed" || true
            if ! echo "$output" | grep -q "Dumped UDMF"; then
                failures=$((failures + 1))
                index=$((index + 1))
                continue
            fi
            report_dump
            if ! validate_dump "$size" "$theme"; then
                failures=$((failures + 1))
            fi
            index=$((index + 1))
        done
        if [ "$failures" -ne 0 ]; then
            echo "Seed stress validation failed for $failures configuration(s)"
            exit 1
        fi
        echo "Seed stress validation passed for ${#seeds[@]} configurations"
        ;;
    inspect)
        shift
        seed="${1:-12345}"
        run_test "$seed" | grep -E "Dumped UDMF|Generation failed"
        report_dump
        echo "--- locknumber lines ---"
        grep -n "locknumber" /tmp/procmap_test.udmf
        echo "--- key thing lines ---"
        grep -nE '^\s*type = (5|6|13);' /tmp/procmap_test.udmf
        echo "--- exit trigger ---"
        grep -n "special = 243" /tmp/procmap_test.udmf
        ;;
    size)
        shift
        for size in 1 3 5 10 20; do
            echo "=== size=$size ==="
            run_test 12345 techbase 3 "$size" | grep -E "Dumped UDMF|Generation failed"
            report_dump
        done
        ;;
    determinism)
        run_test 424242 techbase 3 3 >/dev/null
        first=$(sha256sum /tmp/procmap_test.udmf | cut -d' ' -f1)
        run_test 424242 techbase 3 3 >/dev/null
        second=$(sha256sum /tmp/procmap_test.udmf | cut -d' ' -f1)
        run_test 424243 techbase 3 3 >/dev/null
        different=$(sha256sum /tmp/procmap_test.udmf | cut -d' ' -f1)
        if [ "$first" != "$second" ] || [ "$first" = "$different" ]; then
            echo "Determinism check failed"
            exit 1
        fi
        echo "Determinism check passed: $first"
        ;;
    menu)
        menu_dump=/tmp/procmap_menu_definition.txt
        menu_config=/tmp/procmap_menu_test.ini
        menu_log=/tmp/procmap_menu_test.log
        launch_log=/tmp/procmap_menu_launch.log
        rm -f "$menu_dump" "$menu_config" "$menu_log" "$launch_log"

        if ! unzip -p "$ROOT/build/biaseddoom.pk3" menudef.txt >"$menu_dump"; then
            echo "Could not read packed MENUDEF"
            exit 1
        fi
        required_menu_patterns=(
            'TextItem "PROCEDURAL GAME", "p", "ProceduralMapMenu"'
            'OptionMenu "ProceduralMapMenu"'
            'TextField "Seed", "procgen_seed"'
            '"procmap_randomize_seed"'
            '"procgen_theme", "ProcGenThemes"'
            '"procgen_difficulty", "ProcGenDifficulties"'
            'Slider "Map Size", "procgen_size", 1, 20, 1, 0'
            '"procmap", 1, 1'
            '"procmap random", 1, 1'
            '"procmap_restore_defaults"'
        )
        for pattern in "${required_menu_patterns[@]}"; do
            if ! grep -Fq "$pattern" "$menu_dump"; then
                echo "Packed procedural menu is missing: $pattern"
                exit 1
            fi
        done

        # MENUDEF lumps from gameplay mods may replace MainMenu after the
        # engine definition. Verify that the native post-parse reconciliation
        # remains present for both graphic and localized/text-only layouts.
        menu_source="$ROOT/src/common/menu/menudef.cpp"
        required_compat_patterns=(
            'static void EnsureProceduralMenuEntries()'
            'EnsureProceduralMenuEntry(NAME_MainMenu);'
            'EnsureProceduralMenuEntry(NAME_MainMenuTextOnly);'
            'item->mAction == action'
        )
        for pattern in "${required_compat_patterns[@]}"; do
            if ! grep -Fq "$pattern" "$menu_source"; then
                echo "Procedural menu override compatibility is missing: $pattern"
                exit 1
            fi
        done

        listmenu_source="$ROOT/wadsrc/static/zscript/engine/ui/menu/listmenu.zs"
        required_scroll_patterns=(
            'double mScrollOffset;'
            'UIEvent.Type_WheelDown'
            'case MKEY_PageDown:'
            'case MKEY_Home:'
            'case MKEY_End:'
            'EnsureSelectionVisible();'
        )
        for pattern in "${required_scroll_patterns[@]}"; do
            if ! grep -Fq "$pattern" "$listmenu_source"; then
                echo "Scrollable replacement-menu support is missing: $pattern"
                exit 1
            fi
        done

        "$BIN" -nosound -nomusic -nogui -config "$menu_config" -iwad "$IWAD" \
            +procgen_seed 123456 +procgen_theme hell +procgen_difficulty 5 +procgen_size 5 \
            +procmap_restore_defaults +quit >"$menu_log" 2>&1
        if ! grep -q 'Procedural map settings restored to defaults' "$menu_log" ||
                ! grep -q '^procgen_seed=0$' "$menu_config" ||
                ! grep -q '^procgen_theme=techbase$' "$menu_config" ||
                ! grep -q '^procgen_difficulty=3$' "$menu_config" ||
                ! grep -q '^procgen_size=3$' "$menu_config"; then
            echo "Procedural menu defaults did not persist correctly"
            exit 1
        fi

        "$BIN" -nosound -nomusic -nogui -config "$menu_config" -iwad "$IWAD" \
            +procmap_randomize_seed +quit >"$menu_log" 2>&1
        random_seed=$(sed -n 's/^procgen_seed=//p' "$menu_config")
        if ! grep -q 'Procedural map seed set to' "$menu_log" ||
                ! [[ "$random_seed" =~ ^[1-9][0-9]*$ ]]; then
            echo "Procedural menu seed randomization failed"
            exit 1
        fi

        status=0
        # Keep redirected engine output line-buffered and stop without entering
        # the interactive engine's signal-driven shutdown path.
        { timeout --signal=KILL 30 stdbuf -oL -eL "$BIN" -nosound -nomusic -nogui \
            -config "$menu_config" -iwad "$IWAD" +procgen_theme hell \
            +procgen_difficulty 4 +procgen_size 1 +procmap random >"$launch_log" 2>&1; } \
            2>/dev/null || status=$?
        if ! grep -q '^PROCMAP - ' "$launch_log"; then
            echo "Procedural menu launch command did not enter PROCMAP (exit=$status)"
            exit 1
        fi
        if grep -Eqi 'procedural map generation failed|invalid map|nodebuilder.*failed|missing texture|unknown texture' "$launch_log"; then
            echo "Procedural menu launch reported a runtime error"
            exit 1
        fi
        echo "Procedural main-menu integration passed"
        ;;
    balance)
        previous=0
        previous_area=0
        for difficulty in 1 2 3 4 5; do
            output=$(run_test 2024 techbase "$difficulty" 3)
            if ! echo "$output" | grep -q "Dumped UDMF"; then
                echo "Balance generation failed at difficulty=$difficulty"
                exit 1
            fi
            monsters=$(grep -Ec '^\s*type = (7|9|16|58|64|65|66|67|68|69|71|72|84|88|89|3001|3002|3003|3004|3005|3006);' \
                /tmp/procmap_test.udmf 2>/dev/null || true)
            ammo=$(grep -Ec '^\s*type = (17|2007|2008|2010|2046|2047|2048|2049);' /tmp/procmap_test.udmf 2>/dev/null || true)
            health=$(grep -Ec '^\s*type = (2011|2012|2013|2014|2015|2018|2019);' /tmp/procmap_test.udmf 2>/dev/null || true)
            exit_area=$(measure_exit_room_area)
            echo "difficulty=$difficulty monsters=$monsters ammo=$ammo health+armor=$health exit-area=$exit_area"
            if [ "$monsters" -lt "$previous" ]; then
                echo "Encounter pressure regressed from $previous to $monsters"
                exit 1
            fi
            if ! validate_dump 3 techbase; then
                echo "Balance validation failed at difficulty=$difficulty"
                exit 1
            fi
            if [ "$previous_area" -gt 0 ] && [ "$exit_area" -le "$previous_area" ]; then
                echo "Finale arena did not grow from $previous_area at difficulty=$difficulty (area=$exit_area)"
                exit 1
            fi
            previous=$monsters
            previous_area=$exit_area
        done
        if [ "$previous_area" -lt 500000 ]; then
            echo "Nightmare finale arena is too small: $previous_area"
            exit 1
        fi
        echo "Difficulty balance progression passed"
        ;;
    doom1)
        if [ ! -f "$RERELEASE_DOOM_IWAD" ]; then
            echo "ERROR: Ultimate Doom IWAD not found at $RERELEASE_DOOM_IWAD"
            exit 1
        fi
        IWAD="$RERELEASE_DOOM_IWAD"
        specs=(
            "2718 techbase 4 4"
            "31337 hell 5 5"
        )
        for spec in "${specs[@]}"; do
            read -r seed theme difficulty size <<<"$spec"
            output=$(run_test "$seed" "$theme" "$difficulty" "$size")
            if ! echo "$output" | grep -q "Dumped UDMF"; then
                echo "Ultimate Doom compatibility generation failed"
                exit 1
            fi
            echo "$output" | grep "Dumped UDMF"
            report_dump
            if ! validate_dump "$size" "$theme"; then
                echo "Ultimate Doom structural validation failed"
                exit 1
            fi
            if grep -Eq '^\s*type = (64|65|66|67|68|69|71|72|82|84|88|89);' /tmp/procmap_test.udmf; then
                echo "Ultimate Doom map contains a Doom II-only actor"
                exit 1
            fi
            if grep -Eq '^\s*type = (85|86);' /tmp/procmap_test.udmf; then
                echo "Ultimate Doom map contains Doom II-only tech lamps"
                exit 1
            fi
        done
        doom1_runtime_log=/tmp/procmap_doom1_runtime.log
        status=0
        { timeout --signal=KILL 30 stdbuf -oL -eL "$BIN" -nosound -nomusic -nogui \
            -iwad "$RERELEASE_DOOM_IWAD" +procgen_seed 31337 +procgen_theme hell \
            +procgen_difficulty 5 +procgen_size 5 +map PROCMAP >"$doom1_runtime_log" 2>&1; } \
            2>/dev/null || status=$?
        if ! grep -q '^PROCMAP - ' "$doom1_runtime_log"; then
            echo "Ultimate Doom runtime load failed (exit=$status)"
            grep -Ei 'error|failed|invalid|unknown|node|texture' "$doom1_runtime_log" | tail -20 || true
            exit 1
        fi
        if grep -Eqi 'procedural map generation failed|invalid map|nodebuilder.*failed|unconnected|missing texture|unknown texture' \
                "$doom1_runtime_log"; then
            echo "Ultimate Doom runtime load reported an error"
            grep -Ei 'error|failed|invalid|unknown|node|texture' "$doom1_runtime_log" | tail -20 || true
            exit 1
        fi
        echo "Ultimate Doom roster and runtime compatibility passed"
        ;;
    load)
        specs=(
            "7 techbase 2 1"
            "42 hell 3 3"
            "999 techbase 5 5"
        )
        for spec in "${specs[@]}"; do
            read -r seed theme difficulty size <<<"$spec"
            log="/tmp/procmap_runtime_load_${seed}.log"
            status=0
            { timeout --signal=KILL 30 stdbuf -oL -eL "$BIN" -nosound -nomusic -nogui -iwad "$IWAD" \
                +procgen_seed "$seed" +procgen_theme "$theme" \
                +procgen_difficulty "$difficulty" +procgen_size "$size" \
                +map PROCMAP >"$log" 2>&1; } 2>/dev/null || status=$?
            if ! grep -q '^PROCMAP - ' "$log"; then
                echo "Runtime load failed for seed=$seed theme=$theme size=$size (exit=$status)"
                grep -Ei 'error|failed|invalid|unknown|node' "$log" | tail -20 || true
                exit 1
            fi
            if grep -Eqi 'procedural map generation failed|invalid map|nodebuilder.*failed|unconnected|missing texture|unknown texture' "$log"; then
                echo "Runtime load reported an error for seed=$seed theme=$theme size=$size"
                grep -Ei 'error|failed|invalid|unknown|node' "$log" | tail -20 || true
                exit 1
            fi
            echo "Runtime load passed: seed=$seed theme=$theme size=$size"
        done
        ;;
    validate)
        failures=0
        specs=(
            "1 techbase 2 1"
            "42 hell 3 2"
            "99 techbase 3 3"
            "123 hell 4 4"
            "999 techbase 5 5"
            "20260713 hell 5 20"
        )
        for spec in "${specs[@]}"; do
            read -r seed theme difficulty size <<<"$spec"
            echo "=== seed=$seed theme=$theme difficulty=$difficulty size=$size ==="
            output=$(run_test "$seed" "$theme" "$difficulty" "$size")
            echo "$output" | grep -E "Dumped UDMF|Generation failed" || true
            if ! echo "$output" | grep -q "Dumped UDMF"; then
                failures=$((failures + 1))
                continue
            fi
            report_dump
            if ! validate_dump "$size" "$theme"; then
                failures=$((failures + 1))
            fi
        done
        if [ "$failures" -ne 0 ]; then
            echo "Validation failed for $failures configuration(s)"
            exit 1
        fi
        echo "All procedural generation validations passed"
        ;;
    udmf)
        head -100 /tmp/procmap_test.udmf
        ;;
    *)
        echo "Usage: $0 {validate|seeds|inspect|size|determinism|menu|balance|doom1|load|udmf} [args...]"
        exit 2
        ;;
esac
