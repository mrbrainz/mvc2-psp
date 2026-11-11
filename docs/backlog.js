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
  "P09",
  "P10",
  "P11",
  "P12",
  "P13",
  "P21",
  "P24",
  "P25",
  "P26",
  "P27",
  "P31",
  "P33"
 ],
 "activities": [
  {
   "id": "P14",
   "order": 5.4,
   "phase": "P1",
   "size": "S",
   "status": "Ready",
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
   "id": "P22",
   "order": 10,
   "phase": "P2",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P21",
    "P24"
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
   "id": "P28",
   "order": 5.05,
   "phase": "P2",
   "size": "S",
   "status": "Ready",
   "depends": [
    "P25"
   ],
   "read": [
    "port/render/r_gu_mesh.c",
    "port/docs/PSP.md"
   ],
   "title": "PSP stage background glitches",
   "description": "Animated stage backgrounds glitch in PPSSPP: water turns grey at times, rocks along the bottom edge flicker. Seen in the scripted fight run (STAGE=16 env.txt), with or without audio.",
   "done_when": "Water and edge rocks draw steadily through a fight on the affected stage(s), matching the native/web port."
  },
  {
   "id": "P32",
   "order": 5.15,
   "phase": "P3",
   "size": "S",
   "status": "Ready",
   "depends": [
    "P31"
   ],
   "read": [
    "port/docs/AUDIO.md",
    "port/audio/audio_psp.c"
   ],
   "title": "PSP sound effects too quiet against music",
   "description": "In PPSSPP the SFX/voices sit noticeably below the ADX music, unlike the web build at 100/100. The PSP mix matches upstream's synth level (MIXCMP=1), so find the cause PSP-side (output path, clamp, sceAudio, gains) and fix it in PSP files only; no upstream changes.",
   "done_when": "In a PPSSPP fight the SFX/music balance matches the web build at default volumes (user listens)."
  },
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
   "id": "P07",
   "order": 5.5,
   "phase": "P1",
   "size": "XS",
   "status": "Ready",
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
   "order": 5.6,
   "phase": "P4",
   "size": "M",
   "status": "Ready",
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
  }
 ]
};
