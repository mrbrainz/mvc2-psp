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

# fe_mesh.c (P96): everything func_003F0500 does between the game's body and
# fl_clay_draw (state sync, texture re-sync, the per-draw render-state
# hand-off, the dump/verbose logs) is one call, psp_mesh_predraw()
# (port/psp/host/psp_mesh_state.c); the upstream lines stay in the copy
# under #if 0.
psp_patch(port/host/fe/fe_mesh.c port/host/fe/fe_mesh.c
"        fe_sync_state();
        {   /* textures the game (re)loaded into a record since it was last synced */"
"        /* PSP patch (port/psp/patches, P96): port/psp/host/psp_mesh_state.c */
        { extern void psp_mesh_predraw(int, u32); psp_mesh_predraw(h, fmt); }
#if 0
        fe_sync_state();
        {   /* textures the game (re)loaded into a record since it was last synced */"
"        fl_clay_draw(h);
    }
    return r;"
"#endif
        fl_clay_draw(h);
    }
    return r;")
