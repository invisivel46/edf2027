#!/usr/bin/env python3
"""Write an EDF 2017 save container that unlocks a campaign mission.

Automated runs start from an empty user directory (start-native-binding-
validation.ps1 makes a fresh one per run), so only Mission 1 is reachable. A
seed directory from this tool, passed as -SaveSeed, is copied into that user
directory before launch; the title screen then opens on Continue, the load
screen on slot 1, and mission select on the chosen mission.

Layout. Every number below is recorded, with its guest evidence, in the
analysis workspace (D:/roms2/edf2027-analysis):
  container  7968 bytes: a 288-byte header then eight 960-byte slots
             (sub_82177890..sub_82178020; sub_820C56A8 counts occupied slots by
             the word at 288 + 960*i; notes/menu/0458, 0640)
  header     +0 u32 0x010A0101 (0x82579B90), +4 u32 7968, +8 u32 last slot,
             +12 the 272-byte options/controller payload
  slot       +0 u32 occupied, +8 the 952-byte clGameStatusManager block
             (edf2017-native/src/game/save_storage.cpp)
  block      (offsets into the 952 bytes = manager offset - 32; save.h,
             notes/combat/0321, 0325)
               +0    u32 armour crates (stamina = 200 + 1.2 * n)
               +4    8 x u32 loadout, player*2 + slot
               +36   200 x [u8 owned][u8 equipped]
               +436  u32 current mission (the mission-select cursor)
               +440  u32 difficulty (0 Easy .. 4 Inferno; 1 Normal)
               +444  100 x 5 bytes, one clear flag per difficulty
               +944  u64 play time in seconds
  new game   sub_820A3200 grants weapons 0 (AF14), 64 (Stingray) and 101
             (hand grenade) and equips 0/64 for all four players
  unlock     mission select lists (highest cleared mission + 1) + 1 rows
             (sub_820A2948) and opens its cursor on the current mission
             (clamped), so clearing 1..N-1 and selecting N opens on N.
The header template (0x000..0x11F) and the content header are the bytes the
game and the kernel wrote into a fresh user directory on 2026-09-21
(out/native-bridge-run/binding-validation-20260921-164822-1c72a91f/user): the
game's default options and Technical controller profiles, and the XContent
header for "EDFXSave".

Big-endian throughout. Standard library only.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

CONTAINER_BYTES = 7968
HEADER_BYTES = 288
SLOT_BYTES = 960
SLOT_COUNT = 8
SESSION_BYTES = 952
SESSION_IN_SLOT = 8
MISSION_SLOTS = 100
DIFFICULTIES = ('easy', 'normal', 'hard', 'hardest', 'inferno')
CAMPAIGN_MISSIONS = 53  # Mission/Text.Sgo and Mission/ReplaceList.Sgo entries
WEAPON_SLOTS = 200
WEAPONS_ON_DISC = 171   # WeaponTable.Sgo records; the block has room for 200

OFF_ARMOUR = 0
OFF_LOADOUT = 4
OFF_WEAPONS = 36
OFF_MISSION = 436
OFF_DIFFICULTY = 440
OFF_RECORDS = 444
OFF_PLAY_TIME = 944

# Where the kernel keeps the save for the default profile and this title.
PROFILE = 'B13EBABEBABEBABE'
TITLE = '445007D3'
SAVE_PATH = Path(PROFILE, TITLE, '00000001', 'EDFXSave', 'EDFXSAVE.bin')
CONTENT_HEADER_PATH = Path(PROFILE, TITLE, 'Headers', '00000001', 'EDFXSave.header')

HEADER_TEMPLATE = bytes.fromhex(
    '010a010100001f20000000003f3333333f3333333f80000001000000000000010000000000000000000000070000'
    '0004000000060000000500000002000000030000000800000000000000010000000400000005010100003f000000'
    '0000000100000000000000000000000700000004000000060000000500000002000000030000000800000000000000'
    '010000000400000005010100003f00000000000001000000000000000000000007000000040000000600000005000000'
    '02000000030000000800000000000000010000000400000005010100003f000000000000010000000000000000000000'
    '07000000040000000600000005000000020000000300000008000000000000000100000004000000050101'
    '00003f00000000000000')

CONTENT_HEADER = (
    bytes.fromhex('0000000100000001')
    + 'Earth Defense Force 2017 Save Data'.encode('utf-16-be').ljust(0x100, b'\0')
    + b'EDFXSave'.ljust(0x2C, b'\0')
    + bytes.fromhex('1102000000000000000000004450' '07d311020000'))

# The campaign order (Mission/ReplaceList.Sgo) with what each mission loads,
# from the shipped scripts: map (Save "MapNN"), vehicles placed for the
# player (native203) and the largest enemy groups. Maps: Map01 City 1, Map02
# City 2, Map03 Residential, Map04 Ruins, Map05 Mountains, Map07 Cave 1,
# Map08 Cave 2 (VersusMenu_MapN).
CAMPAIGN = {
    1: ('M202', 'Arrival'), 2: ('M203', 'Invasion'), 3: ('M205', 'Melee'), 4: ('M204', 'Landing'),
    5: ('M221', 'Search'), 6: ('M212', 'Airforce'), 7: ('M100', 'Takedown'), 8: ('M206', 'Mobilization'),
    9: ('M211', 'Tsugawa'), 10: ('M210', 'Arms'), 11: ('M301', 'Infiltration'), 12: ('M300', 'Reinforcement'),
    13: ('M302', 'Retaliation'), 14: ('M217', 'Fortress'), 15: ('M219', 'Blockade'),
    16: ('M317', 'Multiplication'), 17: ('M311', 'Nesting'), 18: ('M213', 'Dino-mech'), 19: ('M305', 'Umbra'),
    20: ('M326', 'Artillery'), 21: ('M307', 'Crimson'),
}


def put_u32(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into('>I', buf, offset, value)


def new_session() -> bytearray:
    """The new-game block: sub_820A2748's defaults plus sub_820A3200's grants."""
    block = bytearray(SESSION_BYTES)
    for player in range(4):
        put_u32(block, OFF_LOADOUT + (player * 2 + 0) * 4, 0)
        put_u32(block, OFF_LOADOUT + (player * 2 + 1) * 4, 64)
    for weapon in (0, 64, 101):
        block[OFF_WEAPONS + weapon * 2] = 1
    put_u32(block, OFF_DIFFICULTY, 1)
    return block


def build_session(mission: int, difficulty: int = 1, armour: int = 0, all_weapons: bool = False,
                  loadout: tuple[int, int] | None = None, play_time_s: int = 0) -> bytearray:
    """A block with campaign missions 1..mission-1 cleared on `difficulty` and
    mission `mission` (1-based) selected."""
    if not 1 <= mission <= CAMPAIGN_MISSIONS:
        raise ValueError(f'mission must be 1..{CAMPAIGN_MISSIONS}, got {mission}')
    if not 0 <= difficulty < len(DIFFICULTIES):
        raise ValueError(f'difficulty must be 0..{len(DIFFICULTIES) - 1}, got {difficulty}')
    if not 0 <= armour <= 0xFFFFFFFF:
        raise ValueError('armour must fit a u32')
    block = new_session()
    put_u32(block, OFF_ARMOUR, armour)
    put_u32(block, OFF_DIFFICULTY, difficulty)
    put_u32(block, OFF_MISSION, mission - 1)
    for cleared in range(mission - 1):
        block[OFF_RECORDS + cleared * 5 + difficulty] = 1
    if all_weapons:  # the console's unlockweapon: owned=1, equipped=0 for all 200
        for weapon in range(WEAPON_SLOTS):
            block[OFF_WEAPONS + weapon * 2] = 1
            block[OFF_WEAPONS + weapon * 2 + 1] = 0
    if loadout is not None:
        for weapon in loadout:
            if not 0 <= weapon < WEAPONS_ON_DISC:
                raise ValueError(f'weapon index {weapon} is not on the disc (0..{WEAPONS_ON_DISC - 1})')
            block[OFF_WEAPONS + weapon * 2] = 1
        for player in range(4):
            for slot, weapon in enumerate(loadout):
                put_u32(block, OFF_LOADOUT + (player * 2 + slot) * 4, weapon)
    struct.pack_into('>Q', block, OFF_PLAY_TIME, play_time_s)
    return block


def build_container(session: bytes, slot: int = 0) -> bytes:
    if len(session) != SESSION_BYTES:
        raise ValueError(f'a session block is {SESSION_BYTES} bytes, got {len(session)}')
    if not 0 <= slot < SLOT_COUNT:
        raise ValueError(f'slot must be 0..{SLOT_COUNT - 1}')
    data = bytearray(CONTAINER_BYTES)
    data[:HEADER_BYTES] = HEADER_TEMPLATE
    put_u32(data, 8, slot)
    base = HEADER_BYTES + slot * SLOT_BYTES
    put_u32(data, base, 1)
    data[base + SESSION_IN_SLOT:base + SLOT_BYTES] = session
    return bytes(data)


def read_session(container: bytes, slot: int = 0) -> bytes | None:
    base = HEADER_BYTES + slot * SLOT_BYTES
    if struct.unpack_from('>I', container, base)[0] == 0:
        return None
    return container[base + SESSION_IN_SLOT:base + SLOT_BYTES]


def mission_rows(session: bytes, missions: int = CAMPAIGN_MISSIONS) -> int:
    """Rows mission select offers: highest mission cleared on any difficulty,
    plus one, clamped to the campaign (sub_820A2948 and its caller)."""
    highest = 0
    for mission in range(min(missions, MISSION_SLOTS)):
        if any(session[OFF_RECORDS + mission * 5:OFF_RECORDS + mission * 5 + 5]):
            highest = mission + 1
    return min(missions, highest + 1)


def describe(session: bytes) -> dict:
    mission = struct.unpack_from('>I', session, OFF_MISSION)[0]
    rows = mission_rows(session)
    return dict(armour=struct.unpack_from('>I', session, OFF_ARMOUR)[0],
                stamina=int(200 + struct.unpack_from('>I', session, OFF_ARMOUR)[0] * 1.2),
                difficulty=DIFFICULTIES[struct.unpack_from('>I', session, OFF_DIFFICULTY)[0]],
                selected_mission=mission + 1, mission_rows=rows,
                cursor_mission=min(mission, rows - 1) + 1,
                weapons_owned=sum(1 for i in range(WEAPON_SLOTS) if session[OFF_WEAPONS + i * 2]),
                loadout=[struct.unpack_from('>I', session, OFF_LOADOUT + i * 4)[0] for i in range(2)])


def write_seed(root: Path, container: bytes) -> list[Path]:
    """Write the save and its content header under a user-data root."""
    written = []
    for relative, payload in ((SAVE_PATH, container), (CONTENT_HEADER_PATH, CONTENT_HEADER)):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        written.append(path)
    return written


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('out', type=Path, help='seed directory (a user-data root; pass it to -SaveSeed)')
    ap.add_argument('--mission', type=int, required=True,
                    help='campaign mission number to select, 1-based (1 Arrival .. 53 Starship)')
    ap.add_argument('--difficulty', choices=DIFFICULTIES, default='normal')
    ap.add_argument('--armour', type=int, default=0,
                    help='armour crates; stamina is 200 + 1.2 per crate (keeps long runs alive)')
    ap.add_argument('--all-weapons', action='store_true', help='own all 200 weapon records (the console command)')
    ap.add_argument('--loadout', type=int, nargs=2, metavar=('SLOT1', 'SLOT2'),
                    help='weapon indices to equip in both slots for every player (default 0 64)')
    args = ap.parse_args(argv)
    session = build_session(args.mission, DIFFICULTIES.index(args.difficulty), args.armour,
                            args.all_weapons, tuple(args.loadout) if args.loadout else None)
    paths = write_seed(args.out, build_container(bytes(session)))
    info = describe(session)
    info['mission_name'] = CAMPAIGN.get(args.mission, ('?', '?'))
    info['files'] = [str(p) for p in paths]
    print(json.dumps(info, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
