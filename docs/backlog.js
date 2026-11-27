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
  "P124",
  "P125",
  "P126",
  "P127",
  "P128",
  "P129",
  "P13",
  "P130",
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
  "P51",
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
  "P72",
  "P73",
  "P74",
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
   "order": 54,
   "phase": "P6",
   "size": "M",
   "status": "Backlog",
   "depends": [
    "P76",
    "P77",
    "P78",
    "P80"
   ],
   "read": [
    "port/docs/PSP.md",
    "port/psp/scripts/arcade_psp.txt"
   ],
   "title": "PSP-1000 full arcade run",
   "description": "Full scripted arcade run in PPSSPP with the PSP-1000 model and the low profile and P80's on-demand loading. Record memory headroom and frame times.",
   "done_when": "Arcade run completes on the PSP-1000 model with headroom recorded in PSP.md; README minimum model updated."
  },
  {
   "id": "P80",
   "order": 53,
   "phase": "P6",
   "size": "L",
   "status": "Backlog",
   "depends": [],
   "read": [
    "port/psp/host/psp_ee_arena.c",
    "port/docs/PSP.md"
   ],
   "title": "Low profile: fit the PSP-1000 (future goal, parked)",
   "description": "On-demand loading does not work: the arena is seven fixed game heaps (15.2 MB, 12.0 MB ever written) read through pointers (PSP.md \"Game heaps in the arena (P80)\"). The PSP-1000 is ~19.4 MB short; candidates: the 4 MB volatile partition, music streaming (P78), trimming the heaps to their high-water plus margin, renderer budgets (P76), and PSP-1000-only visual cuts (16-bit textures, simpler stage background). First measure the stage's share of renderer memory and whether D_004C2878 is used outside arcade mode.",
   "done_when": "The low profile fits the PSP-1000's 15.7 MB (plus the volatile partition) in a full arcade run; full profile state dump unchanged."
  },
  {
   "id": "P131",
   "order": 43,
   "phase": "P2",
   "size": "S",
   "status": "Ready",
   "depends": [],
   "read": [
    ".github/workflows/psp-release.yml",
    "port/psp/snd0/CMakeLists.txt"
   ],
   "title": "Check the XMB music encoder on Windows and Linux",
   "description": "mvc2-snd0 and the workflow's snd0 jobs have only run as a local macOS build. Run psp-release by hand (workflow_dispatch) after pushing, then check the packages.",
   "done_when": "The manual run passes, every builder package holds mvc2-snd0, and the Windows and Linux ones make an SND0.AT3 that matches macOS's or plays."
  }
 ]
};
