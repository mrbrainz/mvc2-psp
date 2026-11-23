# PSP-only patches to upstream sources, applied to copies at build time so
# the upstream files stay untouched (P77).  Run with cmake -P:
#   -DROOT=<checkout> -DOUT=<dir>: writes <OUT>/port/host/state_dump.c,
#   <OUT>/port/host/fe/fe_mesh.c and
#   <OUT>/port/tools/oracle/dump_format.h (same layout, so state_dump.c's
#   relative include "../tools/oracle/dump_format.h" finds the patched copy).
# A patch whose text upstream no longer has stops the build: update it here.

function(psp_patch src dst)
  file(READ "${ROOT}/${src}" text)
  math(EXPR last "${ARGC} - 1")
  if(last GREATER 1)
    foreach(i RANGE 2 ${last} 2)
      math(EXPR j "${i} + 1")
      set(from "${ARGV${i}}")
      set(to "${ARGV${j}}")
      string(FIND "${text}" "${from}" at)
      if(at EQUAL -1)
        message(FATAL_ERROR "port/psp/patches: ${src} no longer contains:\n${from}\nUpdate apply_upstream_patches.cmake.")
      endif()
      string(REPLACE "${from}" "${to}" text "${text}")
    endforeach()
  endif()
  file(WRITE "${OUT}/${dst}.tmp" "${text}")
  file(COPY_FILE "${OUT}/${dst}.tmp" "${OUT}/${dst}" ONLY_IF_DIFFERENT)
  file(REMOVE "${OUT}/${dst}.tmp")
endfunction()

# dump_format.h: the 1.5 MB oracle state buffer (MVC2_STATE_DUMP only) is
# allocated on first use instead of sitting in every PSP build's BSS.
psp_patch(port/tools/oracle/dump_format.h port/tools/oracle/dump_format.h
"static uint8_t oracle_ram_buf[ORACLE_HASH_HI - ORACLE_HASH_LO];"
"/* PSP patch (port/psp/patches, P77): allocated on first use, not BSS */
static uint8_t (*oracle_ram_buf_p)[ORACLE_HASH_HI - ORACLE_HASH_LO];
static uint8_t (*oracle_ram_buf_get(void))[ORACLE_HASH_HI - ORACLE_HASH_LO]
{
    if (!oracle_ram_buf_p && !(oracle_ram_buf_p = malloc(sizeof *oracle_ram_buf_p))) {
        fprintf(stderr, \"oracle: no memory for the state buffer\\n\");
        abort();
    }
    return oracle_ram_buf_p;
}
#define oracle_ram_buf (*oracle_ram_buf_get())")

# state_dump.c: unchanged, compiled from the copy so it includes the patched header
psp_patch(port/host/state_dump.c port/host/state_dump.c)

# fe_mesh.c (P96): the per-draw render-state hand-off in func_003F0500 goes
# through psp_mesh_state.c, which lets the renderer read the game's copies.
psp_patch(port/host/fe/fe_mesh.c port/host/fe/fe_mesh.c
"        fl_set_render_state(0x16, (uintptr_t)EE_BSS_PTR(0x0061C720));
        fl_set_render_state(0x17, (uintptr_t)EE_BSS_PTR(0x0061C6E0));
        fl_set_render_state(0x19, (uintptr_t)EE_BSS_PTR(0x0061BEE0));
        fl_set_render_state(0x1A, (uintptr_t)EE_BSS_PTR(0x0061BF20));
        fl_set_clip_matrix((const float *)EE_BSS_PTR(0x0061BDE0));
        fl_set_render_state(0x59, (uintptr_t)EE_BSS_PTR(0x0061B2F0));
        fl_set_render_state(0x5A, (uintptr_t)EE_BSS_PTR(0x0061B354));
        fl_set_render_state(0x5B, (uintptr_t)EE_BSS_PTR(0x0061B3B8));"
"        /* PSP patch (port/psp/patches, P96): port/psp/host/psp_mesh_state.c */
        { extern void psp_mesh_handoff(void); psp_mesh_handoff(); }")
