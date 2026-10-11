#!/usr/bin/env python3
"""Headless checks for the TUI reference menu and settings."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.pi1551_oled_tui import Simulator

class TuiControls(unittest.TestCase):
    def click(self, sim, label):
        sim.cursor = sim.items().index(label)
        sim.activate()

    def test_display_shared_options(self):
        s = Simulator(); s.set_screen("settings",True); self.click(s,"Display")
        self.assertEqual(s.screen,"display")
        self.assertNotIn("OLED",s.GROUPS["settings"])
        for label in ("Temp","Dim time","Dim level","Font","Scroll"): self.click(s,label)
        self.assertFalse(s.options["Temp"])
        self.assertEqual(s.options["Dim time"],60); self.assertEqual(s.options["Dim level"],16)
        self.assertFalse(s.options["i2cLcdUseCBMChar"]); self.assertEqual(s.options["scrollHighlightRate"],0.20)
        self.assertFalse({"oledFont","oledScroll","Font","Scroll rate"} & s.options.keys())

    def test_sound_mode(self):
        s = Simulator(); s.set_screen("sound",True)
        self.click(s,"Duration"); self.assertEqual(s.options["Duration"],1000)
        self.click(s,"Mode"); self.assertEqual(s.items(),["..","Sound","Mode"])
        self.assertEqual(s.display_value("Mode"),"SAMPLE")
        self.click(s,"Mode"); self.assertIn("Duration",s.items())
        self.assertEqual(s.display_value("Mode"),"BUZZER")

    def test_new_image_format(self):
        s = Simulator(); s.set_screen("disk",True); count = len(s.current_dir.children)
        self.click(s,"New image"); self.assertEqual(s.screen,"new_image"); self.assertEqual(s.cursor,1)
        self.assertEqual(len(s.current_dir.children),count)
        self.click(s,"Type"); self.click(s,"Create image")
        self.assertEqual(s.current_dir.children[-1].name,"NEWIMAGE1.g64")
        self.assertIsNone(s.active_media)
        self.click(s,"Create image"); self.assertEqual(s.current_dir.children[-1].name,"NEWIMAGE2.g64")
        s.go_back(); self.assertEqual(s.screen,"disk")

    def test_scroll_interval_reaches_tail(self):
        s = Simulator(); s.screen = "rom"; s.cursor = 2; s.ROMS = ["short.bin","very long dos filename ending.bin"]
        with patch("tools.pi1551_oled_tui.time.monotonic",return_value=0): s.update_scroll()
        seen = []
        for step in range(100):
            with patch("tools.pi1551_oled_tui.time.monotonic",return_value=step * 0.10): s.update_scroll()
            seen.append(s.scrolling(s.ROMS[1],True))
        self.assertTrue(any("ending.bin" in value for value in seen))

if __name__ == "__main__": unittest.main()
