# PSP-only patches to upstream sources, applied to copies at build time so
# the upstream files stay untouched (P77).  Run with cmake -P:
#   -DROOT=<checkout> -DOUT=<dir>: writes <OUT>/port/host/state_dump.c and
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
