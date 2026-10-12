#!/usr/bin/env python3
"""Headless checks for the TUI reference menu and settings."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.pi1551_oled_tui import Simulator, UI

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
        self.assertEqual(s.options["Dim time"],60); self.assertEqual(s.options["Dim level"],12)
        self.assertFalse(s.options["i2cLcdUseCBMChar"]); self.assertEqual(s.options["scrollHighlightRate"],0.20)
        self.assertFalse({"oledFont","oledScroll","Font","Scroll rate"} & s.options.keys())

    def test_sound_mode(self):
        s = Simulator(); s.set_screen("sound",True)
        self.click(s,"Duration"); self.assertEqual(s.options["Duration"],1000)
        self.click(s,"Mode"); self.assertEqual(s.items(),["..","Sound","Mode"])
        self.assertEqual(s.display_value("Mode"),"SAMPLE")
        self.click(s,"Mode"); self.assertIn("Duration",s.items())
        self.assertEqual(s.display_value("Mode"),"BUZZER")

    def test_independent_reset_path_and_folder_header(self):
        for ignore in (False,True):
            for reset_path in (False,True):
                s = Simulator(); folder = s.start_folder.children[0]; s.path.append(folder)
                s.options["Reset"] = "IGNORE" if ignore else "REACT"; s.options["Reset path"] = reset_path
                s.computer_reset()
                self.assertEqual(s.reset_count,0 if ignore else 1)
                self.assertIs(s.current_dir,s.start_folder if reset_path else folder)
        s = Simulator(); s.start_folder.name = "Very_long_directory_ending"
        with patch("tools.pi1551_oled_tui.time.monotonic",return_value=0):
            self.assertEqual(s.status()[0],"8: Very_long")
        seen = []
        for step in range(100):
            with patch("tools.pi1551_oled_tui.time.monotonic",return_value=step * .1): seen.append(s.status()[0])
        self.assertTrue(any("ending" in text for text in seen)); self.assertTrue(all(len(text) == 12 for text in seen))
        s.options["Temp"] = False
        self.assertEqual(s.status()[0],"8: Very_long_dir")
        ui = UI.__new__(UI); ui.width = 16; ui.ox = ui.oy = 0; lines = []
        ui.text = lambda y,x,text: lines.append(text)
        ui.status_line(0,s.status()[0],""); self.assertEqual(len(lines[-1]),16)
        ui.status_line(0,"8: Very_long","38C"); self.assertEqual(lines[-1],"8: Very_long 38C")

    def test_reverse_and_scroll_preview(self):
        s = Simulator(); s.set_screen("encoder",True)
        self.click(s,"Reverse"); s.move(1); self.assertEqual(s.cursor,0)
        s.move(-1); self.assertEqual(s.cursor,1)
        self.click(s,"Reverse"); s.move(-1); self.assertEqual(s.cursor,0)
        s.set_screen("display",True); self.click(s,"Scroll")
        self.assertEqual(s.scroll_prefix(),"Scroll 0.20s ")
        with patch("tools.pi1551_oled_tui.time.monotonic",return_value=0): s.update_scroll()
        seen = []
        for step in range(100):
            with patch("tools.pi1551_oled_tui.time.monotonic",return_value=step * 0.20): s.update_scroll()
            seen.append(s.scrolling(s.selected_name(),True))
        self.assertGreater(len(set(seen)),1)
        s.options["Contrast"] = 255; self.click(s,"Contrast")
        self.assertEqual(s.options["Contrast"],1)

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
