/*
 * ioquake3-PS3: audio/ps3_snd.c
 * Audio backend using PSL1GHT's libaudio.
 *
 * ioQ3's software mixer (snd_dma.c / snd_mix.c) fills a 16-bit PCM ring
 * buffer. A dedicated audio thread waits for the PS3 audio server event,
 * then converts one block of 16-bit samples to 32-bit float and writes
 * it into the audio port's circular buffer.
 *
 * PS3 audio hardware:
 *   - 48 kHz native sample rate (fixed)
 *   - 256 samples per block (fixed by hardware: AUDIO_BLOCK_SAMPLES)
 *   - Stereo (2 channels)
 *   - 32-bit float internally, range [-1.0, 1.0], interleaved L/R
 *   - Event-driven: audio server sends event when next block is needed
 */

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

/* ----------------------------------------------------------------
 * Configuration
 *
 * PS3 audio blocks are 256 samples (fixed). At 48 kHz stereo, each
 * block is 256 * 2 * sizeof(float) = 2048 bytes for the port buffer.
 *
 * We use a ring buffer of 4096 sample-pairs (~85 ms at 48 kHz).
 * ioQ3's mixer writes 16-bit PCM into this ring; our audio thread
 * reads from it and converts to float for the audio port.
 * ---------------------------------------------------------------- */
#define PS3_AUDIO_RATE       48000
#define PS3_AUDIO_CHANNELS   2
#define PS3_AUDIO_BITS       16
#define PS3_AUDIO_SAMPLES    4096   /* sample-pairs in ring buffer */
#define PS3_AUDIO_BLOCK_SIZE 256    /* PS3 hardware block size (samples per ch) */

static audioPortConfig  ps3_audio_config;
static u32              ps3_audio_port = 0;
static volatile int     ps3_audio_running = 0;

/* Event queue for audio server notifications */
static sys_event_queue_t ps3_audio_eventQ;
static sys_ipc_key_t     ps3_audio_queueKey;

/* Audio thread */
static sys_ppu_thread_t  ps3_audio_thread;
static volatile int      ps3_audio_quit = 0;

/* Ring buffer shared between ioQ3 mixer and audio thread */
static byte ps3_audio_buffer[PS3_AUDIO_SAMPLES * PS3_AUDIO_CHANNELS * (PS3_AUDIO_BITS / 8)];

/* Block counter: incremented each time the audio thread writes a block
 * to the hardware port. SNDDMA_GetDMAPos derives the read position
 * directly from this so the mixer and audio thread stay synchronized. */
static volatile u32 ps3_audio_blocks_written = 0;

/* ----------------------------------------------------------------
 * Audio thread: waits for events from the audio server, then
 * converts one block of 16-bit PCM to 32-bit float and writes
 * it into the audio port's circular buffer.
 *
 * PSL1GHT audio port layout:
 *   - audioPortConfig.readIndex is a u32 holding a POINTER to a
 *     u64 that the audio server updates in real-time with the
 *     current block index being read.
 *   - audioPortConfig.audioDataStart is a u32 holding a POINTER
 *     to the base of the port's float buffer.
 *   - We write to the block AFTER the one being read.
 * ---------------------------------------------------------------- */
static void ps3_audio_thread_func(void *arg)
{
    (void)arg;
    sys_event_t event;
    s16 *src = (s16 *)ps3_audio_buffer;
    int total_interleaved = PS3_AUDIO_SAMPLES * PS3_AUDIO_CHANNELS;
    int block_samples = PS3_AUDIO_BLOCK_SIZE * PS3_AUDIO_CHANNELS;
    int diag_count = 0;
    int timeout_count = 0;

    /* Cache port config pointers -- these are memory-mapped and stay valid
     * for the lifetime of the audio port. */
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

        /* Dereference the live readIndex pointer to get current block */
        u64 currentBlock = *readIndexPtr;
        u32 writeBlock = (u32)((currentBlock + 1) % numBlocks);

        f32 *dst = dataStart + writeBlock * PS3_AUDIO_CHANNELS * PS3_AUDIO_BLOCK_SIZE;

        /* Read position in our 16-bit PCM ring buffer */
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

/* ----------------------------------------------------------------
 * SNDDMA interface -- called by ioQ3's sound system
 * ---------------------------------------------------------------- */
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

    /* Create and register the event queue for audio notifications */
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

    /* Drain any pending events before starting */
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

    /* Fill in dma_t for ioQ3's mixer */
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

    /* Start the audio thread */
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

    /* Return hardware read position derived from the audio thread's block
     * counter. This keeps the mixer's write cursor and the audio thread's
     * read cursor synchronized through the same counter.
     *
     * Each block consumes PS3_AUDIO_BLOCK_SIZE sample-pairs.
     * We return interleaved sample count so S_GetSoundtime's division
     * by dma.channels yields the correct sample-pair offset. */
    u32 blocks = ps3_audio_blocks_written;
    u32 pairs = (blocks * PS3_AUDIO_BLOCK_SIZE) % PS3_AUDIO_SAMPLES;
    return (int)(pairs * PS3_AUDIO_CHANNELS);
}

void SNDDMA_Shutdown(void)
{
    if (!ps3_audio_running) return;

    /* Signal the audio thread to exit */
    ps3_audio_quit = 1;
    ps3_audio_running = 0;

    /* Wait for thread to finish */
    u64 retval;
    sysThreadJoin(ps3_audio_thread, &retval);

    audioPortStop(ps3_audio_port);
    audioRemoveNotifyEventQueue(ps3_audio_queueKey);
    sysEventQueueDestroy(ps3_audio_eventQ, 0);
    audioPortClose(ps3_audio_port);
    audioQuit();

    ps3_log("SNDDMA_Shutdown: done");
}

void SNDDMA_BeginPainting(void)
{
    /* No locking needed -- the mixer and audio thread access different
     * regions of the ring buffer (mixer writes ahead, thread reads behind). */
}

void SNDDMA_Submit(void)
{
    /* No-op: the audio thread handles feeding the hardware via event queue.
     * The mixer writes directly into dma.buffer (= ps3_audio_buffer); the
     * audio thread reads from it independently. */
}

/* VoIP capture stubs -- no microphone support on PS3 homebrew */
void SNDDMA_StartCapture(void) {}
int  SNDDMA_AvailableCaptureSamples(void) { return 0; }
void SNDDMA_Capture(int samples, byte *data) { (void)samples; (void)data; }
void SNDDMA_StopCapture(void) {}
void SNDDMA_MasterGain(float val) { (void)val; }

/* ----------------------------------------------------------------
 * S_Init dispatch -- replaces snd_main.c which depends on SDL/OpenAL.
 *
 * We bypass snd_main.c entirely and forward S_* calls directly to
 * S_Base_* (the software DMA mixer in snd_dma.c).
 * Pattern follows the Wii port.
 * ---------------------------------------------------------------- */

/* Declarations from snd_dma.c not exposed in snd_local.h */
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

/* Codec init from snd_codec.c */
extern void S_CodecInit(void);

/* Cvars normally defined by snd_main.c -- snd_dma.c/snd_mix.c reference these */
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

/* ----------------------------------------------------------------
 * PS3 audio init/shutdown -- called from ps3_main.c
 * ---------------------------------------------------------------- */
void PS3_Snd_Init(void)
{
    /* Actual audio init happens in SNDDMA_Init when S_Init calls S_Base_Init */
}

void PS3_Snd_Shutdown(void)
{
    SNDDMA_Shutdown();
}
