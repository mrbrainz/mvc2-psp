/* sceAudio output thread (psp_audio_hw.c, EABI) for the o32 mixer
 * (port/audio/audio_psp.c). Every call takes <=4 int/pointer args. */
#ifndef PSP_AUDIO_HW_H
#define PSP_AUDIO_HW_H
#define PSP_AUDIO_FRAMES 1024          /* per buffer: 21.3 ms at 48 kHz */
typedef void (*PspAudioFill)(short *dst, int frames);  /* o32, on the audio thread */
int psp_audio_hw_start(PspAudioFill fill);
void psp_audio_hw_stop(void);
void psp_audio_hw_lock(void);
void psp_audio_hw_unlock(void);
void psp_audio_hw_report(void);        /* fills, worst fill time, underruns since the last report */
#endif
