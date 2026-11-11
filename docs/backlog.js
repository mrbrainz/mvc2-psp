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
  "P33",
  "P34",
  "P35",
  "P53",
  "P54",
  "P55",
  "P57",
  "P58",
  "P59"
 ],
 "activities": [
  {
   "id": "P56",
   "order": 10.4,
   "phase": "P2",
   "size": "S",
   "status": "Ready",
   "depends": [],
   "read": [
    "port/psp/host/psp_fe_sync.c",
    "port/docs/PSP.md"
   ],
   "title": "PSP ending: blank under \"Presented by\" (CAPCOM logo?)",
   "description": "After the post-credits legal text, the \"Presented by\" screen shows nothing underneath; the web port (arcade path) shows \"THANK YOU FOR PLAYING / PRESENTED BY\" with the yellow-on-blue CAPCOM logo below (checked in an arcade_ending_rank run, 2026-11-11). Log that screen's draws in an arcade run (SCRIPT=, DRAWLOG=1 rej counters): texture not loaded/synced, draw rejected, or another optimiser issue as in P34. PSP-side fix only.",
   "done_when": "The \"Presented by\" screen shows what the arcade shows, checked by eye in PPSSPP after a full arcade run."
  },
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
  }
 ]
};
