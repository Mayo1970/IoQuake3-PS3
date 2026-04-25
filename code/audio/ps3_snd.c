/* ps3_snd.c -- PSL1GHT libaudio backend for ioQ3's software mixer. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ppu-types.h>
#include <audio/audio.h>
#include <sys/thread.h>
#include <sys/event_queue.h>
#include <sysmodule/sysmodule.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "client/snd_local.h"
#include "../audio/ps3_snd.h"

extern void ps3_log(const char *msg);

/* 4096 sample-pairs (~85 ms at 48 kHz). Mixer writes PCM, audio thread reads. */
#define PS3_AUDIO_RATE       48000
#define PS3_AUDIO_CHANNELS   2
#define PS3_AUDIO_BITS       16
#define PS3_AUDIO_SAMPLES    4096   /* sample-pairs in ring buffer */
#define PS3_AUDIO_BLOCK_SIZE 256    /* PS3 hardware block size (samples per ch) */

static audioPortConfig  ps3_audio_config;
static u32              ps3_audio_port = 0;
static volatile int     ps3_audio_running = 0;

static sys_event_queue_t ps3_audio_eventQ;
static sys_ipc_key_t     ps3_audio_queueKey;
static sys_ppu_thread_t  ps3_audio_thread;
static volatile int      ps3_audio_quit = 0;
static byte ps3_audio_buffer[PS3_AUDIO_SAMPLES * PS3_AUDIO_CHANNELS * (PS3_AUDIO_BITS / 8)];
static volatile u32 ps3_audio_blocks_written = 0;

/* Audio thread: converts 16-bit PCM blocks to 32-bit float for the audio port. */
static void ps3_audio_thread_func(void *arg)
{
    (void)arg;
    sys_event_t event;
    s16 *src = (s16 *)ps3_audio_buffer;
    int total_interleaved = PS3_AUDIO_SAMPLES * PS3_AUDIO_CHANNELS;
    int block_samples = PS3_AUDIO_BLOCK_SIZE * PS3_AUDIO_CHANNELS;
    int diag_count = 0;
    int timeout_count = 0;

    /* Memory-mapped port config pointers, valid for port lifetime */
    volatile u64 *readIndexPtr = (volatile u64 *)((u64)ps3_audio_config.readIndex);
    f32 *dataStart = (f32 *)((u64)ps3_audio_config.audioDataStart);
    u64 numBlocks = ps3_audio_config.numBlocks;

    {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "PS3_AUDIO: thread started readIndexPtr=%p dataStart=%p numBlocks=%u",
                 (void *)readIndexPtr, (void *)dataStart, (unsigned)numBlocks);
        ps3_log(buf);
    }

    if (!readIndexPtr || !dataStart || !numBlocks) {
        ps3_log("PS3_AUDIO: FATAL bad port config pointers, thread exiting");
        sysThreadExit(1);
        return;
    }

    while (!ps3_audio_quit) {
        s32 ret = sysEventQueueReceive(ps3_audio_eventQ, &event, 20 * 1000);
        if (ret != 0) {
            timeout_count++;
            if (timeout_count <= 3) {
                char buf[96];
                snprintf(buf, sizeof(buf),
                         "PS3_AUDIO: eventQ timeout #%d ret=0x%08x",
                         timeout_count, (unsigned)ret);
                ps3_log(buf);
            }
            continue;
        }

        if (ps3_audio_quit) break;

        /* Current block from live readIndex */
        u64 currentBlock = *readIndexPtr;
        u32 writeBlock = (u32)((currentBlock + 1) % numBlocks);

        f32 *dst = dataStart + writeBlock * PS3_AUDIO_CHANNELS * PS3_AUDIO_BLOCK_SIZE;

        /* Ring buffer read position */
        u32 ring_pos = (ps3_audio_blocks_written * (u32)block_samples) % (u32)total_interleaved;

        for (int i = 0; i < block_samples; i++) {
            int idx = (ring_pos + i) % total_interleaved;
            dst[i] = (f32)src[idx] / 32768.0f;
        }

        ps3_audio_blocks_written++;

        if (diag_count < 5) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "PS3_AUDIO: blk=%u curRd=%u wr=%u ring=%u/%d",
                     (unsigned)ps3_audio_blocks_written,
                     (unsigned)currentBlock, (unsigned)writeBlock,
                     (unsigned)ring_pos, total_interleaved);
            ps3_log(buf);
            diag_count++;
        }
    }

    ps3_log("PS3_AUDIO: thread exiting");
    sysThreadExit(0);
}

/* SNDDMA interface */
qboolean SNDDMA_Init(void)
{
    audioPortParam params;
    s32 ret;

    ps3_log("SNDDMA_Init: loading SYSMODULE_AUDIO");
    ret = sysModuleLoad(SYSMODULE_AUDIO);
    if (ret != 0 && ret != 0x8001112E) { /* 0x8001112E = already loaded */
        char buf[64];
        snprintf(buf, sizeof(buf), "SNDDMA_Init: sysModuleLoad(AUDIO) failed: 0x%08x", (unsigned)ret);
        ps3_log(buf);
        return qfalse;
    }

    ret = audioInit();
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioInit failed");
        return qfalse;
    }

    memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks   = AUDIO_BLOCK_8;
    params.attrib      = AUDIO_PORT_INITLEVEL;
    params.level       = 1.0f;

    ret = audioPortOpen(&params, &ps3_audio_port);
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioPortOpen failed");
        audioQuit();
        return qfalse;
    }

    ret = audioGetPortConfig(ps3_audio_port, &ps3_audio_config);
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioGetPortConfig failed");
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "SNDDMA_Init: readIndex=0x%08x audioDataStart=0x%08x status=%u ch=%lu nblk=%lu portSize=%u",
                 (unsigned)ps3_audio_config.readIndex,
                 (unsigned)ps3_audio_config.audioDataStart,
                 (unsigned)ps3_audio_config.status,
                 (unsigned long)ps3_audio_config.channelCount,
                 (unsigned long)ps3_audio_config.numBlocks,
                 (unsigned)ps3_audio_config.portSize);
        ps3_log(buf);
    }

    /* Event queue for audio server notifications */
    ret = audioCreateNotifyEventQueue(&ps3_audio_eventQ, &ps3_audio_queueKey);
    if (ret != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "SNDDMA_Init: audioCreateNotifyEventQueue failed: 0x%08x", (unsigned)ret);
        ps3_log(buf);
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    ret = audioSetNotifyEventQueue(ps3_audio_queueKey);
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioSetNotifyEventQueue failed");
        sysEventQueueDestroy(ps3_audio_eventQ, 0);
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    sysEventQueueDrain(ps3_audio_eventQ);

    ret = audioPortStart(ps3_audio_port);
    if (ret != 0) {
        ps3_log("SNDDMA_Init: audioPortStart failed");
        audioRemoveNotifyEventQueue(ps3_audio_queueKey);
        sysEventQueueDestroy(ps3_audio_eventQ, 0);
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    /* dma_t for ioQ3's mixer */
    memset(&dma, 0, sizeof(dma));
    dma.channels         = PS3_AUDIO_CHANNELS;
    dma.samples          = PS3_AUDIO_SAMPLES * PS3_AUDIO_CHANNELS;
    dma.fullsamples      = PS3_AUDIO_SAMPLES;
    dma.submission_chunk = PS3_AUDIO_SAMPLES / 4; /* 1024 pairs, ~21ms lead */
    dma.samplebits       = PS3_AUDIO_BITS;
    dma.speed            = PS3_AUDIO_RATE;
    dma.buffer           = ps3_audio_buffer;

    memset(ps3_audio_buffer, 0, sizeof(ps3_audio_buffer));
    ps3_audio_blocks_written = 0;

    ps3_audio_quit = 0;
    ps3_audio_running = 1;

    static char audio_thread_name[] = "AudioThread";
    ret = sysThreadCreate(&ps3_audio_thread, ps3_audio_thread_func, NULL,
                          1001, /* priority (slightly lower than main thread) */
                          0x4000, /* 16 KB stack */
                          THREAD_JOINABLE,
                          audio_thread_name);
    if (ret != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "SNDDMA_Init: sysThreadCreate failed: 0x%08x", (unsigned)ret);
        ps3_log(buf);
        ps3_audio_running = 0;
        audioPortStop(ps3_audio_port);
        audioRemoveNotifyEventQueue(ps3_audio_queueKey);
        sysEventQueueDestroy(ps3_audio_eventQ, 0);
        audioPortClose(ps3_audio_port);
        audioQuit();
        return qfalse;
    }

    {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "SNDDMA_Init: OK - %d Hz, %d-bit, %d ch, %d sample-pairs, thread started",
                 PS3_AUDIO_RATE, PS3_AUDIO_BITS, PS3_AUDIO_CHANNELS, PS3_AUDIO_SAMPLES);
        ps3_log(buf);
    }
    return qtrue;
}

int SNDDMA_GetDMAPos(void)
{
    if (!ps3_audio_running) return 0;
    u32 blocks = ps3_audio_blocks_written;
    u32 pairs = (blocks * PS3_AUDIO_BLOCK_SIZE) % PS3_AUDIO_SAMPLES;
    return (int)(pairs * PS3_AUDIO_CHANNELS);
}

void SNDDMA_Shutdown(void)
{
    if (!ps3_audio_running) return;

    ps3_audio_quit = 1;
    ps3_audio_running = 0;

    u64 retval;
    sysThreadJoin(ps3_audio_thread, &retval);

    audioPortStop(ps3_audio_port);
    audioRemoveNotifyEventQueue(ps3_audio_queueKey);
    sysEventQueueDestroy(ps3_audio_eventQ, 0);
    audioPortClose(ps3_audio_port);
    audioQuit();

    ps3_log("SNDDMA_Shutdown: done");
}

void SNDDMA_BeginPainting(void) {}
void SNDDMA_Submit(void) {}

/* VoIP capture stubs */
void SNDDMA_StartCapture(void) {}
int  SNDDMA_AvailableCaptureSamples(void) { return 0; }
void SNDDMA_Capture(int samples, byte *data) { (void)samples; (void)data; }
void SNDDMA_StopCapture(void) {}
void SNDDMA_MasterGain(float val) { (void)val; }

/* S_Init dispatch -- bypasses snd_main.c, forwards to S_Base_* directly. */
extern void S_Update_(void);
extern void S_Base_Shutdown(void);
extern void S_Base_StartSound(vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx);
extern void S_Base_StartLocalSound(sfxHandle_t sfx, int channelNum);
extern void S_Base_StopAllSounds(void);
extern void S_Base_StopLoopingSound(int entityNum);
extern void S_Base_ClearLoopingSounds(qboolean killall);
extern void S_Base_AddLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx);
extern void S_Base_AddRealLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx);
extern void S_Base_UpdateEntityPosition(int entityNum, const vec3_t origin);
extern void S_Base_Respatialize(int entityNum, const vec3_t origin, vec3_t axis[3], int inwater);
extern sfxHandle_t S_Base_RegisterSound(const char *sample, qboolean compressed);
extern void S_Base_BeginRegistration(void);
extern void S_Base_ClearSoundBuffer(void);
extern void S_Base_DisableSounds(void);
extern void S_Base_RawSamples(int stream, int samples, int rate, int width, int s_channels, const byte *data, float volume, int entityNum);

extern void S_CodecInit(void);
cvar_t *s_volume;
cvar_t *s_muted;
cvar_t *s_musicVolume;
cvar_t *s_doppler;

static soundInterface_t s_snd_if;

void S_Init(void)
{
    s_volume      = Cvar_Get("s_volume",      "0.8",  CVAR_ARCHIVE);
    s_muted       = Cvar_Get("s_muted",       "0",    CVAR_ROM);
    s_musicVolume = Cvar_Get("s_musicVolume", "0.25", CVAR_ARCHIVE);
    s_doppler     = Cvar_Get("s_doppler",     "1",    CVAR_ARCHIVE);

    S_CodecInit();

    Com_Memset(&s_snd_if, 0, sizeof(s_snd_if));
    S_Base_Init(&s_snd_if);
}

void S_Shutdown(void)
{
    if (s_snd_if.Shutdown)
        s_snd_if.Shutdown();
    SNDDMA_Shutdown();
    Com_Memset(&s_snd_if, 0, sizeof(s_snd_if));
}

void        S_Update(void)                                             { S_Update_(); }
void        S_BeginRegistration(void)                                  { S_Base_BeginRegistration(); }
sfxHandle_t S_RegisterSound(const char *n, qboolean comp)             { return S_Base_RegisterSound(n, comp); }
void        S_StartSound(vec3_t o,int n,int c,sfxHandle_t h)          { S_Base_StartSound(o,n,c,h); }
void        S_StartLocalSound(sfxHandle_t h,int c)                    { S_Base_StartLocalSound(h,c); }
void        S_StopAllSounds(void)                                      { S_Base_StopAllSounds(); }
void        S_StopLoopingSound(int e)                                  { S_Base_StopLoopingSound(e); }
void        S_ClearLoopingSounds(qboolean k)                           { S_Base_ClearLoopingSounds(k); }
void        S_AddLoopingSound(int n,const vec3_t o,const vec3_t v,sfxHandle_t h)     { S_Base_AddLoopingSound(n,o,v,h); }
void        S_AddRealLoopingSound(int n,const vec3_t o,const vec3_t v,sfxHandle_t h) { S_Base_AddRealLoopingSound(n,o,v,h); }
void        S_UpdateEntityPosition(int n,const vec3_t o)              { S_Base_UpdateEntityPosition(n,o); }
void        S_Respatialize(int n,const vec3_t o,vec3_t ax[3],int i)   { S_Base_Respatialize(n,o,ax,i); }
void        S_ClearSoundBuffer(void)                                   { S_Base_ClearSoundBuffer(); }
void        S_DisableSounds(void)                                      { S_Base_DisableSounds(); }
void        S_StartBackgroundTrack(const char *i,const char *l)       { (void)i;(void)l; }
void        S_StopBackgroundTrack(void)                                { }
void        S_RawSamples(int stream,int samples,int rate,int width,int channels,const byte *d,float v,int e)
                                                                       { S_Base_RawSamples(stream,samples,rate,width,channels,d,v,e); }

/* Init/shutdown called from ps3_main.c. Actual audio init in SNDDMA_Init. */
void PS3_Snd_Init(void) {}

void PS3_Snd_Shutdown(void)
{
    SNDDMA_Shutdown();
}
