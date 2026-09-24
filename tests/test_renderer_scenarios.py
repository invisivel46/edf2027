"""Renderer scenario inputs: the save seeds, the scenario table and the input scripts. No game needed."""
import contextlib
import importlib.util
import io
import json
import re
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('make_edf_save', ROOT / 'tools/make-edf-save.py')
save = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(save)
TABLE = json.loads((ROOT / 'tools/renderer-scenarios.json').read_text(encoding='utf-8'))


def u32(data, offset):
    return struct.unpack_from('>I', data, offset)[0]


def parse_events(text):
    """tools-side copy of edf::ParseInputEvents (src/scripted_input_logic.h): the
    lines it keeps, and the non-blank, non-comment lines it would drop."""
    events, dropped = [], []
    for line in text.splitlines():
        fields = line.split()
        if not fields or line.startswith('#') or fields == ['clock', 'game'] or fields == ['clock', 'wall']:
            continue
        try:
            start, duration, buttons = int(fields[0]), int(fields[1]), int(fields[2], 16)
            if not (0 <= start <= 0xFFFFFFFF and 0 <= duration <= 0xFFFFFFFF and 0 <= buttons <= 0xFFFF):
                raise ValueError
            analog = [int(v) for v in fields[3:]]
            if analog and (len(analog) != 6 or any(not 0 <= v <= 255 for v in analog[:2]) or
                           any(not -32768 <= v <= 32767 for v in analog[2:])):
                raise ValueError
        except (ValueError, IndexError):
            dropped.append(line)
            continue
        events.append((start, duration, buttons, analog or None))
    return events, dropped


def active_analog(events, ms):
    """edf::AnalogAt: the last active analog event in file order wins."""
    state = [0] * 6
    for start, duration, _, analog in events:
        if analog and start <= ms < start + duration:
            state = analog
    return state


def buttons_at(events, ms):
    value = 0
    for start, duration, buttons, _ in events:
        if start <= ms < start + duration:
            value |= buttons
    return value


class SaveSeedTests(unittest.TestCase):
    def test_container_layout(self):
        session = save.build_session(11, armour=100000)
        data = save.build_container(bytes(session))
        self.assertEqual(len(data), 7968)
        self.assertEqual(u32(data, 0), 0x010A0101)
        self.assertEqual(u32(data, 4), 7968)
        self.assertEqual(u32(data, 8), 0)                 # last slot written
        self.assertEqual(data[:288], save.HEADER_TEMPLATE)  # options and four Technical profiles
        self.assertEqual(u32(data, 12 + 16), 1)           # player 1 profile: Technical
        self.assertEqual([u32(data, 12 + 16 + 12 + 4 * i) for i in range(4)], [7, 4, 6, 5])  # RT LB LT RB
        self.assertEqual(u32(data, 288), 1)               # slot 1 occupied
        for slot in range(1, 8):
            self.assertEqual(u32(data, 288 + 960 * slot), 0)
        self.assertEqual(save.read_session(data), bytes(session))
        self.assertIsNone(save.read_session(data, 1))

    def test_new_game_defaults(self):
        block = save.new_session()
        self.assertEqual(len(block), 952)
        self.assertEqual([u32(block, 4 + 4 * i) for i in range(8)], [0, 64] * 4)
        self.assertEqual([i for i in range(200) if block[36 + 2 * i]], [0, 64, 101])
        self.assertEqual(u32(block, 440), 1)              # Normal
        self.assertEqual(u32(block, 436), 0)
        self.assertEqual(save.mission_rows(block), 1)     # only Mission 1, as a fresh profile

    def test_selected_mission_opens_mission_select_on_it(self):
        for mission in (1, 6, 9, 11, 53):
            block = save.build_session(mission)
            info = save.describe(block)
            self.assertEqual(info['mission_rows'], mission)
            self.assertEqual(info['cursor_mission'], mission)
            self.assertEqual(u32(block, 436), mission - 1)
            cleared = [m for m in range(100) if any(block[444 + 5 * m:449 + 5 * m])]
            self.assertEqual(cleared, list(range(mission - 1)))
            self.assertTrue(all(block[444 + 5 * m + 1] for m in cleared))   # on Normal
        hard = save.build_session(6, difficulty=2)
        self.assertEqual(hard[444 + 2], 1)
        self.assertEqual(hard[444 + 1], 0)
        self.assertEqual(u32(hard, 440), 2)

    def test_armour_weapons_and_limits(self):
        block = save.build_session(1, armour=1000000, all_weapons=True)
        self.assertEqual(save.describe(block)['stamina'], 1200200)
        self.assertEqual(save.describe(block)['weapons_owned'], 200)
        block = save.build_session(1, loadout=(10, 70))
        self.assertEqual([u32(block, 4 + 4 * i) for i in range(8)], [10, 70] * 4)
        self.assertTrue(block[36 + 20] and block[36 + 140])
        for bad in (dict(mission=0), dict(mission=54), dict(mission=1, difficulty=5),
                    dict(mission=1, loadout=(0, 171)), dict(mission=1, armour=-1)):
            with self.assertRaises(ValueError):
                save.build_session(**bad)
        with self.assertRaises(ValueError):
            save.build_container(b'short')

    def test_content_header_and_seed_files(self):
        header = save.CONTENT_HEADER
        self.assertEqual(len(header), 328)
        self.assertEqual(header[8:8 + 68].decode('utf-16-be'), 'Earth Defense Force 2017 Save Data')
        self.assertEqual(header[0x108:0x110], b'EDFXSave')
        self.assertEqual(u32(header, 0x140), 0x445007D3)  # the title id
        with tempfile.TemporaryDirectory() as tmp:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(save.main([tmp, '--mission', '9', '--armour', '5']), 0)
            info = json.loads(out.getvalue())
            self.assertEqual(info['mission_name'], ['M211', 'Tsugawa'])
            root = Path(tmp) / 'B13EBABEBABEBABE' / '445007D3'
            data = (root / '00000001' / 'EDFXSave' / 'EDFXSAVE.bin').read_bytes()
            self.assertEqual(u32(data, 288 + 8 + 436), 8)
            self.assertEqual((root / 'Headers' / '00000001' / 'EDFXSave.header').read_bytes(), header)


class ScenarioTableTests(unittest.TestCase):
    def test_every_scenario_has_its_input_seed_and_times(self):
        for name, entry in TABLE['scenarios'].items():
            with self.subTest(name):
                self.assertTrue((ROOT / entry['input']).is_file(), entry['input'])
                self.assertRegex(entry['mission'], r'^M\d{3}$')
                self.assertEqual(set(entry['seconds']), set(TABLE['variants']))
                self.assertGreater(entry['capture_start'], 0)
                if entry['seed'] is None:
                    continue
                args = entry['seed']
                mission = int(args[args.index('--mission') + 1])
                # The seed selects the mission the gate expects to see loaded.
                self.assertEqual(save.CAMPAIGN[mission][0], entry['mission'])
                self.assertIn(save.CAMPAIGN[mission][1], entry['description'])
                text = (ROOT / entry['input']).read_text(encoding='utf-8')
                self.assertIn(f'--mission {mission}', text)

    def test_variant_flags(self):
        variants = TABLE['variants']
        for name, variant in variants.items():
            self.assertIn('--edf_native_renderer=native', variant['args'])
            names = [a.split('=')[0] for a in variant['args']]
            self.assertEqual(len(names), len(set(names)), f'{name} passes a flag twice')
        self.assertIn('--edf_native_ab_alternate=1', variants['ab']['args'])
        self.assertIn('--edf_native_output_capture_interval=1', variants['ab']['args'])
        soak = variants['soak-ab']['args']
        interval = int(next(a for a in soak if 'capture_interval' in a).split('=')[1])
        limit = int(next(a for a in soak if 'capture_limit' in a).split('=')[1])
        self.assertEqual(interval % 2, 1, 'an odd interval alternates guest and native captures')
        self.assertLessEqual(limit, 128)                                 # the game clamps to 128
        self.assertGreaterEqual(interval * limit / 60 / 60, 20)          # 20+ minutes at 60 FPS
        for name in ('unlocked', 'soak-unlocked'):
            self.assertIn('--edf_native_frame_times=true', variants[name]['args'])
            self.assertIn('--edf_native_gpu_timings=true', variants[name]['args'])
            self.assertIn('--edf_native_unlock_framerate=true', variants[name]['args'])


class InputScriptTests(unittest.TestCase):
    def scripts(self):
        return {name: (ROOT / entry['input']).read_text(encoding='utf-8')
                for name, entry in TABLE['scenarios'].items()}

    def test_every_event_line_parses_and_uses_the_game_clock(self):
        for name, text in self.scripts().items():
            with self.subTest(name):
                events, dropped = parse_events(text)
                self.assertEqual(dropped, [])
                self.assertIn('clock game', text.splitlines())
                self.assertGreater(len(events), 10)

    def test_seeded_menu_route(self):
        for name, entry in TABLE['scenarios'].items():
            if entry['seed'] is None:
                continue
            with self.subTest(name):
                events, _ = parse_events((ROOT / entry['input']).read_text(encoding='utf-8'))
                menu = [(s, b) for s, d, b, a in events if s <= 60000]
                self.assertEqual(menu, [(20000, 0x10), (25000, 0x10)] + [(t, 0x1000) for t in range(30000, 60001, 5000)])
                # After the menus: no A, B, Start or Back, which could open or confirm a menu.
                for start, _, buttons, _ in events:
                    if start > 61000:
                        self.assertEqual(buttons & (0x1000 | 0x2000 | 0x0010 | 0x0020), 0, start)

    def test_long_runs_keep_playing(self):
        scripts = self.scripts()
        for name, until in (('cave', 1000000), ('ufo-swarm', 1000000), ('soak', 2090000), ('vehicle', 890000)):
            events, _ = parse_events(scripts[name])
            active = [ms for ms in range(250000, until, 7000) if any(active_analog(events, ms))]
            self.assertGreater(len(active), 0.9 * len(range(250000, until, 7000)), name)
        events, _ = parse_events(scripts['soak'])
        self.assertGreaterEqual(max(s + d for s, d, _, _ in events), 2100000)

    def test_vehicle_route_waits_then_walks_to_the_tank(self):
        events, _ = parse_events(self.scripts()['vehicle'])
        self.assertFalse(any(buttons_at(events, ms) or any(active_analog(events, ms))
                             for ms in range(61000, 240000, 500)))
        lt, rt, lx, ly, rx, ry = active_analog(events, 241000)
        # The tank from the spawn: 13.84 right, 4.85 forward (Mission/M211/Mission.xPath).
        self.assertAlmostEqual(lx / 32767, 13.84 / 14.665, places=3)
        self.assertAlmostEqual(ly / 32767, 4.85 / 14.665, places=3)
        self.assertEqual((rt, rx, ry), (0, 0, 0))
        walk = next(d for s, d, b, a in events if s == 240000)
        self.assertGreater(walk / 1000 * 6.6, 14.665)                     # reaches the hull at 0.11/tick
        enters = [s for s, d, b, a in events if b & 0x8000]
        self.assertEqual(len(enters), 2)                                  # enter, and leave at the end
        self.assertLess(enters[0] - 242600, 2000)


CONSOLE_COMMANDS = {
    # src/console/console.cpp (built-ins) and console_game.cpp (game commands).
    'help', 'echo', 'mark', 'exec', 'wait', 'waitmission', 'at', 'alias', 'unalias', 'set', 'get', 'reset',
    'cvarlist', 'clear', 'cheats', 'runners', 'stop', 'console', 'quit',
    'spawn', 'killall', 'god', 'ammo', 'pos', 'teleport', 'destroy', 'effects', 'timescale', 'stats',
}
GAME_COMMANDS = {'spawn', 'killall', 'ammo', 'pos', 'teleport', 'destroy', 'effects'}


def console_commands(text):
    """tools-side copy of edf::console::ParseScript for scripts without quotes: one or more
    ';'-separated commands per line, '//' or '#' starting a comment."""
    commands = []
    for line in text.splitlines():
        for part in line.split(';'):
            words = []
            for word in part.split():
                if word.startswith('#') or word.startswith('//'):
                    break
                words.append(word)
            if words:
                commands.append(words)
            if any(w.startswith('#') or w.startswith('//') for w in part.split()):
                break
    return commands


def duration_ticks(text):
    """edf::console::ParseDuration: ticks, Ns or Nms."""
    if text.endswith('ms'):
        return float(text[:-2]) * 60 / 1000
    if text.endswith('s'):
        return float(text[:-1]) * 60
    return float(text.rstrip('t'))


class ConsoleScenarioTests(unittest.TestCase):
    def test_console_scripts_parse_and_wait_for_the_mission(self):
        scenarios = {n: e for n, e in TABLE['scenarios'].items() if e.get('console')}
        self.assertEqual(set(scenarios), {'stress-ants', 'stress-collapse', 'stress-effects', 'stress-mixed',
                                          'horde-ants-100', 'horde-ants-300', 'horde-ants-600', 'horde-ants-1000',
                                          'horde-spiders-300', 'horde-hectors-100', 'horde-mixed'})
        for name, entry in scenarios.items():
            with self.subTest(name):
                path = ROOT / entry['console']
                self.assertTrue(path.is_file(), entry['console'])
                commands = console_commands(path.read_text(encoding='utf-8'))
                self.assertGreater(len(commands), 5)
                for words in commands:
                    self.assertIn(words[0], CONSOLE_COMMANDS, ' '.join(words))
                    if words[0] == 'wait':
                        self.assertGreaterEqual(duration_ticks(words[1]), 0)
                # Game commands need a mission: the script waits for it first.
                first_game = next(i for i, w in enumerate(commands) if w[0] in GAME_COMMANDS)
                waits = [i for i, w in enumerate(commands) if w[0] == 'waitmission']
                self.assertTrue(waits and waits[0] < first_game)
                # Stats and marks bracket the phases for the reports.
                self.assertTrue(any(w[0] == 'stats' for w in commands))
                self.assertGreaterEqual(sum(w[0] == 'mark' for w in commands), 3)

    def test_stress_scenarios_time_the_reports(self):
        for name, entry in TABLE['scenarios'].items():
            if not entry.get('console'):
                continue
            with self.subTest(name):
                self.assertIn('--edf_native_hook_timings=true', entry['args'])
                names = [a.split('=')[0] for a in entry['args']]
                self.assertEqual(len(names), len(set(names)))
                self.assertNotIn('--edf_exec', names)  # the runner adds it from "console"
                # With the unlocked variant: frame times and GPU timings come from the variant.
                unlocked = TABLE['variants']['unlocked']['args']
                self.assertIn('--edf_native_frame_times=true', unlocked)
                self.assertIn('--edf_native_gpu_timings=true', unlocked)


if __name__ == '__main__':
    unittest.main()
