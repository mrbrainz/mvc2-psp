// Active backlog. Models: read this file; completed_ids resolves Done dependencies without archive reads.
// Complete a ticket here, then run: node tools/backlog.mjs sync
window.BACKLOG = {
 "title": "MvC2 PSP Backlog",
 "phases": [
  {
   "id": "P0",
   "name": "Toolchain & boot"
  },
  {
   "id": "P1",
   "name": "Platform layer"
  },
  {
   "id": "P2",
   "name": "Renderer"
  },
  {
   "id": "P3",
   "name": "Audio"
  },
  {
   "id": "P4",
   "name": "Performance & correctness"
  },
  {
   "id": "P5",
   "name": "Release"
  }
 ],
 "completed_ids": [
  "P01",
  "P02",
  "P03",
  "P04",
  "P05",
  "P06",
  "P07",
  "P08",
  "P09",
  "P10",
  "P11",
  "P12",
  "P13",
  "P14",
  "P21",
  "P22",
  "P23",
  "P24",
  "P25",
  "P26",
  "P27",
  "P28",
  "P31",
  "P32",
  "P33",
  "P34",
  "P35",
  "P53",
  "P54",
  "P55",
  "P56",
  "P57",
  "P58",
  "P59",
  "P60",
  "P61",
  "P62",
  "P63",
  "P64",
  "P65",
  "P66",
  "P67",
  "P68",
  "P69"
 ],
 "activities": [
  {
   "id": "P41",
   "order": 13,
   "phase": "P4",
   "size": "L",
   "status": "Backlog",
   "depends": [
    "P25",
    "P31",
    "P13",
    "P09"
   ],
   "read": [
    "port/docs/ORACLE_FUZZ.md"
   ],
   "title": "Full-speed on hardware",
   "description": "Profile on real PSP (333MHz); fix hotspots, consider ME offload for audio.",
   "done_when": "Locked 60fps in 3v3 fights on target model."
  },
  {
   "id": "P42",
   "order": 14,
   "phase": "P4",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P06"
   ],
   "read": [
    "port/docs/ORACLE_FUZZ.md"
   ],
   "title": "Determinism check on PSP",
   "description": "Run fuzz seeds on PSP build and compare hashes with native.",
   "done_when": "Selected seeds match frame for frame."
  },
  {
   "id": "P51",
   "order": 15,
   "phase": "P5",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P41",
    "P70",
    "P71",
    "P72"
   ],
   "read": [
    "README.md"
   ],
   "title": "Release packaging",
   "description": "EBOOT icon/title, user guide for supplying own disc data.",
   "done_when": "Installable EBOOT + PSP section in USER_GUIDE.md."
  },
  {
   "id": "P70",
   "order": 18,
   "phase": "P5",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P69"
   ],
   "read": [
    "port/psp/pack/psp_pack.py",
    "port/psp/tools/xmb_art.py"
   ],
   "title": "EBOOT builder tool (Go, single binary)",
   "description": "Port psp_pack.py and xmb_art.py to one Go program: check the ISO against known hashes (clear errors for wrong region, .bin/.cue, .cso), generate ICON0/PIC1, pack AFS00-03 as DATA.PSAR, write MVC2/EBOOT.PBP. Drag-and-drop or file picker on Windows/macOS, CLI argument on Linux.",
   "done_when": "Output is byte-identical to the Python tools for the same ISO and base EBOOT; the packed EBOOT boots in PPSSPP with no ISO present."
  },
  {
   "id": "P71",
   "order": 19,
   "phase": "P5",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P70"
   ],
   "read": [
    "port/docs/PSP.md"
   ],
   "title": "Release CI and packages",
   "description": "CI fetches PSPSDK (never committed), builds the base EBOOT and builders, and publishes mvc2-psp-vX-windows.zip (exe + base/EBOOT.PBP), -macos.zip (universal .app with the base EBOOT inside), -linux-x64.tar.gz, -pypack.zip (base EBOOT + Python tools) and SHA256SUMS, each with README.txt and LICENSE.txt.",
   "done_when": "A tagged push produces all four archives plus SHA256SUMS; none contain copyrighted data."
  },
  {
   "id": "P72",
   "order": 20,
   "phase": "P5",
   "size": "XS",
   "status": "Backlog",
   "depends": [
    "P71"
   ],
   "read": [
    "README.md"
   ],
   "title": "User README and unsigned-binary guidance",
   "description": "Five-step README.txt (drop ISO, get MVC2 folder, copy to PSP/GAME, play, do not share the built EBOOT) plus Gatekeeper/SmartScreen first-launch steps; sign binaries if certificates are available.",
   "done_when": "README.txt is in every package; a new user can build and install from it alone."
  }
 ]
};
