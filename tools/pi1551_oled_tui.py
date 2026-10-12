#!/usr/bin/env python3
"""Independent keyboard TUI simulator for the proposed Pi1551 OLED UI."""
from __future__ import annotations
import curses
import time
from dataclasses import dataclass, field
from typing import Optional

@dataclass
class Node:
    name: str
    kind: str = "file"
    children: list["Node"] = field(default_factory=list)
    detail: str = ""
    size: str = ""

def fake_sd() -> Node:
    games = Node("games", "dir", [
        Node("Green Beret C16.d64", "disk", detail="35 TRACKS"),
        Node("Ghosts n Goblins.d64", "disk", detail="35 TRACKS"),
        Node("Death Sector.g64", "disk", detail="G64 IMAGE"),
        Node("A_Very_Long_Disk_Image_Name_For_Scroll_Test_2026.d64", "disk", detail="35 TRACKS"),
        Node("Multidisk Collection.d71", "browser_image", [Node("DISK 1"), Node("DISK 2")], detail="D71 BROWSER"),
    ])
    demos = Node("demos", "dir", [
        Node("Pi1551 Demo Disk.d64", "disk", detail="35 TRACKS"),
        Node("Fastloader Test.prg", "browser_image", [Node("FASTLOADER.PRG")], detail="PRG BROWSER"),
        Node("GEOS Work.d81", "browser_image", [Node("GEOS", "dir"), Node("DESKTOP", "dir")], detail="D81 BROWSER"),
        Node("Archive.d80", "browser_image", [Node("ARCHIVE")], detail="D80 BROWSER"),
        Node("Archive.d82", "browser_image", [Node("ARCHIVE")], detail="D82 BROWSER"),
    ])
    lists = Node("lists", "dir", [
        Node("favorites.lst", "list", [games.children[0], games.children[2], demos.children[0]], detail="3 DISKS"),
        Node("autoswap.lst", "list", [games.children[1], games.children[3]], detail="2 DISKS"),
    ])
    return Node("/", "dir", [
        Node("1551", "dir", [games, demos, lists,
            Node("tap", "dir", [Node("Winter Games.tap", "tape"), Node("Boulder Dash.tap", "tape")]),
            Node("options-pi1551.txt", "config"),
        ]),
        Node("roms", "dir", [Node("dos1551.bin"), Node("dos1551-ram.bin"), Node("super_dos_1551.rom"), Node("custom_fastload.bin")]),
        Node("chargen.bin", "font"), Node("README.TXT"),
    ])

def sorted_nodes(nodes: list[Node]) -> list[Node]:
    return sorted(nodes, key=lambda n: (n.kind not in {"dir", "browser_image"}, n.name.lower()))

def clip(value: str, width: int) -> str:
    return value.replace("\n", " ")[:max(0, width)]

class Simulator:
    # The ROM selector models the ordered ROM1/ROM2/... entries from
    # options.txt. It is deliberately independent of the SD Browser's roms/
    # directory.
    ROMS = ["dos1551.bin", "dos1551-ram.bin", "super_dos_1551.rom", "custom_fastload.bin"]
    GROUPS = {
        "menu": ["..", "Browser", "Tape", "Disk", "Settings", "Status", "Reboot Pi1551"],
        "reboot_confirm": ["..", "Cancel", "Reboot Pi1551?"],
        "settings": ["..", "ROM", "Drive", "Tape", "Browser", "Display", "Encoder", "Sound", "Load settings", "Save settings"],
        "drive": ["..", "Device number", "Idle trap", "RAMBOard", "Skip spinup", "Reset"],
        "sound": ["..", "Sound", "Mode", "Freq", "Duration"],
        "tape_settings": ["..", "Motor ON"],
        "browser_settings": ["..", "Auto mount", "Reset path", "Lowercase", "SD2IEC"],
        "display": ["..", "Temp", "Contrast", "Dimming", "Dim time", "Dim level", "Orient", "Font", "Scroll"],
        "encoder": ["..", "Reverse"],
        "disk": ["..", "WProt", "Write image", "New image", "Reset drive"],
        "new_image": ["..", "Type", "Create image"],
        "status": ["..", "TEMP", "SD", "FREE", "FIRMWARE", "DOS", "IMAGE", "TAPE", "LED", "MOTOR", "PLAY", "TRACK"],
        "tape": ["..", "TAP FILE", "Load TAP...", "Eject TAP", "Rewind", "Go to counter"],
        "tape_seek": ["..", "TARGET"],
    }
    def __init__(self) -> None:
        self.sd = fake_sd(); self.start_folder = self.sd.children[0]
        self.path = [self.sd, self.start_folder]; self.browser_source = self.start_folder
        self.browser_filter: Optional[str] = None; self.screen = "browser"; self.previous_screen = "browser"
        self.cursor = 0; self.offset = 0; self.stack: list[tuple[str, int, int]] = []
        self.browser_return_state: Optional[tuple[str, int, int]] = None
        self.message = "Browser — Enter menu, i/b browser"
        self.flip_mode = "solo"
        self.fliplist: list[Node] = []; self.disk: Optional[Node] = None; self.active_media: Optional[Node] = None
        self.last_flip_media: Optional[Node] = None
        self.tape: Optional[Node] = None; self.tape_pos = 0; self.tape_motor = False
        self.seek_target = 0
        self.rom = self.ROMS[0]; self.device = 8; self.write_protect = False
        self.reset_count = 0; self.track = 18.0; self.activity = "browser"
        self.temperature = 38; self.sd_size = "8GB"; self.sd_free = "6.4GB"; self.firmware = "0.3.0"
        self.options = {"Idle trap": True, "RAMBOard": False, "Skip spinup": False, "Motor override": True,
                        "Auto mount": False, "Reset path": True, "Lowercase names": False,
                        "SD2IEC": True, "Contrast": 255, "Dimming": True, "Orientation": "normal",
                        "scrollHighlightRate": 0.10, "i2cLcdUseCBMChar": True, "Encoder enabled": True, "Encoder reverse": False,
                        "Temp": True, "Dim time": 30, "Dim level": 10, "soundOnGPIO": True, "Duration": 500, "NewDiskType": "D64",
                        "Sound": True, "Freq": "1600 Hz", "Reset": "REACT"}
        self.reset_cursor()
    @property
    def current_dir(self) -> Node: return self.path[-1]
    def reset_cursor(self) -> None:
        self.cursor = 0; self.offset = 0; self.scroll_key = ""; self.scroll_pos = 0; self.scroll_dir = 1; self.scroll_pause = 7
        self.scroll_clock = time.monotonic()
        # Keep Menu... visible, but focus the first actual Browser entry.
        # In a subdirectory this is normally the parent entry (..).
        if self.screen == "browser" and self.browser_items():
            self.cursor = 1
    def set_screen(self, value: str, push: bool = False) -> None:
        if push: self.stack.append((self.screen, self.cursor, self.offset))
        self.screen = value; self.reset_cursor()
        if value in self.GROUPS and len(self.GROUPS[value]) > 1: self.cursor = 1
        elif value == "rom": self.cursor = 1
    def on(self, value: object) -> str: return "ON" if value else "OFF"
    def motor_active(self) -> bool:
        return self.options["Motor override"] or self.tape_motor
    def unmount_for_browser(self) -> None:
        if self.flip_mode == "solo":
            self.fliplist.clear()
            self.last_flip_media = None
            self.stack = [("browser", 1, 0) if screen == "flip" else (screen, cursor, offset)
                          for screen, cursor, offset in self.stack]
        self.active_media = None; self.disk = None; self.activity = "browser"
    def restore_flip(self, cursor: int, offset: int) -> None:
        if not self.fliplist:
            self.set_screen("browser")
            return
        if 0 < cursor <= len(self.fliplist):
            node = self.fliplist[cursor - 1]
        elif self.last_flip_media in self.fliplist:
            node = self.last_flip_media
        else:
            node = self.fliplist[0]
        if self.active_media is node and (node.kind != "disk" or self.disk is node):
            # Returning from an ordinary menu must not reset an emulating drive.
            self.screen = "flip"; self.reset_cursor()
        else:
            self.activate_flip_item(node)
        self.cursor = cursor if 0 <= cursor <= len(self.fliplist) else self.fliplist.index(node) + 1
        self.offset = offset
        self.ensure_visible()
    def media_type_label(self) -> str:
        if not self.active_media: return "IMG"
        suffix = self.active_media.name.rsplit(".", 1)[-1].upper()
        return suffix if suffix in {"D64", "G64", "D71", "D80", "D81", "D82", "PRG"} else "IMG"
    def main_menu_label(self) -> str:
        if self.flip_mode != "list": return "Menu..."
        if self.screen == "flip":
            count = str(len(self.fliplist)) if len(self.fliplist) <= 99 else "99+"
            return f"Menu... LIST {count}" if count != "99+" else "Menu... LIST99+"
        return "Menu... LIST"
    def enable_list_mode(self) -> None:
        if self.flip_mode == "solo" and self.active_media and self.active_media not in self.fliplist:
            self.fliplist.append(self.active_media)
        self.flip_mode = "list"
    def items(self) -> list[str]:
        items = self.GROUPS.get(self.screen, [])
        if self.screen == "sound" and not self.options["soundOnGPIO"]: return items[:3]
        if self.screen == "menu" and self.fliplist:
            return ["..", "Clear list", *items[1:]]
        return items
    def display_value(self, item: str) -> str:
        values = {"Device number": str(self.device), "Idle trap": self.on(self.options["Idle trap"]), "RAMBOard": self.on(self.options["RAMBOard"]),
          "Skip spinup": self.on(self.options["Skip spinup"]), "Reset": self.options["Reset"], "Sound": self.on(self.options["Sound"]),
          "Motor ON": "ALWAYS" if self.options["Motor override"] else "COMP.", "Auto mount": self.on(self.options["Auto mount"]), "Reset path": self.on(self.options["Reset path"]), "Lowercase": self.on(self.options["Lowercase names"]), "SD2IEC": self.on(self.options["SD2IEC"]),
          "Freq": self.options["Freq"], "Mode": "BUZZER" if self.options["soundOnGPIO"] else "SAMPLE",
          "Duration": f'{self.options["Duration"]}ms', "Temp": self.on(self.options["Temp"]),
          "Dim time": f'{self.options["Dim time"]}s' if self.options["Dim time"] else "OFF", "Dim level": str(self.options["Dim level"]),
          "Type": self.options["NewDiskType"],
          "D64": self.active_media.name if self.active_media else "---", "TAP": self.tape.name if self.tape else "---",
          "LED": self.on(self.disk is not None and self.activity != "browser"), "MOTOR": self.on(self.motor_active()),
          "PLAY": self.on(self.tape is not None), "TRACK": f"{self.track:04.1f}{'S' if self.activity == 'idle' and self.options['Idle trap'] else ''}" if self.disk else "---",
          "TARGET": f"{self.seek_target:03d}", "TAP FILE": self.tape.name if self.tape else "---",
          "Contrast": str(self.options["Contrast"]), "Dimming": self.on(self.options["Dimming"]), "Orient": self.options["Orientation"],
          "Font": "C64" if self.options["i2cLcdUseCBMChar"] else "ASCII", "Scroll": f'{self.options["scrollHighlightRate"]:.2f}s',
          "Reverse": self.on(self.options["Encoder reverse"]), "Write protect": self.on(self.write_protect), "WProt": self.on(self.write_protect),
          "TEMP": f"{self.temperature}C", "SD": self.sd_size, "FREE": self.sd_free,
          "FIRMWARE": self.firmware, "DOS": self.rom, "SIGNALS": self.signal_status()}
        return values.get(item, "")
    def signal_status(self) -> str:
        idle = "S" if self.disk and self.activity == "idle" and self.options["Idle trap"] else ""
        return f"L{1 if self.disk and self.activity != 'browser' else 0} M{1 if self.motor_active() else 0} P{1 if self.tape else 0} T{self.track:04.1f}{idle}"
    def browser_items(self) -> list[Node]:
        result: list[Node] = []
        if len(self.path) > 1: result.append(Node("[..]", "parent"))
        children = sorted_nodes(self.current_dir.children)
        if self.browser_filter == "tape": children = [n for n in children if n.kind in {"dir", "tape"}]
        return result + children
    def ensure_visible(self) -> None:
        visible = 5 if self.screen in {"browser", "flip"} else 6
        if self.screen == "browser":
            entry_cursor = max(0, self.cursor - 1)
            if self.cursor == 0: self.offset = 0
            elif entry_cursor < self.offset: self.offset = entry_cursor
            elif entry_cursor >= self.offset + visible: self.offset = entry_cursor - visible + 1
            return
        if self.screen == "flip":
            if self.cursor == 0: self.offset = 0
            else:
                entry_cursor = self.cursor - 1
                if entry_cursor < self.offset: self.offset = entry_cursor
                elif entry_cursor >= self.offset + visible: self.offset = entry_cursor - visible + 1
            return
        if self.cursor < self.offset: self.offset = self.cursor
        if self.cursor >= self.offset + visible: self.offset = self.cursor - visible + 1
    def move(self, direction: int) -> None:
        if self.options["Encoder reverse"]: direction = -direction
        if self.screen == "flip":
            if not self.fliplist: self.open_browser("No fliplist — browser"); return
            if self.cursor == 0:
                if direction > 0:
                    self.activate_flip_item(self.fliplist[0])
                    self.cursor = 1
                    self.ensure_visible()
                return
            if self.cursor == 1 and direction < 0:
                self.cursor = 0
                self.ensure_visible()
                return
            index = self.cursor - 1
            next_index = (index + direction) % len(self.fliplist)
            self.activate_flip_item(self.fliplist[next_index]); self.cursor = next_index + 1; self.ensure_visible(); return
        if self.screen == "tape_seek" and self.cursor == 1:
            self.seek_target = max(0, min(999, self.seek_target + direction))
            return
        maximum = len(self.browser_items()) if self.screen == "browser" else (len(self.ROMS) if self.screen == "rom" else len(self.items()) - 1)
        if maximum >= 0: self.cursor = max(0, min(maximum, self.cursor + direction)); self.ensure_visible()
    def open_browser(self, message: str = "Media browser") -> None:
        from_fliplist = self.screen == "flip"
        self.browser_return_state = (self.screen, self.cursor, self.offset) if from_fliplist and self.flip_mode == "list" and self.fliplist else None
        if from_fliplist:
            self.unmount_for_browser()
        self.previous_screen = self.screen; self.browser_filter = None; self.set_screen("browser"); self.message = message
    def mount(self, node: Node) -> None:
        self.active_media = node; self.last_flip_media = node; self.disk = node
        if node not in self.fliplist: self.fliplist.append(node)
        self.track = 18.0; self.activity = "mounted"; self.screen = "flip"; self.reset_cursor(); self.message = f"Emulating {node.name}"
    def activate_flip_item(self, node: Node) -> None:
        if node.kind == "disk":
            self.mount(node)
            return
        self.active_media = node; self.last_flip_media = node; self.disk = None; self.activity = "browser"; self.screen = "flip"; self.reset_cursor()
        self.message = f"Mounted {node.name} (browser mode)"
    def select_browser_media(self, node: Node) -> None:
        # LIST only has meaning while it contains an explicitly built list.
        # If its last entry was removed (or an empty .lst was loaded), Enter
        # on the next image starts a fresh one-item SOLO selection.
        if self.flip_mode == "list" and not self.fliplist:
            self.flip_mode = "solo"
        if self.flip_mode == "solo":
            self.fliplist = [node]
        elif node not in self.fliplist:
            self.fliplist.append(node)
        self.activate_flip_item(node)
        self.cursor = self.fliplist.index(node) + 1
        self.ensure_visible()
    def enter_flip_item(self, node: Node) -> None:
        if node.kind == "disk":
            self.open_browser("Encoder click: media browser")
            return
        self.browser_return_state = ("flip", self.cursor, self.offset) if self.flip_mode == "list" and self.fliplist else None
        self.unmount_for_browser()
        found = self.find_path(self.sd, node)
        if found:
            self.path = found[:-1]
            self.browser_source = self.current_dir
        self.screen = "browser"; self.previous_screen = "flip"; self.reset_cursor()
        if found:
            entries = self.browser_items()
            if node in entries: self.cursor = entries.index(node) + 1
            self.ensure_visible()
        self.message = f"Browser: {node.name}"
    def load_fliplist(self, node: Node) -> None:
        self.flip_mode = "list"
        self.fliplist = list(node.children)
        if not self.fliplist:
            self.flip_mode = "solo"
            self.active_media = None; self.last_flip_media = None; self.disk = None; self.activity = "browser"
            self.screen = "browser"; self.reset_cursor(); self.message = f"{node.name}: empty fliplist"
            return
        self.active_media = self.fliplist[0] if self.fliplist else None
        self.last_flip_media = self.active_media
        self.disk = self.active_media if self.active_media and self.active_media.kind == "disk" else None
        self.track = 18.0; self.activity = "mounted" if self.disk else "browser"
        self.screen = "flip"; self.reset_cursor(); self.message = f"Loaded {node.name}: {len(self.fliplist)} images"
    def find_path(self, root: Node, target: Node) -> Optional[list[Node]]:
        if root is target: return [root]
        for child in root.children:
            found = self.find_path(child, target)
            if found: return [root] + found
        return None
    def choose_container(self, node: Node, add_to_fliplist: bool = False) -> None:
        if add_to_fliplist and node not in self.fliplist: self.fliplist.append(node)
        self.active_media = node if add_to_fliplist else None
        self.path.append(node); self.browser_source = node; self.disk = None; self.activity = "browser"; self.reset_cursor(); self.message = f"Browser source: {node.name}"
    def load_tape(self, node: Node) -> None:
        self.tape, self.tape_pos = node, 0; self.browser_filter = None
        if self.screen == "browser" and self.previous_screen == "tape": self.set_screen("tape")
        self.message = f"Loaded {node.name}"
    def activate_browser(self) -> None:
        entries = self.browser_items()
        if self.cursor == 0: self.set_screen("menu", True); return
        node = entries[self.cursor - 1]
        if node.kind == "parent":
            self.path.pop(); self.browser_source = self.current_dir; self.reset_cursor()
        elif node.kind == "dir":
            self.path.append(node); self.browser_source = self.current_dir; self.reset_cursor()
        elif node.kind == "disk": self.select_browser_media(node)
        elif node.kind == "list": self.load_fliplist(node)
        elif node.kind == "browser_image": self.select_browser_media(node)
        elif node.kind == "tape": self.load_tape(node)
    def activate(self) -> None:
        if self.screen == "flip":
            if self.cursor == 0:
                self.set_screen("menu", True)
            elif self.fliplist:
                node = self.fliplist[self.cursor - 1]
                self.enter_flip_item(node)
        elif self.screen == "browser": self.activate_browser()
        elif self.screen == "rom":
            if self.cursor == 0:
                self.go_back()
                return
            selected = self.ROMS[self.cursor - 1]
            if selected == self.rom: self.go_back(); self.message = f"ROM active: {self.rom}"
            else: self.rom = selected; self.message = f"ROM applied: {self.rom} — click again to exit"
        elif self.screen in self.GROUPS: self.activate_menu_item()
    def activate_menu_item(self) -> None:
        item = self.items()[self.cursor]
        if item == "..":
            self.go_back()
            return
        if self.screen == "menu":
            if item == "Clear list":
                self.fliplist.clear(); self.flip_mode = "solo"
                self.active_media = None; self.last_flip_media = None; self.disk = None; self.activity = "browser"
                self.browser_return_state = None
                if self.stack and self.stack[-1][0] == "flip":
                    self.stack[-1] = ("browser", 1, 0)
                self.cursor = self.items().index("Browser")
                self.ensure_visible(); self.message = "Fliplist cleared — media unmounted"
            elif item == "Browser":
                self.flip_mode = "solo"; self.fliplist.clear(); self.active_media = None; self.last_flip_media = None; self.disk = None; self.activity = "browser"
                self.stack.clear(); self.browser_return_state = None; self.previous_screen = "browser"; self.browser_filter = None
                self.path = [self.sd, self.start_folder]; self.browser_source = self.start_folder
                self.set_screen("browser"); self.message = "Browser selected — fliplist cleared"
            elif item == "Reboot Pi1551":
                self.set_screen("reboot_confirm", True)
                self.message = "Confirm Pi1551 reboot; Cancel is selected"
            else:
                target = {"Tape": "tape", "Disk": "disk", "Settings": "settings", "Status": "status"}[item]
                self.set_screen(target, True)
        elif self.screen == "reboot_confirm":
            if item == "Cancel":
                self.go_back()
                self.message = "Pi1551 reboot cancelled"
            elif item == "Reboot Pi1551?":
                self.__init__()
                self.message = "Pi1551 rebooted (simulated); settings reloaded"
        elif self.screen == "settings":
            if item == "Load settings": self.message = "Settings loaded from options-pi1551.txt (simulated)"
            elif item == "Save settings": self.message = "Settings saved to options-pi1551.txt (simulated)"
            else: self.set_screen({"ROM": "rom", "Drive": "drive", "Tape": "tape_settings", "Browser": "browser_settings", "Display": "display", "Encoder": "encoder", "Sound": "sound"}[item], True)
        elif self.screen == "tape":
            if item == "Load TAP...":
                self.unmount_for_browser()
                self.path = [self.sd]; self.browser_source = self.sd
                self.browser_filter = "tape"; self.previous_screen = "tape"; self.set_screen("browser"); self.message = "Choose a TAP"
            elif item == "Eject TAP": self.tape = None; self.tape_pos = 0; self.message = "TAP ejected"
            elif item == "Rewind":
                if self.tape: self.tape_pos = 0; self.message = "TAP rewound"
                else: self.message = "No TAP loaded"
            elif item == "Go to counter":
                if self.tape:
                    self.seek_target = self.tape_pos; self.set_screen("tape_seek", True); self.message = "Set target with encoder, Enter seeks"
                else: self.message = "No TAP loaded"
            else: self.message = "Tape filename is read-only"
        elif self.screen == "tape_seek":
            if item == "TARGET":
                self.tape_pos = self.seek_target
                self.restore_parent("tape")
                self.message = f"TAP counter: {self.tape_pos:03d}"
        elif self.screen == "disk":
            if item == "WProt":
                self.write_protect = not self.write_protect; self.message = f"Write protect: {self.on(self.write_protect)}"
            elif item == "Write image":
                self.message = "Image written (simulated)" if self.disk else "No emulated image to write"
            elif item == "New image":
                self.set_screen("new_image", True)
            else: self.reset_drive()
        elif self.screen == "new_image":
            if item == "Type": self.options["NewDiskType"] = "G64" if self.options["NewDiskType"] == "D64" else "D64"
            else:
                existing = {node.name for node in self.current_dir.children}
                number = 1
                ext = self.options["NewDiskType"].lower()
                while f"NEWIMAGE{number}.{ext}" in existing: number += 1
                node = Node(f"NEWIMAGE{number}.{ext}", "disk", detail=f'NEW {self.options["NewDiskType"]} / EMPTY')
                self.current_dir.children.append(node); self.message = f"Created {node.name}"
        elif self.screen == "status":
            self.message = "Status is read-only"
        else: self.change_setting(item)
    def change_setting(self, item: str) -> None:
        if item == "Device number": self.device = 9 if self.device == 8 else 8
        elif item in self.options and isinstance(self.options[item], bool): self.options[item] = not self.options[item]
        elif item == "Contrast": self.options[item] = self.next_choice(self.options[item], [16,32,48,64,80,96,112,128,144,160,176,192,208,224,240,255])
        elif item == "Orient": self.options["Orientation"] = "rotated" if self.options["Orientation"] == "normal" else "normal"
        elif item == "Font": self.options["i2cLcdUseCBMChar"] = not self.options["i2cLcdUseCBMChar"]
        elif item == "Scroll": self.options["scrollHighlightRate"] = self.next_choice(self.options["scrollHighlightRate"], [0.05,0.10,0.20,0.35,0.50,1.00])
        elif item == "Dim time": self.options[item] = self.next_choice(self.options[item], [0,5,10,30,60,120,300])
        elif item == "Dim level": self.options[item] = self.next_choice(self.options[item], [0,16,32,48,64,80,96,112,128,144,160,176,192,208,224,240,255])
        elif item == "Duration": self.options[item] = self.next_choice(self.options[item], [0,50,100,250,500,1000,2000,5000])
        elif item == "Mode": self.options["soundOnGPIO"] = not self.options["soundOnGPIO"]
        elif item == "Reset": self.options[item] = "IGNORE" if self.options[item] == "REACT" else "REACT"
        elif item == "Auto mount": self.options[item] = not self.options[item]
        elif item == "Reset path": self.options[item] = not self.options[item]
        elif item == "Lowercase": self.options["Lowercase names"] = not self.options["Lowercase names"]
        elif item == "Reverse": self.options["Encoder reverse"] = not self.options["Encoder reverse"]
        elif item == "Freq": self.options[item] = {"1600 Hz": "1200 Hz", "1200 Hz": "2000 Hz", "2000 Hz": "1600 Hz"}.get(self.options[item], "1600 Hz")
        elif item == "Motor ON": self.options["Motor override"] = not self.options["Motor override"]
        self.message = f"{item}: {self.display_value(item)}"
        if self.screen == "sound":
            self.sound_preview_until = time.monotonic() + 1.0
            self.message += " — preview 1s" if self.options["Sound"] else " — preview muted"
    @staticmethod
    def next_choice(value, choices):
        return next((choice for choice in choices if choice > value), choices[0])
    def insert(self) -> None:
        if self.screen == "flip":
            self.enable_list_mode(); self.open_browser("Insert: LIST mode, choose media"); return
        if self.screen != "browser": self.message = "Insert is available in Fliplist and Browser"; return
        entries = self.browser_items()
        if self.cursor == 0: self.set_screen("menu", True); return
        node = entries[self.cursor - 1]
        if node.kind in {"disk", "browser_image"}:
            was_listed = node in self.fliplist
            self.enable_list_mode()
            if was_listed:
                self.fliplist.remove(node)
                if self.last_flip_media is node:
                    self.last_flip_media = self.fliplist[0] if self.fliplist else None
                if not self.fliplist: self.flip_mode = "solo"
                self.message = f"LIST ({len(self.fliplist)}): removed {node.name}"
            elif node not in self.fliplist:
                self.fliplist.append(node); self.message = f"LIST ({len(self.fliplist)}): added {node.name}"
            else:
                self.message = f"LIST ({len(self.fliplist)}): added {node.name}"
        elif node.kind == "dir": self.choose_container(node)
        elif node.kind == "tape": self.load_tape(node)
    def go_back(self) -> None:
        if self.screen == "browser":
            if len(self.path) > 1:
                self.path.pop(); self.browser_source = self.current_dir; self.reset_cursor()
            else:
                target = self.previous_screen
                self.browser_filter = None
                if target == "tape": self.set_screen("tape")
                elif target == "flip" and self.fliplist:
                    state = self.browser_return_state
                    cursor, offset = (state[1], state[2]) if state and state[0] == "flip" else (0, 0)
                    self.restore_flip(cursor, offset)
                else: self.reset_cursor()
        elif self.stack: self.restore_parent()
        elif self.screen not in {"flip", "menu"}: self.screen = "menu"; self.reset_cursor()
        else: self.open_browser("Back: media browser")
    def restore_parent(self, fallback: Optional[str] = None) -> None:
        if self.stack:
            screen, cursor, offset = self.stack.pop()
            if screen == "flip": self.restore_flip(cursor, offset)
            else:
                self.screen, self.cursor, self.offset = screen, cursor, offset
                self.ensure_visible()
        else:
            self.screen = fallback or "menu"
            self.reset_cursor()
    def path_text(self) -> str: return "/" + "/".join(n.name for n in self.path[1:])
    def source_text(self) -> str:
        found = self.find_path(self.sd, self.browser_source)
        return "/" + "/".join(node.name for node in found[1:]) if found else self.browser_source.name
    def status(self) -> tuple[str, str]:
        if self.disk: self.header_key = None
        drive = f"{self.device}: {self.track:04.1f}{'S' if self.disk and self.activity == 'idle' and self.options['Idle trap'] else ''}" if self.disk else self.folder_status()
        percent = min(99, self.tape_pos // 10)
        tape = f"TAPE {self.tape_pos:03d} {percent:02d}% {'M' if self.motor_active() else '-'}P" if self.tape else f"TAPE --- --% {'M' if self.motor_active() else '-'}-"
        return drive, tape
    def folder_status(self) -> str:
        prefix = f"{self.device}:"; folder = self.current_dir.name
        width = (12 if self.options["Temp"] else 16) - len(prefix)
        key = (self.source_text(),prefix,width)
        now = time.monotonic()
        if key != getattr(self,"header_key",None): self.header_key, self.header_clock = key, now
        pos = 0
        if len(folder) > width:
            last = len(folder) - width; cycle = last * 2 + 14
            ticks = int((now - self.header_clock) / self.options["scrollHighlightRate"]) % cycle
            if 7 < ticks <= 7 + last: pos = ticks - 7
            elif 7 + last < ticks <= 14 + last: pos = last
            elif ticks > 14 + last: pos = cycle - ticks
        return prefix + folder[pos:pos+width]
    def toggle_motor(self) -> None:
        self.tape_motor = not self.tape_motor
        self.message = f"Computer MOTOR {self.on(self.tape_motor)}; effective {self.on(self.motor_active())}"
    def reset_drive(self) -> None:
        self.track = 18.0; self.activity = "mounted" if self.disk else "browser"
        self.message = "Drive reset"
    def computer_reset(self) -> None:
        ignored = self.options["Reset"] == "IGNORE"
        if not ignored: self.reset_count += 1; self.reset_drive()
        if self.options["Reset path"]:
            self.path = [self.sd, self.start_folder]; self.browser_source = self.start_folder
            self.stack = [("browser", 1, 0) if screen == "browser" else (screen, cursor, offset)
                          for screen, cursor, offset in self.stack]
            if self.screen == "browser": self.reset_cursor()
        self.message = "Drive RESET ignored" if ignored else f"Computer RESET #{self.reset_count}"
    def tick(self) -> None:
        if self.tape and self.motor_active(): self.tape_pos = (self.tape_pos + 1) % 1000
        if self.disk and self.activity == "mounted": self.track = 1.0 if self.track > 35 else self.track + .1; self.activity = "idle" if self.options["Idle trap"] else "run"
        self.update_scroll()
    def selected_name(self) -> str:
        if self.screen == "display" and self.items()[self.cursor] == "Scroll": return "Scrolling preview 0123456789"
        if self.screen == "flip" and self.active_media in self.fliplist: return self.active_media.name
        if self.screen == "rom" and 0 < self.cursor <= len(self.ROMS): return self.ROMS[self.cursor - 1]
        if self.screen == "tape" and self.tape: return self.tape.name
        if self.screen == "status":
            if self.cursor == 5: return self.rom
            if self.cursor == 6 and self.active_media: return self.active_media.name
            if self.cursor == 7 and self.tape: return self.tape.name
        if self.screen == "browser":
            entries = self.browser_items()
            if 0 < self.cursor <= len(entries): return self.browser_name(entries[self.cursor - 1])
        return ""
    def scroll_prefix(self) -> str:
        if self.screen == "display" and self.items()[self.cursor] == "Scroll": return "Scroll " + self.display_value("Scroll") + " "
        if self.screen == "browser":
            entries = self.browser_items()
            if 0 < self.cursor <= len(entries): return self.browser_prefix(entries[self.cursor - 1])
        if self.screen == "tape": return "1:"
        if self.screen == "status":
            if self.cursor == 5: return "DOS "
            if self.cursor == 6: return self.media_type_label() + " "
            if self.cursor == 7: return "TAP "
        return ""
    def browser_label(self, node: Node) -> str:
        if node.kind in {"disk", "browser_image"} and node in self.fliplist:
            return self.browser_prefix(node) + self.browser_name(node)
        return self.browser_name(node)
    def browser_prefix(self, node: Node) -> str:
        if node.kind in {"disk", "browser_image"} and node in self.fliplist:
            return f"{self.fliplist.index(node) + 1}>"
        return ""
    def browser_name(self, node: Node) -> str:
        if node.kind == "dir":
            return f"[{node.name}]"
        return node.name
    def update_scroll(self) -> None:
        text = self.selected_name()
        width = 15 - len(self.scroll_prefix())
        if len(text) <= width: self.scroll_key = ""; self.scroll_pos = 0; return
        key = f"{self.screen}:{self.cursor}:{text}"
        now = time.monotonic()
        if key != self.scroll_key: self.scroll_key = key; self.scroll_pos = 0; self.scroll_dir = 1; self.scroll_pause = 7; self.scroll_clock = now; return
        rate = max(0.01, self.options["scrollHighlightRate"])
        steps = int((now - self.scroll_clock) / rate)
        self.scroll_clock += steps * rate
        for _ in range(min(steps, 1000)):
            if self.scroll_pause: self.scroll_pause -= 1; continue
            last = len(text) - width; self.scroll_pos += self.scroll_dir
            if self.scroll_pos >= last: self.scroll_pos, self.scroll_dir, self.scroll_pause = last, -1, 7
            elif self.scroll_pos <= 0: self.scroll_pos, self.scroll_dir, self.scroll_pause = 0, 1, 7
    def scrolling(self, text: str, selected: bool) -> str:
        width = 15 - len(self.scroll_prefix())
        return text[self.scroll_pos:self.scroll_pos + width] if selected and len(text) > width else clip(text, width)
    def browser_visible_name(self, node: Node, selected: bool) -> str:
        width = 15 - len(self.browser_prefix(node))
        name = self.browser_name(node)
        return name[self.scroll_pos:self.scroll_pos + width] if selected and len(name) > width else clip(name, width)

class UI:
    def __init__(self, stdscr: curses.window, sim: Simulator) -> None:
        self.s, self.sim = stdscr, sim; self.box_x, self.box_y, self.box_w, self.box_h = 2, 1, 18, 10; self.ox, self.oy, self.width = 3, 2, 16
        try:
            curses.curs_set(0); curses.start_color(); curses.use_default_colors(); curses.init_pair(1, curses.COLOR_CYAN, -1); curses.init_pair(2, curses.COLOR_BLACK, curses.COLOR_CYAN); curses.init_pair(3, curses.COLOR_YELLOW, -1)
        except curses.error: pass
    def text(self, y: int, x: int, value: str, attr: int = 0) -> None:
        try: self.s.addstr(y, x, clip(value, max(0, self.s.getmaxyx()[1] - x)), attr)
        except curses.error: pass
    def frame(self) -> bool:
        h, w = self.s.getmaxyx(); self.s.erase()
        if h < 15 or w < 70: self.text(0, 0, "Resize terminal to at least 70x15"); return False
        tl, tr, bl, br, hz, vt = "┌", "┐", "└", "┘", "─", "│"
        self.text(0, 0, tl + hz * (w - 2) + tr, curses.color_pair(1)); self.text(h - 1, 0, bl + hz * (w - 2) + br, curses.color_pair(1))
        for y in range(1, h - 1): self.text(y, 0, vt, curses.color_pair(1)); self.text(y, w - 1, vt, curses.color_pair(1))
        self.text(0, 3, " PI1551 TUI ", curses.color_pair(1) | curses.A_BOLD); x, y, bw, bh = self.box_x, self.box_y, self.box_w, self.box_h
        self.text(y, x, tl + hz * (bw - 2) + tr, curses.color_pair(1)); self.text(y, x + 2, " OLED ", curses.color_pair(1) | curses.A_BOLD)
        for row in range(y + 1, y + bh - 1): self.text(row, x, vt, curses.color_pair(1)); self.text(row, x + bw - 1, vt, curses.color_pair(1))
        self.text(y + bh - 1, x, bl + hz * (bw - 2) + br, curses.color_pair(1)); return True
    def line(self, row: int, value: str, selected: bool = False, marker: str = " ") -> None:
        self.text(self.oy + row, self.ox, (marker + clip(value, 15)).ljust(16), curses.color_pair(2) if selected else 0)
    def status_line(self, row: int, left: str, right: str) -> None:
        value = clip(left, self.width - len(right) - (1 if right else 0))
        self.text(self.oy + row, self.ox, (value + " " * max(0, self.width - len(value) - len(right)) + right).ljust(self.width))
    def tree(self) -> None:
        x = self.box_x + self.box_w + 3; lines = [f"MENU TREE  SD {self.sim.source_text()}", "├─ Fliplist", "├─ Browser", "├─ Device menu", "│  ├─ Browser", "│  ├─ Tape", "│  ├─ Disk", "│  │  └─ New image", "│  ├─ Settings", "│  │  ├─ ROM", "│  │  ├─ Drive", "│  │  ├─ Tape", "│  │  ├─ Browser", "│  │  ├─ Display", "│  │  ├─ Encoder", "│  │  └─ Sound", "│  ├─ Status", "│  └─ Reboot Pi1551", "│     └─ Confirm"]
        active = {"flip": 1, "browser": 2, "menu": 3, "tape": 5, "tape_seek": 5, "disk": 6, "new_image": 7, "settings": 8, "rom": 9, "drive": 10, "tape_settings": 11, "browser_settings": 12, "display": 13, "encoder": 14, "sound": 15, "status": 16, "reboot_confirm": 18}.get(self.sim.screen)
        if self.sim.fliplist:
            lines.insert(4, "│  ├─ Clear list")
            if active is not None and active >= 4: active += 1
        for i, value in enumerate(lines): self.text(self.box_y + i, x, ("▶ " if i == active else "  ") + value, curses.color_pair(2) if i == active else 0)
    def draw(self) -> None:
        if not self.frame(): return
        sim = self.sim; drive, tape = sim.status(); self.status_line(0, drive, f"{sim.temperature:02d}C" if sim.options["Temp"] else ""); self.line(1, tape); self.tree()
        if sim.screen == "flip":
            self.line(2, sim.main_menu_label(), sim.cursor == 0, "▶" if sim.cursor == 0 else " ")
            for row in range(5):
                index = sim.offset + row
                if index < len(sim.fliplist):
                    node = sim.fliplist[index]; active = node is sim.active_media; selected = sim.cursor == index + 1
                    self.line(3 + row, sim.scrolling(node.name, active), selected, "▶" if active else " ")
                else: self.line(3 + row, "")
        elif sim.screen == "browser":
            entries, start = sim.browser_items(), sim.offset
            self.line(2, sim.main_menu_label(), sim.cursor == 0, "▶" if sim.cursor == 0 else " ")
            for row in range(5):
                index = start + row
                if index < len(entries):
                    node = entries[index]
                    selected = sim.cursor == index + 1
                    label = sim.browser_prefix(node) + sim.browser_visible_name(node, selected)
                    self.line(3 + row, label, selected, "▶" if selected else " ")
                else: self.line(3 + row, "")
        elif sim.screen == "rom":
            rom_items = [".."] + sim.ROMS
            for row, index in enumerate(range(sim.offset, min(len(rom_items), sim.offset + 6))):
                item = rom_items[index]
                label = sim.scrolling(item, index == sim.cursor) if index else item
                self.line(2 + row, label, index == sim.cursor, "▶" if item == sim.rom else " ")
        elif sim.screen == "status":
            values = ["..", f"TEMP {sim.temperature:02d}C", f"SD {sim.sd_size}", f"FREE {sim.sd_free}", f"FW {sim.firmware}", f"DOS {sim.rom}", f"{sim.media_type_label()} {sim.active_media.name if sim.active_media else '---'}", f"TAP {sim.tape.name if sim.tape else '---'}", f"LED {sim.on(sim.disk is not None and sim.activity != 'browser')}", f"MOTOR {sim.on(sim.motor_active())}", f"PLAY {sim.on(sim.tape is not None)}", f"TRACK {sim.track:04.1f}{'S' if sim.disk and sim.activity == 'idle' and sim.options['Idle trap'] else ''}" if sim.disk else "TRACK ---"]
            for row, index in enumerate(range(sim.offset, min(len(values), sim.offset + 6))):
                value = values[index]
                if index == 5:
                    value = "DOS " + sim.scrolling(sim.rom, index == sim.cursor)
                elif index == 6 and sim.active_media:
                    value = sim.media_type_label() + " " + sim.scrolling(sim.active_media.name, index == sim.cursor)
                elif index == 7 and sim.tape:
                    value = "TAP " + sim.scrolling(sim.tape.name, index == sim.cursor)
                self.line(2 + row, value, index == sim.cursor)
        elif sim.screen == "tape":
            values = ["..", "1:", "Load TAP...", "Eject TAP", "Rewind", "Go to counter"]
            for row, value in enumerate(values):
                if row == 1 and sim.tape: value = "1:" + sim.scrolling(sim.tape.name, True)
                self.line(2 + row, value, row == sim.cursor)
        else:
            values = sim.items()
            for row, index in enumerate(range(sim.offset, min(len(values), sim.offset + 6))):
                item = values[index]
                submenu = sim.screen == "settings" and item in {"ROM", "Drive", "Tape", "Browser", "Display", "Encoder", "Sound"}
                suffix = " " + sim.display_value(item) if sim.display_value(item) and not submenu else ""
                label = item + suffix
                if sim.screen == "display" and item == "Scroll":
                    label += " " + sim.scrolling("Scrolling preview 0123456789", index == sim.cursor)
                self.line(2 + row, label, index == sim.cursor)
        h = self.s.getmaxyx()[0]; self.text(h - 3, 2, "Message: " + sim.message, curses.color_pair(3)); self.text(h - 2, 2, "←→ encoder   Enter select   i Insert   b Back   r RESET   m MOTOR   q quit")

def run(stdscr: curses.window) -> None:
    sim, ui = Simulator(), None; ui = UI(stdscr, sim); stdscr.timeout(120)
    while True:
        ui.draw(); ui.s.refresh(); key = stdscr.getch()
        if key == -1: sim.tick(); continue
        if key in (ord("q"), ord("Q")): return
        if key in (curses.KEY_LEFT, curses.KEY_RIGHT): sim.move(-1 if key == curses.KEY_LEFT else 1)
        elif key in (10, 13, curses.KEY_ENTER): sim.activate()
        elif key in (ord("i"), ord("I")): sim.insert()
        elif key in (ord("b"), ord("B"), 27, curses.KEY_BACKSPACE, 127): sim.go_back()
        elif key in (ord("r"), ord("R")): sim.computer_reset()
        elif key in (ord("m"), ord("M")): sim.toggle_motor()
        sim.tick()

def main() -> None: curses.wrapper(run)
if __name__ == "__main__": main()
