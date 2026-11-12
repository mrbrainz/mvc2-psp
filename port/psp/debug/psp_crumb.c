/* Debug (MVC2_PSP_CRUMBS): called by the psp_crumbs.S trampolines. */
#include <stdio.h>

void psp_crumb(const char *name)
{
    fprintf(stderr, "[crumb] %s\n", name);
}
