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
  "P60"
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
    "P41"
   ],
   "read": [
    "README.md"
   ],
   "title": "Release packaging",
   "description": "EBOOT icon/title, user guide for supplying own disc data.",
   "done_when": "Installable EBOOT + PSP section in USER_GUIDE.md."
  },
  {
   "id": "P61",
   "order": 17,
   "phase": "P5",
   "size": "XS",
   "status": "Backlog",
   "depends": [
    "P60"
   ],
   "read": [
    "port/docs/PLATFORM_SURFACE.md",
    "port/docs/BOOT_NOTES.md"
   ],
   "title": "Block 2P join-in",
   "description": "P2 input is already dead on PSP (plat_pad_connected returns 0 for slot 1). Confirm in a full run that no challenger or P2 cursor can appear; add a mask only if one does.",
   "done_when": "Pressing any input never spawns P2 in a full arcade run in PPSSPP."
  },
  {
   "id": "P62",
   "order": 18,
   "phase": "P5",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P60"
   ],
   "read": [
    "port/docs/PLATFORM_SURFACE.md",
    "port/docs/BOOT_NOTES.md"
   ],
   "title": "Hide 2P modes from menus",
   "description": "Trace which front-end sub-state (mode 1, src/w20b0_e.c, D_00477170) draws the mode menu, then skip Versus/2P entries from the PSP side. Entries are textures, not strings.",
   "done_when": "Menus show only single-player modes; navigation wraps cleanly."
  },
  {
   "id": "P63",
   "order": 19,
   "phase": "P5",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P61"
   ],
   "read": [
    "port/docs/PLATFORM_SURFACE.md",
    "port/docs/BOOT_NOTES.md"
   ],
   "title": "Hide 2P prompts and HUD text",
   "description": "Skip func_003D7350 draws of gPressStartLabel_004B5640 / gNoChallengerLabel_004B5630 (from residual_00172B70) via a PSP-side --wrap, like the existing func_003D7400 wrap. Check attract/title for other 2P prompts.",
   "done_when": "No 2P prompt visible anywhere in a full arcade run (screenshots)."
  },
  {
   "id": "P64",
   "order": 20,
   "phase": "P5",
   "size": "XS",
   "status": "Backlog",
   "depends": [
    "P60"
   ],
   "read": [
    "port/docs/PLATFORM_SURFACE.md",
    "port/docs/BOOT_NOTES.md"
   ],
   "title": "Drop P2 controls from options",
   "description": "Hide the controller left/right side choice (gControllerLeftPrompt_004B3590 / gControllerRightPrompt_004B35B0) in button config so only P1 shows. PSP-side only.",
   "done_when": "Options show P1 settings only; saves still load."
  },
  {
   "id": "P65",
   "order": 21,
   "phase": "P5",
   "size": "S",
   "status": "Backlog",
   "depends": [],
   "read": [
    "port/docs/BOOT_NOTES.md"
   ],
   "title": "Add port credits to top of staff roll",
   "description": "Prepend entries to the ending credits, PSP-side only: 'MVC2 PSP Port By' / 'Mr Brainz & LLM Vibes', then 'Original MVC2 Decomp' / 'G. Guthrie'. Match the existing heading/name styles.",
   "done_when": "New entries roll first in the ending credits, styled like the originals; rest of the roll unchanged (screenshots)."
  }
 ]
};
