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
  "P82",
  "P85",
  "P86",
  "P87",
  "P90",
  "P92"
 ],
 "activities": [
  {
   "id": "P95",
   "order": 33,
   "phase": "P4",
   "size": "M",
   "status": "Ready",
   "depends": [
    "P90"
   ],
   "read": [
    "src/b10s_0_f.c",
    "src/s9b_a.c",
    "src/s2m.c",
    "src/r3_d.c",
    "port/psp/host/psp_packet.c",
    "port/psp/host/psp_fe_sync.c"
   ],
   "title": "Hardware speed: GS upload bookkeeping in tex select (func_003EA660)",
   "description": "After P90, fight tex select is still 1.0-1.4 ms: lists with non-resident textures run the game's GS upload path (func_003EBE70/EBF30 allocation, EBA80/EBC20 insert, func_003DBD80 packet build). On PSP textures go through psp_fe_sync/VRAM, so find which of this bookkeeping the PSP renderer actually needs and replace the rest with a PSP-side equivalent.",
   "done_when": "Tex select <= 1 ms in the scripted 3v3 fight; game-state trace and rendering unchanged (A/B on hardware)."
  },
  {
   "id": "P84",
   "order": 33,
   "phase": "P4",
   "size": "L",
   "status": "In progress",
   "depends": [
    "P82",
    "P90"
   ],
   "read": [
    "port/psp/host/psp_fe_sync.c",
    "port/render/r_gu_mesh.c",
    "port/docs/PSP.md"
   ],
   "title": "Hardware speed: skip or cache the PS2 VU1 packet build",
   "description": "Hardware profile (PSP-2003, PROFILE=1, 3v3 fight): frame 45-47 ms; game update 3; fl_clay_draw 17; func_003F0500__game packet build 13.5; fe_sync_handle 4.5 + func_003EA660 tex select 4.2; GE wait 0.2 (CPU-bound). The game builds VU1 packets (func_003F0500__game) that the renderer parses back. Cache packets of unchanged (static stage) meshes across frames, or have the PSP renderer read mesh data directly, PSP-side only (--wrap).",
   "done_when": "Packet build <= 4 ms/frame in a 3v3 fight on hardware; logic hashes unchanged over a scripted run.",
   "note": "Partial: psp_packet.c replaces the draw body, skipping the VU1 setup callbacks, vertex/UV packets, DMA tags and kicks; same-binary A/B (MVC2_PACKET_FULL) over PSPLink: frame 45.3 -> 39.8 ms, packet build 14.9 -> 9.4, 6 fight shots over ~1500 frames identical (ra_state_hash covers the skipped packet memory, so screenshots are the check). Left in func_003F0500__game (~9 ms): (1) func_003EA660__game tex select ~4.5 ms (ticketed as P90); (2) the other ~4.5 ms inside lite_body is __wrap_fe_sync_handle (~3.7 ms, called from __wrap_func_003EA660 in psp_fe_sync.c:139) plus o->wC != D_004C2AD4 format-toggle packet rebuilds (psp_packet.c:43 calling func_003EE8C0). In psp_fe_sync.c, seen[] is keyed on combined (pi<<16)|ti (re-checking shared textures/palettes) and check_fixed hashes 64 B descriptors + up to 1024 B palette byte-by-byte every frame; either wrap func_003EA130/func_003EBD00 (src/nq235_a.c, where all tex/pal loads, frees and unlocks pass) to set a dirty bitmap, or deduplicate ti (1..384) and pi (1..256) separately and hash uint32_t words. On o->wC != D_004C2AD4 when pol_ofs is unchanged, lite_body only needs func_003EE930(o) + func_003E0F60 unless cmap[h].ever is set."
  },
  {
   "id": "P89",
   "order": 34,
   "phase": "P4",
   "size": "M",
   "status": "Ready",
   "depends": [
    "P86"
   ],
   "read": [
    "port/host/fe/fe_mesh.c",
    "port/psp/host/psp_packet.c",
    "port/render/r_gu.c",
    "port/render/r_gu_mesh.c"
   ],
   "title": "Hardware speed: PSP mesh wrapper (fe_mesh func_003F0500)",
   "description": "P86 profile: mesh draw minus fl_clay_draw minus packet build leaves ~4 ms a fight frame in the port's func_003F0500 wrapper (format check, vertex-write pull, texture sync per clay, render-state hand-off). Measure its parts on hardware and move the hot ones PSP-side (no upstream changes).",
   "done_when": "Wrapper <= 1 ms in the scripted 3v3 fight; screenshots bit-identical.",
   "note": "Code inspection of fe_mesh.c:204-270 (~115-168 calls/frame) shows three main costs: (1) fe_mesh.c:230-237 calls fl_set_render_state 7 times + fl_set_clip_matrix every draw (memcpying four 64 B matrices, clipm, and three 100 B light structs 0x59..0x5B), and r_gu.c:586-606 runs sqrtf + 3 float divides to normalize each light direction vector 3 times per mesh draw (~350-500 sqrtf and >1000 divides/frame) even though 0x16, 0x17, clipm and lights 0x59..0x5B only change at scene/camera setup — guard them with a memcmp/direction check in r_gu.c or read directly in a PSP wrapper. (2) fe_mesh.c:224-226 calls fl_clay_textures + fe_sync_handle on every draw, duplicating the __wrap_func_003EA660 sync that just ran at line 203. (3) fe_mesh.c:207-209 calls fl_clay_build whenever D_004C2AD4 != fl_clay_format(h), which calls r_gu_sync(), frees all mesh buffers and re-parses the POL chunk from scratch when only the format word toggled; if pol_ofs is unchanged, update me->fmt, me->desc = desc_select(fmt, me->key) and me->cvalid = 0 in place."
  },
  {
   "id": "P88",
   "order": 35,
   "phase": "P4",
   "size": "L",
   "status": "Ready",
   "depends": [
    "P86"
   ],
   "read": [
    "port/render/r_gu_mesh.c",
    "port/render/r_gu.c",
    "port/docs/PSP.md"
   ],
   "title": "Hardware speed: per-draw cost of mesh submits",
   "description": "P86 profile: fl_clay_draw 13-15 ms in character select and fights for ~100 draws (~150 us each), mostly fixed per-draw cost (GU submit, matrices, clip prep, list restarts), not per-vertex work. Options: record each static mesh's GE commands once and replay them with sceGuCallList, merge ranges sharing a texture, fewer list restarts. Output must stay bit-identical.",
   "done_when": "fl_clay_draw <= 6 ms in the scripted 3v3 fight over PSPLink; screenshots bit-identical.",
   "note": "Specific hotspots from PROFILE=2 (mvc2_psp.log) and code inspection: (1) Uncached RAM CPU read-back stalls: since P85 moved list_buf to 0x40000000, sceGuGetMemory returns uncached pointers, yet r_gu.c:1186 reads vv[idx[...]].x back from verts for depth-zoom z (use cached me->cpu or me->bb instead) and r_gu_mesh.c:569 (near_clip) reads from uncached vsrc when lit; also skip sceKernelDcacheWritebackRange (r_gu_mesh.c:585,700) when (uintptr_t)ptr & 0x40000000. (2) Pre-multiply scene-invariant matrices VP_full = view * proj * vpm * ge and VC = view * clipm once when 0x16/0x17/0x18/clipm change instead of 5-6 fl_mat_mul calls per draw (r_gu_mesh.c:708-751), and share bbox_corners between bbox_inside (line 756) and clip_bbox_inside (line 798). (3) 6-plane AABB frustum culling (all_out &= corner_code) in clip_bbox_inside/bbox_inside: in char select, 168/168 draws use clipping descriptors (desc 19/44..46) and 167 fail clip_bbox_inside, running clip_prepare on all 6324 verts (3.56 ms) when only 1 draw needs near_clip because off-screen meshes fail the guard-band check instead of being culled. (4) submit: tex bind takes 2.7 ms despite 95% VRAM hits because multi-material meshes alternate textures across me->range[], thrashing the 1-entry mesh_tex_key memo and sceGuTexFlush(); sort/merge MRange by (tex, diffuse, c0) at build_mesh time and move sceGuDisable(GU_CLIP_PLANES) (r_gu.c:1181) to list_open."
  },
  {
   "id": "P93",
   "order": 36,
   "phase": "P4",
   "size": "S",
   "status": "Ready",
   "depends": [
    "P86",
    "P88"
   ],
   "read": [
    "port/render/r_gu_mesh.c",
    "port/platform/platform_psp.c",
    "port/docs/PSP.md"
   ],
   "title": "Hardware speed: Allegrex VFPU for mesh matrices, clipping and lighting",
   "description": "All 4x4 matrix multiplies (fl_mat_mul), bounding-box corner transforms (bbox_corners), per-vertex clip preparation (clip_prepare), ADC transforms, and 3-light normal dot products (colours_lit) in r_gu_mesh.c currently run in scalar MIPS FPU C (~7-8 ms/frame in fights and character select). Enable THREAD_ATTR_VFPU on the main thread and add VFPU routines (vmmul.q, vtfm4.q, vdot.t) for matrix multiply and vertex/lighting loops while preserving visual fidelity.",
   "done_when": "Matrices + ADC + clip_prepare + colours_lit time in PROFILE=2 drops by >= 50% on PSP-2003; fight and character-select screenshots match."
  },
  {
   "id": "P94",
   "order": 37,
   "phase": "P4",
   "size": "S",
   "status": "Ready",
   "depends": [
    "P86"
   ],
   "read": [
    "port/cmake/psp-clang.cmake",
    "port/psp/CMakeLists.txt",
    "port/render/r_gu.c",
    "port/render/r_gu_mesh.c"
   ],
   "title": "Hardware speed: Allegrex ISA flags (-march=mips32r2) and 16 KB scratchpad SRAM",
   "description": "psp-clang.cmake currently builds all C code with -O2 -mips2, preventing Clang from emitting Allegrex/MIPS32r2 instructions (ext/ins bitfield extract/insert for colour/UV unpacking, seb/seh, movz/movn, madd/msub, clz), and the PSP's 16 KB 0-wait-state scratchpad SRAM at 0x00010000-0x00013FFF is unused. Test -march=mips32r2 (and -O3 on hot render/audio files) and place hot lookup/scratch buffers (unorm_lut[1024], per-draw clip/ADC scratch) in scratchpad SRAM, measuring the delta over PSPLink.",
   "done_when": "Measured frame-time improvement on PSP-2003 recorded in PSP.md; logic hashes and screenshots unchanged."
  },
  {
   "id": "P83",
   "order": 38,
   "phase": "P4",
   "size": "M",
   "status": "In progress",
   "depends": [
    "P82",
    "P88",
    "P93"
   ],
   "read": [
    "port/render/r_gu_mesh.c",
    "port/docs/PSP.md"
   ],
   "title": "Hardware speed: GU mesh renderer (fl_clay_draw)",
   "description": "Hardware profile (PSP-2003, PROFILE=1, 3v3 fight): frame 45-47 ms; game update 3; fl_clay_draw 17; func_003F0500__game packet build 13.5; fe_sync_handle 4.5 + func_003EA660 tex select 4.2; GE wait 0.2 (CPU-bound). fl_clay_draw does ~3.7k vertices/frame at ~1500 cycles each: remove soft-double maths (__extendsfdf2), inline fminf/floorf, move per-vertex transform/lighting (r_light, r_matrix) to GU hardware transform/lighting or the VFPU. Output must stay identical.",
   "done_when": "fl_clay_draw <= 4 ms/frame in a 3v3 fight on hardware (PROFILE=1), screenshots unchanged in PPSSPP.",
   "note": "Partial: exact inline floor/fmin, unorm LUT, bbox skip of ADC and near-clip prep, mesh texture/blend state memo; 15.8 -> 14.7 ms in a scripted 3v3 fight over PSPLink, screenshot bit-identical to HEAD. Hardware A/B: submit ~7 ms (~25 us per GU submit), ADC 2.4, clip prep 2.1, r_gu_begin list restarts ~1; per-draw splits with PROFILE=2. The __extendsfdf2 calls were only the MESH_DUMP fprintfs. Remaining per-draw and VFPU work split into P88 and P93."
  },
  {
   "id": "P91",
   "order": 39,
   "phase": "P4",
   "size": "M",
   "status": "Ready",
   "depends": [
    "P86",
    "P92"
   ],
   "read": [
    "port/docs/PSP.md",
    "port/psp/host/psp_prof.c",
    "port/psp/host/psp_audio_hw.c"
   ],
   "title": "Hardware speed: non-mesh rendering in fights and character select",
   "description": "P86 profile: render pass minus mesh draw is ~9 ms in fights and ~12 ms in character select (2D sprites, effects, characters, HUD). Split it with PROFILE timers on hardware and ticket the hot parts.",
   "done_when": "Split recorded in PSP.md; tickets for parts over 2 ms.",
   "note": "Watch out for audio thread preemption when splitting P91: psp_prof.c measures wall-clock time (sceRtcGetCurrentTick), and mvc2_audio runs on the same CPU at priority 0x12 (preempting the main thread at 0x20, psp_audio_hw.c:96) for ~2 fills per logic frame (607 fills / 300 frames in mvc2_psp.log, worst fill 4.9-9.3 ms). Run after P92 adds a prof_slot around device_fill (or subtracts audio fill ticks from interrupted timers in psp_prof.c) so audio CPU steal is separated from scene traversal (func_001538B0) and 2D batch flushes (func_00421270)."
  },
  {
   "id": "P41",
   "order": 40,
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
    "P94",
    "P95"
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
   "order": 41,
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
   "id": "P73",
   "order": 42,
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
   "order": 43,
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
   "order": 44,
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
   "order": 45,
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
   "order": 46,
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
   "order": 47,
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
   "order": 48,
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
   "order": 49,
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
   "order": 50,
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
   "id": "P51",
   "order": 51,
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
