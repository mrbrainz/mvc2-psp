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
  "P11"
 ],
 "activities": [
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
  },
  {
   "id": "P08",
   "order": 4.5,
   "phase": "P4",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P04"
   ],
   "read": [
    "port/docs/PSP.md",
    "port/host/boot.c"
   ],
   "title": "PSP-1000 feasibility: arena high-water",
   "description": "PSP-1000 has ~24 MB user memory; the 28 MB EE heap (22 MB arena + 6 MB pool) does not fit. Measure the arena and pool high-water over full fights and decide if a smaller arena is possible without changing behaviour.",
   "done_when": "High-water figures recorded in PSP.md and a go/no-go on PSP-1000 support."
  },
  {
   "id": "P09",
   "order": 4.2,
   "phase": "P0",
   "size": "M",
   "status": "Ready",
   "depends": [
    "P11"
   ],
   "read": [
    "port/docs/PSP.md",
    "port/platform/platform_psp.c"
   ],
   "title": "Profile the logic frame on PSP",
   "description": "A logic frame costs 94-121 ms in PPSSPP at 333 MHz (-O2), budget 16.7 ms; EE float emulation is ~10-25% of it. Find where the time goes (PPSSPP profiler or sampled sceRtc timers around mvc2_frame stages), and measure on real hardware if available. Also re-run compare_ports.py: host_sigs now come from the wasm path, not caller guesses.",
   "done_when": "Top costs per frame listed in PSP.md with a plan (or no-go) for 60 Hz; compare_ports.py still 0 diverging words."
  }
 ]
};
