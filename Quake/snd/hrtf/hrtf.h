#pragma once

#define FRAME_SIZE (1024 * 2)
#define SFX_CACHE_MAX 1024
#define CHANNELS_MAX 1024
#define NUM_OUTPUT_CHANNELS 2

// For looped static sound sources.
static const int ENTNUM_STATIC = -1;

// Gets assigned to the first available channel.
static const int ENTCHANNEL_AUTO = 0;

// For ambients and menu sounds, doesn't get spatialized.
static const int ENTCHANNEL_PLAY_GLOBALLY = -1;

// Get a sample width out of a SDL_AudioSpec.format.
#define SDL_AUDIO_SAMPLESIZE(x) (SDL_AUDIO_BITSIZE((x)) / 8)

// Adjust a [0-1] volume input to match the non-linear ear response when multiplying a sample.
#define TO_PERCEPTIBLE_VOLUME(x) ((exp((x))-1.f) / (M_E-1.f))

typedef struct {
	IPLAudioBuffer buf;
	int loopStart;
} hrtf_sfx_cache_entry_t;

typedef struct {
	// Channel 0 is an auto-allocate channel, the others override anything
	// already running on that entity/channel pair.
	int entnum;
	int entchannel;

	float volume; // [0-1]
	// [0-4], an attenuation of 0 will play full volume everywhere in the
	// level. Larger attenuations will drop off.  (max 4 attenuation)
	float attenuation;
	sfx_t* sfx; // NULL = inactive
	vec3_t origin;

	int spent; // samples already sent to output.
	int loopStartSample;  // sample to go back to when looping, -1 to disable looping.
	IPLBinauralEffect iplBinauralEffect;
	IPLDirectEffect iplDirectEffect;
} hrtf_channel_t;

typedef struct {
	qboolean initialized;
	SDL_AudioDeviceID deviceID;
	SDL_AudioSpec audioSpec;
	float volume;

	sfx_t* sfxCache;
	size_t nextSFXCacheEntry;
	size_t precachedAudioBuffersSize;

	hrtf_channel_t mixer[CHANNELS_MAX];
	float underwater;

	// Steam Audio, IPL, Phonon: make up your mind. IPL will be used because
	// it's shorter.
	IPLContext iplContext;
	IPLAudioSettings iplAudioSettings;
	IPLHRTF iplHRTF;

	vec3_t listenerOrigin;
	vec3_t listenerForward;
	vec3_t listenerRight;
	vec3_t listenerUp;

	// {{{ Used in the SDL callback, don't r/w without locking audio device.
	qboolean frameBufferFull;
	qboolean starved;
	size_t frameBufferSize;
	IPLfloat32* frameBuffer;

	qboolean bgmFrameFull;
	IPLAudioBuffer bgmFrame;
	// }}}
} hrtf_state_t;

// {{{ util
size_t iplAudioBufferDataSize(const IPLAudioBuffer* buf);
void iplAudioBufferCopy(IPLAudioBuffer* dst, const IPLAudioBuffer* src);
void iplAudioBufferZero(IPLAudioBuffer* dst);

// Returns true if the buffer was {re-,}allocated and thus contains uninitialized memory.
qboolean AllocScratchBuffer(IPLContext ctx, int channels, int samples, IPLAudioBuffer* dst);

qboolean LoadWAV(hrtf_state_t* state, const char* name, IPLAudioBuffer* dst, int* loopStart);
qboolean WAVToIPLAudioBuffer(hrtf_state_t* state, SDL_AudioSpec spec, byte* src, size_t srcLen, IPLAudioBuffer* dst);

sfx_t* FindOrAllocateCacheEntry(hrtf_state_t* state, const char* name);
sfx_t* PrecacheSound(hrtf_state_t* state, const char* name);
void freeSFXCache(hrtf_state_t* state);
int CleanupNaNs(IPLAudioBuffer* buf);
// }}} util

// {{{ ipl
qboolean AllocEffects(hrtf_state_t* state, hrtf_channel_t* chan);
void ApplyEffects(hrtf_state_t* state, IPLAudioBuffer* buf, hrtf_channel_t* chan);
void FreeEffects(hrtf_state_t* state, hrtf_channel_t* chan);
float ComputeDistanceAttenuation(const vec3_t listenerOrigin, const vec3_t sourceOrigin, const float attenuation);
// }}} ipl

// {{{ mixer
hrtf_channel_t* PickChannel(hrtf_channel_t mixer[CHANNELS_MAX], int entnum, int entchannel);
void FreeChannel(hrtf_state_t* state, hrtf_channel_t* chan);
void MixAndSend(hrtf_state_t* state);
void MixAudio(hrtf_state_t* state, IPLAudioBuffer* mix);
hrtf_channel_t* StartSound(hrtf_state_t* state, int entnum, int entchannel, sfx_t* sfx, vec3_t origin, float volume, float attenuation, qboolean loop);
// }}} mixer

// {{{ bgm
qboolean bgm_init(hrtf_state_t* state);
void bgm_shutdown();

qboolean HRTF_BGMInit();
void HRTF_BGMShutdown();
void HRTF_BGMUpdate();
// }}}
