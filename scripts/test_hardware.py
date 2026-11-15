"""Verify RGB565 routing against the Waveshare LCD interface pinout."""

from pathlib import Path
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[1]


class DisplayRoutingTests(unittest.TestCase):
    def test_primary_colours_reach_the_correct_panel_channels(self):
        display = yaml.safe_load((ROOT / "esphome/hardware.yaml").read_text())["display"][0]
        pins = display["data_pins"]
        if isinstance(pins, dict):
            # MIPI RGB's named-pin schema generates big-endian bus ordering.
            pins = pins["blue"] + pins["green"] + pins["red"]
            pins = pins[8:] + pins[:8]
        pins = [int(pin.removeprefix("GPIO")) for pin in pins]
        # Manufacturer's panel signals, least significant connected bit first.
        channels = ([1, 2, 42, 41, 40], [39, 0, 45, 48, 47, 21], [14, 38, 18, 17, 10])
        byte_order = {"big_endian": "big", "little_endian": "little"}[display["byte_order"]]
        for colour, word, expected in (
            ("red", 0xF800, [31, 0, 0]),
            ("green", 0x07E0, [0, 63, 0]),
            ("blue", 0x001F, [0, 0, 31]),
            ("white", 0xFFFF, [31, 63, 31]),
        ):
            with self.subTest(colour=colour):
                bus = int.from_bytes(word.to_bytes(2, byte_order), "little")
                driven = {pin: (bus >> bit) & 1 for bit, pin in enumerate(pins)}
                actual = [sum(driven[pin] << bit for bit, pin in enumerate(channel)) for channel in channels]
                self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
