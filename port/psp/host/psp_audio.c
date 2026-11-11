/* PSP audio binding (o32), as port/host/fe/fe_audio.c without the settings
 * and Dreamcast-disc options: the port mixer (port/audio/audio_psp.c) plays
 * the game's TSD queue and ADXT music from the disc image. */
#include <stdlib.h>
#include "host.h"
#include "host_cri.h"
#include "../../audio/audio.h"

static uint8_t *afs_loader(void *ctx, int ptid, int fid, size_t *size)
{
    GameDisc *d = host_disc();
    (void)ctx;
    if (!d || ptid < 0 || ptid > 3 || !d->afs_ok[ptid])
        return NULL;
    return disc_load(d, ptid, fid, size);
}

static int ops_start_afs(int ptid, int fid) { return padx_start_afs(ptid, fid); }
static void ops_stop(void) { padx_stop(); }
static int ops_get_stat(void) { return padx_get_stat(); }
static void ops_set_out_vol(int vol) { padx_set_out_vol(vol); }
static void ops_pause(int sw) { padx_pause(sw); }
static void ops_set_mono(int mono) { padx_set_output_mono(mono); }

static const HostAdxtOps ops = {
    ops_start_afs, ops_stop, ops_get_stat, ops_set_out_vol, ops_pause, ops_set_mono, NULL,
};

int psp_audio_init(void)
{
    if (getenv("NO_AUDIO"))
        return 0;
    if (audio_init(1) != 0) {
        host_log("audio: no sceAudio channel; headless");
        if (audio_init(0) != 0)
            return -1;
    }
    audio_set_loader(afs_loader, NULL);
    host_adxt_ops = &ops;
    return 0;
}
