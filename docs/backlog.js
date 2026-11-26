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
  "P100",
  "P101",
  "P102",
  "P103",
  "P107",
  "P108",
  "P109",
  "P11",
  "P110",
  "P112",
  "P113",
  "P114",
  "P115",
  "P116",
  "P118",
  "P119",
  "P12",
  "P121",
  "P122",
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
  "P73",
  "P77",
  "P81",
  "P82",
  "P83",
  "P84",
  "P85",
  "P86",
  "P87",
  "P88",
  "P89",
  "P90",
  "P91",
  "P92",
  "P93",
  "P95",
  "P96",
  "P97",
  "P98",
  "P99"
 ],
 "activities": [
  {
   "id": "P117",
   "order": 38,
   "phase": "P4",
   "size": "M",
   "status": "Blocked",
   "depends": [
    "P115"
   ],
   "read": [
    "port/psp/host/psp_me.c",
    "port/psp/host/psp_entry.c",
    "port/docs/PSP.md"
   ],
   "title": "Media Engine: give it back to the firmware before sleep, take it again on wake",
   "description": "P115: sleep with our code on the ME powers the PSP off; without ME use it works (2026-11-25). On PSP_POWER_CB_SUSPENDING: park our ME code, put back the reset-vector words psp_me.c overwrote (saved at boot) and restart the firmware's ME code so its sleep handling runs; on resume boot ours again (the P104 path waits for or rebuilds any pending job).",
   "done_when": "Sleep and wake from a fight with MVC2_ME_BUILD=1 work on the PSP-2003 and the fight goes on with the ME building.",
   "note": "Cold give-back built (power lock, ME RAM saved at take, firmware boot words, state -3 -> -4): works over PSPLink without sleep, but sleep still powers off (PSP.md). Needs the firmware ME halted with its sleep-save call at take and resumed from it at give-back; blocked on a decision, since the ME brings no speed now (P116)."
  },
  {
   "id": "P104",
   "order": 45,
   "phase": "P4",
   "size": "L",
   "status": "Backlog",
   "depends": [
    "P103",
    "P117"
   ],
   "read": [
    "port/docs/PSP.md",
    "port/psp/host/psp_entry.c"
   ],
   "title": "Hardware speed: build the GE list on the Media Engine",
   "description": "Ideas from gibletto/mvc2-naomi-dc ports/port/psp (AGPL-3.0: ideas only, no code copied). Take the Media Engine from the firmware (kernel access via ARK-4's kubridge; the firmware's ME code halted as for sleep, ours started at its reset vector, given back before sleep and taken again on wake) and run P103's list build there while the main CPU runs the game's next frame; the GE draws it a frame later. The ME's caches are not coherent with the main CPU's: shared data written back/invalidated by hand, the handshake word in its own cache line through the uncached mirror. Double-buffer the list, vertices and anything the record refers to.",
   "done_when": "A one-frame-late build check (MVC2_REC_CHECK) passes; List build off the main CPU in fights; frame time and display fps in a 3v3 fight on the PSP-2003 measured before and after; sleep/resume works; screenshots identical apart from the one-frame delay.",
   "note": "Design (a) built (MVC2_ME_BUILD=1, PSP.md): the ME building the words measured no better than the main-CPU writer (P116), 16.24 vs 16.12-16.18 ms render pass. Next try: move the per-item prep (texture key, depth zoom) to the ME, after P115."
  },
  {
   "id": "P106",
   "order": 46,
   "phase": "P4",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P104"
   ],
   "read": [
    "port/docs/PSP.md",
    "port/platform/platform_psp.c"
   ],
   "title": "Hardware speed: audio mixing on the Media Engine",
   "description": "Ideas from gibletto/mvc2-naomi-dc ports/port/psp (AGPL-3.0: ideas only, no code copied). Once P104 owns the ME, run the frame's sound mix there after the list build, if the profile shows mixing costs the main CPU measurable time per frame. Output must stay sample-identical.",
   "done_when": "Mix time per frame measured on hardware; if moved, WAV output identical to the main-CPU mix and frame time measured before and after.",
   "note": "Measured 2026-11-25 (PSP.md): the mix takes 1.2-1.85 ms per 60 Hz frame in fights (mean 1.55), up to 2.0 in character select; upper bound (includes waits on audio_lock). Needs P117 before any ME use."
  },
  {
   "id": "P41",
   "order": 47,
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
    "P86",
    "P88",
    "P89",
    "P90",
    "P91",
    "P92",
    "P93",
    "P95",
    "P96"
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
   "order": 48,
   "phase": "P4",
   "size": "S",
   "status": "Blocked",
   "depends": [
    "P06",
    "P123"
   ],
   "read": [
    "port/docs/ORACLE_FUZZ.md"
   ],
   "title": "Determinism check on PSP",
   "description": "Run fuzz seeds on PSP build and compare hashes with native.",
   "done_when": "Selected seeds match frame for frame.",
   "note": "Reference: port/psp/ref (wasm runner with the PSP's game-flow wraps), PSP.md 'Determinism check (P42)'. Seeds 1, 17, 40, 240 (PPSSPP; seed 1 also PSP-2003): boot, menus, select and RNG identical to the fight; then float drift from build-time folding (P122), seeds 17 and 40 split. Fixed on the way: libgcc double->float rounding (psp_fpu.c), newlib cosf (psp_libm.c, musl)."
  },
  {
   "id": "P123",
   "order": 58,
   "phase": "P4",
   "size": "M",
   "status": "Ready",
   "depends": [],
   "read": [
    "port/docs/PSP.md",
    "port/psp/ref/ref_main.c"
   ],
   "title": "Fuzz seeds 17 and 40 split from the reference with identical game memory",
   "description": "Seed 40 (PPSSPP, after P122): EE BSS identical to the reference through frame 2399 (GS packet words aside); at frame 2400 a character's sub-state p[0x20D] selects func_002A5D10 (jtbl_00458680) on the PSP only, which spawns func_0015D010's 15 particles (60 libc rand calls), while the reference advances the game RNG instead. Same with every PSP-side replacement set to the game's body. The deciding input is outside EE BSS: heap, sound status (PSP mixes on a real-time thread, the reference 1/60 s per frame) or pad data. Seed 17 splits at frame 3889, probably the same; seed 240 has one word 1 ulp off at frame 4755 (0x004F2D18).",
   "done_when": "The input that differs is named and made to match; P42 seeds 1, 17, 40, 240 match the reference frame for frame."
  },
  {
   "id": "P74",
   "order": 49,
   "phase": "P6",
   "size": "S",
   "status": "Ready",
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
   "order": 50,
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
   "order": 51,
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
   "id": "P78",
   "order": 52,
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
   "order": 53,
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
   "order": 54,
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
   "id": "P72",
   "order": 55,
   "phase": "P5",
   "size": "XS",
   "status": "Ready",
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
   "id": "P51",
   "order": 56,
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
  }
 ]
};
