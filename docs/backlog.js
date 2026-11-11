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
  "P22",
  "P23",
  "P24",
  "P25",
  "P26",
  "P27",
  "P28",
  "P31",
  "P33",
  "P35",
  "P53"
 ],
 "activities": [
  {
   "id": "P52",
   "order": 10.15,
   "phase": "P2",
   "size": "S",
   "status": "Backlog",
   "depends": [],
   "read": [
    "port/render/r_gu.c",
    "port/docs/PSP.md"
   ],
   "title": "PSP credits: first concept-art frame glitches",
   "description": "In the arcade credits scroll the first concept-art image draws garbled; the following images scroll cleanly. Low priority. Likely a texture/CLUT upload racing its first draw or a first-use path in the 2D sprite route; reproduce with the SCRIPT= arcade run near the end and compare with the web port.",
   "done_when": "The first credits concept-art image draws cleanly in PPSSPP, as the later ones do."
  },
  {
   "id": "P34",
   "order": 10.1,
   "phase": "P2",
   "size": "M",
   "status": "Ready",
   "depends": [],
   "read": [
    "port/render/r_gu.c",
    "port/docs/PSP.md"
   ],
   "title": "PSP post-credits legal text garbled",
   "description": "After the arcade credits (state 2 1 4) the legal text is drawn as textured triangles (func_003DF0C0) whose two triangles per glyph carry different UV rows already in the game's data (e.g. v 0.1895 vs 0.1270), giving cut letters and diagonal slashes. The boot legal screen (sprites) is fine. Compare the vertex data against the wasm reference to find the diverging game code; PSP-side fix only.",
   "done_when": "The post-credits legal text reads cleanly in PPSSPP after a full arcade run (SCRIPT= arcade run)."
  },
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
