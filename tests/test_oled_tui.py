#!/usr/bin/env python3
"""Headless checks for the TUI reference menu and settings."""
from pathlib import Path
from datetime import datetime
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.pi1551_oled_tui import Simulator, UI

class TuiControls(unittest.TestCase):
    def click(self, sim, label):
        sim.cursor = sim.items().index(label)
        sim.activate()

    def test_status_shows_local_clock(self):
        s = Simulator(); s.set_screen("status")
        self.assertEqual(s.status_rows()[1:4], ["RTC NO", "DATE 2026-11-01", "TIME 12:00:00"])
        s.rtc_present = True
        self.assertEqual(s.status_rows()[1:4], ["RTC YES", "DATE 2026-11-01", "TIME 12:00:00"])
        s.set_local_clock(datetime(2027, 3, 4, 18, 5, 6))
        self.assertEqual(s.status_rows()[1:4], ["RTC YES", "DATE 2027-03-04", "TIME 18:05:06"])
        s.clock_base = None
        self.assertEqual(s.status_rows()[1:4], ["RTC YES", "DATE ---- -- --", "TIME --:--:--"])

    def test_date_time_menu_edits_local_clock(self):
        s = Simulator(); s.set_screen("settings", True); self.click(s, "Date & time")
        self.assertEqual(s.screen, "date_time")
        self.assertNotIn("Save to RTC", s.items())
        self.assertNotIn("Load from RTC", s.items())
        self.click(s, "Year"); s.move(1)
        self.assertEqual(s.display_value("Year"), "2027")
        s.go_back(); self.assertEqual(s.local_clock().year, 2026)
        self.click(s, "Year"); s.move(1); s.activate()
        self.assertEqual(s.local_clock().year, 2027)
        s.rtc_present = True
        self.assertIn("Load from RTC", s.items())
        self.click(s, "Save to RTC"); self.assertEqual(s.rtc_value.year, 2027)
        s.set_local_clock(datetime(2028, 1, 2, 3, 4))
        self.click(s, "Load from RTC"); self.assertEqual(s.local_clock().year, 2027)

    def test_status_rows_render_in_tui(self):
        s = Simulator(); s.set_screen("status")
        ui = UI.__new__(UI); ui.sim = s
        ui.s = type("ScreenStub", (), {"getmaxyx": lambda self: (15, 70)})()
        ui.text = lambda *args: None
        ui.frame = lambda: True
        ui.status_line = lambda *args: None
        ui.tree = lambda: None
        rendered = []
        ui.line = lambda row, value, *args: rendered.append((row, value))
        with patch("tools.pi1551_oled_tui.curses.color_pair", return_value=0): ui.draw()
        self.assertEqual([(row, value) for row, value in rendered if 3 <= row <= 5],
                         [(3, "RTC NO"), (4, "DATE 2026-11-01"), (5, "TIME 12:00:00")])
    def test_display_shared_options(self):
        s = Simulator(); s.set_screen("settings",True); self.click(s,"Display")
        self.assertEqual(s.screen,"display")
        self.assertNotIn("OLED",s.GROUPS["settings"])
        s.options["Dim time"] = 10; s.options["Dim level"] = 10
        for label in ("Temp","Dim time","Dim level","Font","Scroll"): self.click(s,label)
        self.assertFalse(s.options["Temp"])
        self.assertEqual(s.options["Dim time"],16); self.assertEqual(s.options["Dim level"],16)
        self.assertFalse(s.options["i2cLcdUseCBMChar"]); self.assertEqual(s.options["scrollHighlightRate"],0.20)
        self.assertFalse({"oledFont","oledScroll","Font","Scroll rate"} & s.options.keys())

    def test_sound_mode(self):
        s = Simulator(); s.set_screen("sound",True)
        s.options["Duration"] = 500
        self.click(s,"Duration"); self.assertEqual(s.options["Duration"],1000)
        self.assertIn("preview 1s",s.message)
        self.click(s,"Mode"); self.assertEqual(s.items(),["..","Sound","Mode"])
        self.assertEqual(s.display_value("Mode"),"SAMPLE")
        self.click(s,"Mode"); self.assertIn("Duration",s.items())
        self.assertEqual(s.display_value("Mode"),"BUZZER")

    def test_tape_motor_and_autoplay_matrix(self):
        fixture = Simulator()
        tape = fixture.sd.children[0].children[3].children[0]
        for motor_always in (False, True):
            for autoplay in (False, True):
                s = Simulator(); s.set_screen("tape")
                s.options["Motor override"] = motor_always
                s.options["Tape autoplay"] = autoplay
                s.tape_motor = False
                s.load_tape(tape)
                self.assertEqual(s.tape_playing, autoplay)
                self.assertEqual(s.display_value("MOTOR"), "ON" if motor_always else "OFF")
                self.assertIn("Stop" if autoplay else "Play", s.items())
                self.assertNotIn("Play" if autoplay else "Stop", s.items())
                for action in ("Go to file...", "Eject TAP", "Rewind", "Go to counter", "Record"):
                    if autoplay: self.assertNotIn(action, s.items())
                s.tape_motor = True
                self.assertEqual(s.display_value("MOTOR"), "ON")
                if not autoplay:
                    self.click(s, "Play")
                    self.assertTrue(s.tape_playing)
                self.click(s, "Stop")
                self.assertFalse(s.tape_playing)
                self.assertEqual(s.display_value("PLAY"), "OFF")
                s.set_screen("tape_settings")
                self.assertEqual(s.display_value("Motor ON"), "ALWAYS" if motor_always else "COMP.")
                self.assertEqual(s.display_value("Autoplay"), "ON" if autoplay else "OFF")

    def test_tape_play_stop_are_rendered_between_load_and_eject(self):
        tape = Simulator().sd.children[0].children[3].children[0]
        for autoplay, expected in ((False, "Play"), (True, "Stop")):
            s = Simulator(); s.set_screen("tape")
            s.options["Tape autoplay"] = autoplay
            s.load_tape(tape)
            ui = UI.__new__(UI); ui.sim = s
            ui.s = type("ScreenStub", (), {"getmaxyx": lambda self: (15, 70)})()
            ui.text = lambda *args: None
            ui.frame = lambda: True
            ui.status_line = lambda *args: None
            ui.tree = lambda: None
            rendered = []
            ui.line = lambda row, value, *args: rendered.append((row, value))
            with patch("tools.pi1551_oled_tui.curses.color_pair", return_value=0): ui.draw()
            rows = [value for row, value in rendered if row >= 2]
            self.assertTrue(rows[1].startswith("1:" + tape.name[:13]))
            self.assertEqual(rows[2:5], [expected] if autoplay else [expected, "Record", "Eject TAP"])
            self.assertEqual(rows[0], "..")

    def test_tape_record_menu_and_transport_stop_on_seek(self):
        s = Simulator(); s.set_screen("tape")
        self.click(s, "Record")
        self.assertEqual(s.items(), ["..", "New TAP"])
        self.click(s, "New TAP")
        self.assertEqual(s.tape.name, "NEWTAPE1.tap")
        self.assertTrue(s.tape_recording and s.tape_playing)
        self.assertEqual(s.items(), ["..", "TAP FILE", "Stop"])
        self.click(s, "Stop")
        self.click(s, "Go to counter"); self.click(s, "TARGET")
        self.assertFalse(s.tape_recording or s.tape_playing)
        self.click(s, "Record")
        self.assertIn("Current TAP", s.items())
        self.click(s, "Current TAP")
        self.click(s, "Stop")
        self.click(s, "Rewind")
        self.assertFalse(s.tape_recording or s.tape_playing)

    def test_record_motor_off_and_always_manual_stop(self):
        s = Simulator(); s.set_screen("tape")
        s.options["Motor override"] = False
        s.toggle_motor()
        self.click(s, "Record"); self.click(s, "New TAP")
        self.assertTrue(s.tape_recording)
        s.toggle_motor()
        self.assertTrue(s.tape_recording)
        s.record_motor_off_since -= 0.4; s.tick()
        self.assertTrue(s.tape_recording)
        s.toggle_motor(); s.tick()
        self.assertTrue(s.tape_recording)
        s.toggle_motor()
        s.record_motor_off_since -= 0.6; s.tick()
        self.assertFalse(s.tape_recording or s.tape_playing)
        s.options["Motor override"] = True
        self.click(s, "Record"); self.click(s, "Current TAP")
        s.toggle_motor()
        self.assertTrue(s.tape_recording)
        self.click(s, "Stop")
        self.assertFalse(s.tape_recording)

    def test_tape_seek_stops_at_tape_specific_counter_maximum(self):
        s = Simulator(); s.set_screen("tape")
        tape = s.sd.children[0].children[3].children[0]
        s.load_tape(tape); self.click(s, "Stop"); self.click(s, "Go to counter")
        s.move(500)
        self.assertEqual(s.seek_target, tape.counter_max)
        self.assertEqual(s.display_value("TARGET"), f"{tape.counter_max:03d}/{tape.counter_max:03d}")
        self.click(s, "TARGET")
        self.assertEqual(s.tape_pos, tape.counter_max)

    def test_tape_index_in_browser_and_tape_menu(self):
        s = Simulator(); tape_dir = s.start_folder.children[3]
        tape, index, no_index = tape_dir.children
        s.path.append(tape_dir); s.set_screen("browser")
        self.assertIn(index, s.browser_items())
        s.cursor = s.browser_items().index(index) + 1
        s.activate()
        self.assertIs(s.tape, tape)
        self.assertEqual(s.screen, "tape_index")
        self.assertFalse(s.tape_playing)
        self.assertEqual(s.items(), ["..", "INTRO", "EVENT 1", "EVENT 2"])
        self.click(s, "EVENT 1")
        self.assertEqual(s.screen, "tape")
        self.assertEqual(s.items()[2:5], ["Play", "Go to file...", "Record"])
        self.assertEqual((s.tape_pos, s.last_seek_offset, s.tape_playing), (48, 0x4000, False))
        self.click(s, "Go to file...")
        self.click(s, "EVENT 2")
        self.assertEqual((s.tape_pos, s.last_seek_offset), (105, 0x9000))
        self.click(s, "Eject TAP")
        self.assertNotIn("Go to file...", s.items())
        s.load_tape(no_index); s.set_screen("tape")
        self.assertNotIn("Go to file...", s.items())
        s.load_tape(tape)
        self.click(s, "Stop")
        self.assertIn("Go to file...", s.items())
        s.load_index(index)
        s.go_back()
        self.assertIn("Go to file...", s.items())
        self.click(s, "Record"); self.click(s, "Current TAP")
        self.assertNotIn("Go to file...", s.items())

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
            self.assertEqual(s.status()[0],"8:Very_long_")
        seen = []
        for step in range(100):
            with patch("tools.pi1551_oled_tui.time.monotonic",return_value=step * .1): seen.append(s.status()[0])
        self.assertTrue(any("ending" in text for text in seen)); self.assertTrue(all(len(text) == 12 for text in seen))
        s.options["Temp"] = False
        self.assertEqual(s.status()[0],"8:Very_long_dire")
        ui = UI.__new__(UI); ui.width = 16; ui.ox = ui.oy = 0; lines = []
        ui.text = lambda y,x,text: lines.append(text)
        ui.status_line(0,s.status()[0],""); self.assertEqual(len(lines[-1]),16)
        ui.status_line(0,"8:Very_long_","38C"); self.assertEqual(lines[-1],"8:Very_long_ 38C")

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
        self.assertEqual(s.options["Contrast"],16)

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
