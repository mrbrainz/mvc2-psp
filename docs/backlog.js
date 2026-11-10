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
  "P05"
 ],
 "activities": [
  {
   "id": "P04",
   "order": 4,
   "phase": "P0",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P03"
   ],
   "read": [
    "port/docs/PORT_PLAN.md"
   ],
   "title": "Memory budget",
   "description": "Measure heap/static footprint against PSP-1000 (~24MB user) and Slim (~52MB); decide minimum model.",
   "done_when": "Footprint numbers + chosen target model recorded in PSP.md."
  },
  {
   "id": "P11",
   "order": 5,
   "phase": "P1",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P03"
   ],
   "read": [
    "port/docs/PLATFORM_SURFACE.md",
    "port/platform/platform.h",
    "port/platform/platform_sdl.c"
   ],
   "title": "PSP platform shell",
   "description": "platform_psp.c: main loop, vsync 60Hz, exit callback, home-button handling, timing.",
   "done_when": "Game loop runs at 60Hz in PPSSPP with clean exit via HOME."
  },
  {
   "id": "P12",
   "order": 6,
   "phase": "P1",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P11"
   ],
   "read": [
    "port/platform/pad.c",
    "port/platform/input.c"
   ],
   "title": "PSP controls",
   "description": "Map sceCtrl to game pads; default arcade-style layout for 6 buttons on PSP face/shoulder buttons.",
   "done_when": "All game inputs reachable; layout documented."
  },
  {
   "id": "P13",
   "order": 7,
   "phase": "P1",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P11"
   ],
   "read": [
    "port/platform/fs.c",
    "port/docs/ASSET_LOADING.md"
   ],
   "title": "Disc intake on PSP",
   "description": "Read the player-supplied PS2 ISO from ms0:/ (or pre-converted data); AFS reads via sceIo.",
   "done_when": "Game assets load from memory stick; nothing copyrighted bundled."
  },
  {
   "id": "P14",
   "order": 8,
   "phase": "P1",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P11"
   ],
   "read": [
    "port/platform/settings.c"
   ],
   "title": "Saves on PSP",
   "description": "Settings/save file on memory stick.",
   "done_when": "Options survive restart."
  },
  {
   "id": "P21",
   "order": 9,
   "phase": "P2",
   "size": "L",
   "status": "Backlog",
   "depends": [
    "P11"
   ],
   "read": [
    "port/docs/RENDERER.md",
    "port/docs/RENDER_PATH.md",
    "port/render/"
   ],
   "title": "sceGu renderer backend",
   "description": "Implement the renderer API on sceGu: state setter, 2D prims, character drawer, stage meshes.",
   "done_when": "Title, select and a fight render correctly in PPSSPP."
  },
  {
   "id": "P22",
   "order": 10,
   "phase": "P2",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P21"
   ],
   "read": [
    "port/docs/RENDERER.md"
   ],
   "title": "Textures within VRAM",
   "description": "TIM2/CLUT to PSP CLUT textures (T4/T8 native); 2MB VRAM budget and caching/swizzle.",
   "done_when": "No missing/corrupt textures over a full arcade run."
  },
  {
   "id": "P23",
   "order": 11,
   "phase": "P2",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P21"
   ],
   "read": [
    "port/docs/RENDERER.md"
   ],
   "title": "480x272 presentation",
   "description": "Scale 640x480 game space to 480x272: fit/stretch modes, HUD legibility check.",
   "done_when": "Full HUD readable; chosen default documented."
  },
  {
   "id": "P31",
   "order": 12,
   "phase": "P3",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P11"
   ],
   "read": [
    "port/docs/AUDIO.md",
    "port/docs/AUDIO_NOTES.md",
    "port/audio/"
   ],
   "title": "PSP audio backend",
   "description": "Mixer output via sceAudio on a dedicated thread; ADX music + HD/BD SFX.",
   "done_when": "Music and SFX play without underruns in a fight."
  },
  {
   "id": "P41",
   "order": 13,
   "phase": "P4",
   "size": "L",
   "status": "Backlog",
   "depends": [
    "P21",
    "P31",
    "P13"
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
   "id": "P06",
   "order": 3.5,
   "phase": "P0",
   "size": "M",
   "status": "Ready",
   "depends": [
    "P03"
   ],
   "read": [
    "port/docs/PSP.md",
    "port/host/state_dump.c",
    "port/tools/oracle/compare.py"
   ],
   "title": "PSP vs wasm state divergence",
   "description": "Same ISO, 600 frames: compare.py differs at frame 0 in game block words 0x004F4318/40/4C (PSP = wasm - 0x16D6C0 each) and BSS block 0x00626580 from frame 1, more blocks from frame 6. Find the cause (pointer translation in state_dump, glue code-label handles, or real logic/ABI/float divergence).",
   "done_when": "compare.py PSP vs wasm matches for 600 frames, or each remaining difference is explained as non-gameplay and listed in an ignore file."
  },
  {
   "id": "P07",
   "order": 3.6,
   "phase": "P1",
   "size": "XS",
   "status": "Backlog",
   "depends": [
    "P03"
   ],
   "read": [
    "port/psp/host/libc_o32.c"
   ],
   "title": "PSP log newlines",
   "description": "host_log ends lines with fputc to stderr, which goes to newlib, not psp_log; lines run together in mvc2_psp.log.",
   "done_when": "mvc2_psp.log has one message per line."
  }
 ]
};
