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
  },
  {
   "id": "P6",
   "name": "PSP-1000"
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
  "P69",
  "P70",
  "P71",
  "P81",
  "P82"
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
    "P09",
    "P83",
    "P84",
    "P85",
    "P86"
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
  },
  {
   "id": "P73",
   "order": 21,
   "phase": "P6",
   "size": "S",
   "status": "Backlog",
   "depends": [],
   "read": [
    "port/docs/PSP.md",
    "port/psp/host/psp_ee_heap.c",
    "port/psp/host/psp_ee_hwm.c"
   ],
   "title": "PSP-1000 memory budget",
   "description": "Measure every consumer against the PSP-1000's user memory (~24 MB): ELF sections (largest static symbols), EE heap high-water, renderer (textures, atlases, mesh buffers, CLUTs), audio, PSPSDK/newlib and kernel allocations. Run a full arcade run in PPSSPP with the PSP-1000 model. Set a per-consumer target budget for the low profile.",
   "done_when": "PSP.md has the per-consumer table, the PSP-1000 total and a target budget per consumer that fits with headroom, without loading game data from the memory stick (or the shortfall if it cannot)."
  },
  {
   "id": "P74",
   "order": 22,
   "phase": "P6",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P73"
   ],
   "read": [
    "port/psp/host/psp_ee_heap.c",
    "port/psp/host/psp_entry.c",
    "port/docs/PSP.md"
   ],
   "title": "Boot-time memory profile (full / low)",
   "description": "At boot, choose a memory profile from the user memory actually available (MEMSIZE=1 is granted on PSP-2000+, not on the PSP-1000): full = today's sizes, low = the P73 budgets. Log the profile and free memory. Sizes read from the profile instead of constants. PSP-side only (new code under port/psp/ or --wrap).",
   "done_when": "PPSSPP with the PSP-2000+ model logs 'full' and behaves byte-for-byte as before (same logic hashes over a scripted run); with the PSP-1000 model it logs 'low'."
  },
  {
   "id": "P75",
   "order": 23,
   "phase": "P6",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P74"
   ],
   "read": [
    "port/psp/host/psp_ee_heap.c",
    "port/psp/host/psp_ee_hwm.c",
    "port/host/boot.c"
   ],
   "title": "Low profile: smaller EE heap and game arena",
   "description": "In the low profile, size the EE heap and the game's arena/handle pool to the P54 high-water (arena 16.3 MB written of 22 MB, pool 4.6 of 6 MB) plus a measured margin, from the PSP side (no upstream change). Full profile unchanged.",
   "done_when": "Low profile: full arcade run in PPSSPP completes with the smaller heap, [hwm] shows the margin kept; full profile logic hashes unchanged."
  },
  {
   "id": "P76",
   "order": 24,
   "phase": "P6",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P74"
   ],
   "read": [
    "port/render/r_gu.c",
    "port/render/r_gu_char.c",
    "port/render/r_gu_mesh.c",
    "port/docs/PSP.md"
   ],
   "title": "Low profile: renderer memory budgets",
   "description": "In the low profile, cap the texture cache, character atlas pages and mesh buffers to the P73 budgets (evict sooner, 16-bit textures where they look the same), without visible changes in normal play. Full profile unchanged.",
   "done_when": "Low profile 3v3 fight and full arcade run in PPSSPP stay within the renderer budget with no missing or corrupt textures (DRAWLOG counters, screenshots compared with the full profile)."
  },
  {
   "id": "P77",
   "order": 25,
   "phase": "P6",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P73"
   ],
   "read": [
    "port/docs/PSP.md"
   ],
   "title": "Smaller program image (all models)",
   "description": "Find the largest static data in the 9.2 MB ELF (debug tables, unused upstream data, zero-filled arrays that can move to the heap) and shrink it without behaviour changes. Applies to every model, so only changes that cost the full profile nothing.",
   "done_when": "ELF size reduction recorded in PSP.md; full-profile scripted run logic hashes and frame times unchanged."
  },
  {
   "id": "P78",
   "order": 26,
   "phase": "P6",
   "size": "S",
   "status": "Backlog",
   "depends": [
    "P74"
   ],
   "read": [
    "port/audio/mix_psp.c",
    "port/audio/vag_psp.c",
    "port/psp/host/psp_audio.c"
   ],
   "title": "Low profile: audio buffers",
   "description": "Measure audio memory (sample bank, PCM replacements, mixer and stream buffers) and trim it in the low profile where it is inaudible. Full profile unchanged.",
   "done_when": "Low profile audio memory within the P73 budget; no audible difference in a full arcade run."
  },
  {
   "id": "P79",
   "order": 27,
   "phase": "P6",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P75",
    "P76",
    "P77",
    "P78"
   ],
   "read": [
    "port/docs/PSP.md",
    "port/psp/scripts/arcade_psp.txt"
   ],
   "title": "PSP-1000 full arcade run",
   "description": "Full scripted arcade run in PPSSPP with the PSP-1000 model and the low profile, game data loaded as today (no memory-stick streaming). Record memory headroom and frame times.",
   "done_when": "Arcade run completes on the PSP-1000 model with headroom recorded in PSP.md; README minimum model updated."
  },
  {
   "id": "P80",
   "order": 28,
   "phase": "P6",
   "size": "L",
   "status": "Backlog",
   "depends": [
    "P79"
   ],
   "read": [
    "port/psp/host/psp_disc.c",
    "port/psp/host/psp_disc_io.c"
   ],
   "title": "Last resort: load game data from the memory stick on demand (low profile)",
   "description": "Only if P75-P78 cannot reach the PSP-1000 budget: in the low profile, read rarely used data from the EBOOT's DATA.PSAR when needed instead of keeping it resident. Full profile unchanged. Close as Won't fix if P79 passes without it.",
   "done_when": "Either closed as not needed after P79, or the low profile fits with on-demand loading and no load stalls during fights."
  },
  {
   "id": "P83",
   "order": 31,
   "phase": "P4",
   "size": "M",
   "status": "Ready",
   "depends": [
    "P82"
   ],
   "read": [
    "port/render/r_gu_mesh.c",
    "port/docs/PSP.md"
   ],
   "title": "Hardware speed: GU mesh renderer (fl_clay_draw)",
   "description": "Hardware profile (PSP-2003, PROFILE=1, 3v3 fight): frame 45-47 ms; game update 3; fl_clay_draw 17; func_003F0500__game packet build 13.5; fe_sync_handle 4.5 + func_003EA660 tex select 4.2; GE wait 0.2 (CPU-bound). fl_clay_draw does ~3.7k vertices/frame at ~1500 cycles each: remove soft-double maths (__extendsfdf2), inline fminf/floorf, move per-vertex transform/lighting (r_light, r_matrix) to GU hardware transform/lighting or the VFPU. Output must stay identical.",
   "done_when": "fl_clay_draw <= 4 ms/frame in a 3v3 fight on hardware (PROFILE=1), screenshots unchanged in PPSSPP."
  },
  {
   "id": "P84",
   "order": 32,
   "phase": "P4",
   "size": "L",
   "status": "Ready",
   "depends": [
    "P82"
   ],
   "read": [
    "port/psp/host/psp_fe_sync.c",
    "port/render/r_gu_mesh.c",
    "port/docs/PSP.md"
   ],
   "title": "Hardware speed: skip or cache the PS2 VU1 packet build",
   "description": "Hardware profile (PSP-2003, PROFILE=1, 3v3 fight): frame 45-47 ms; game update 3; fl_clay_draw 17; func_003F0500__game packet build 13.5; fe_sync_handle 4.5 + func_003EA660 tex select 4.2; GE wait 0.2 (CPU-bound). The game builds VU1 packets (func_003F0500__game) that the renderer parses back. Cache packets of unchanged (static stage) meshes across frames, or have the PSP renderer read mesh data directly, PSP-side only (--wrap).",
   "done_when": "Packet build <= 4 ms/frame in a 3v3 fight on hardware; logic hashes unchanged over a scripted run."
  },
  {
   "id": "P85",
   "order": 33,
   "phase": "P4",
   "size": "S",
   "status": "Ready",
   "depends": [
    "P82"
   ],
   "read": [
    "port/psp/host/psp_fe_sync.c",
    "port/docs/PSP.md"
   ],
   "title": "Hardware speed: texture sync / select per mesh",
   "description": "Hardware profile (PSP-2003, PROFILE=1, 3v3 fight): frame 45-47 ms; game update 3; fl_clay_draw 17; func_003F0500__game packet build 13.5; fe_sync_handle 4.5 + func_003EA660 tex select 4.2; GE wait 0.2 (CPU-bound). fe_sync_handle and func_003EA660 tex select run for every mesh: skip them when the handle/state has not changed (memo), PSP-side.",
   "done_when": "Texture sync + select <= 2 ms/frame in a 3v3 fight on hardware; no texture regressions."
  },
  {
   "id": "P86",
   "order": 34,
   "phase": "P4",
   "size": "S",
   "status": "Ready",
   "depends": [
    "P83",
    "P84",
    "P85"
   ],
   "read": [
    "port/docs/PSP.md"
   ],
   "title": "Hardware speed: re-profile fights and menus",
   "description": "Hardware profile (PSP-2003, PROFILE=1, 3v3 fight): frame 45-47 ms; game update 3; fl_clay_draw 17; func_003F0500__game packet build 13.5; fe_sync_handle 4.5 + func_003EA660 tex select 4.2; GE wait 0.2 (CPU-bound). After P83-P85, profile intro, character select and a 3v3 fight on hardware again (menus were 19-23 ms) and ticket what remains.",
   "done_when": "PSP.md has the new hardware profile; fights and menus at 59.94 fps or new tickets for the rest."
  }
 ]
};
