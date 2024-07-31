/*
Copyright (C) 2024 Léo Peltier

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
#include <phonon.h>
#include <assert.h>

#include "../../quakedef.h"
#include "../iface.h"
#include "hrtf.h"

#include "../legacy/bgmusic.h"
#include "../legacy/codec.h"

static hrtf_state_t hrtf_state = {0};
static hrtf_channel_t* ambients[NUM_AMBIENTS] = {0};

static void HRTF_CB_sfxvolume(cvar_t* var) {
	assert(hrtf_state.initialized);
	hrtf_state.volume = TO_PERCEPTIBLE_VOLUME(var->value);
}

static qboolean ipl_init(int samplingFreq) {
	Con_Printf("HRTF: Initializing HRTF.\n");

	IPLContextSettings ctxSettings = {
		.version = STEAMAUDIO_VERSION,
// The developer cvar is not set at this point, cannot use it.
#ifndef NDEBUG
		.flags = IPL_CONTEXTFLAGS_VALIDATION,
#endif
	};
	IPLHRTFSettings hrtfSettings = {
		.type = IPL_HRTFTYPE_DEFAULT,
		.volume = 1.f,
	};
	IPLAudioSettings audioSettings = {
		.samplingRate = samplingFreq,
		.frameSize = FRAME_SIZE,
	};
	hrtf_state.iplAudioSettings = audioSettings;

	IPLerror err = iplContextCreate(&ctxSettings, &hrtf_state.iplContext);
	if (err != IPL_STATUS_SUCCESS) {
		Con_Printf("HRTF: Unable to init IPLContext: err #%d\n", err);
		goto error;
	}

	err = iplHRTFCreate(
		hrtf_state.iplContext,
		&audioSettings,
		&hrtfSettings,
		&hrtf_state.iplHRTF
	);
	if (err != IPL_STATUS_SUCCESS) {
		Con_Warning("HRTF: unable to create HRTF: err #%d\n", err);
		goto error;
	}

	Cvar_SetCallback(&sfxvolume, HRTF_CB_sfxvolume);

	return true;

error:
	if (hrtf_state.iplHRTF != NULL) {
		iplHRTFRelease(&hrtf_state.iplHRTF);
	}

	if (hrtf_state.iplContext != NULL) {
		iplContextRelease(&hrtf_state.iplContext);
	}

	return false;
}

static void ipl_shutdown() {
	Con_Printf("HRTF: Shutting down HRTF.\n");

	iplHRTFRelease(&hrtf_state.iplHRTF);
	iplContextRelease(&hrtf_state.iplContext);
}

// DO NOT use Con_* here. It WILL crash, we're not in the same thread.
static void sdl_audio_callback(void* userdata, Uint8* stream, int len) {
	hrtf_state_t* state = (hrtf_state_t*) userdata;
	memset(stream, 0, len);

	if (!state->frameBufferFull) {
		state->starved++;
		return;
	}

	assert(state->frameBufferSize == (size_t) len);
	memcpy(stream, state->frameBuffer, state->frameBufferSize);
	state->frameBufferFull = false;
}

static qboolean sdl_init(const int samplingFreq) {
	Con_Printf("HRTF: Initializing SDL_Audio.\n");
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		return false;
	}

	SDL_AudioSpec desired = {
		.freq = samplingFreq,
		.channels = NUM_OUTPUT_CHANNELS,
		.format = AUDIO_F32SYS,
		.samples = FRAME_SIZE,
		.callback = sdl_audio_callback,
		.userdata = &hrtf_state,
	};

	hrtf_state.deviceID = SDL_OpenAudioDevice(
		NULL, // auto-select device
		0, // 0 = output device
		&desired,
		&hrtf_state.audioSpec,
		0 // allowed changes
	);
	if (hrtf_state.deviceID != 2) {
		// "SDL_OpenAudioDevice() calls always returns devices = 2 on success."
		Con_Printf("HRTF: SDL_OpenAudioDevice returned %d.\n", hrtf_state.deviceID);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	SDL_LockAudioDevice(hrtf_state.deviceID);
	assert(hrtf_state.audioSpec.channels == 2);
	hrtf_state.frameBufferSize = sizeof(IPLfloat32) * FRAME_SIZE * hrtf_state.audioSpec.channels;
	hrtf_state.frameBuffer = malloc(hrtf_state.frameBufferSize);
	if (hrtf_state.frameBuffer == NULL) {
		SDL_UnlockAudioDevice(hrtf_state.deviceID);
		Sys_Error("HRTF: unable to allocate frame buffer.\n");
		return false;
	}

	memset(hrtf_state.frameBuffer, 0, hrtf_state.frameBufferSize);
	SDL_UnlockAudioDevice(hrtf_state.deviceID);

	SDL_PauseAudioDevice(hrtf_state.deviceID, 0);

	return true;
}

static void sdl_shutdown() {
	Con_Printf("HRTF: Shutting down SDL_Audio.\n");
	SDL_CloseAudioDevice(hrtf_state.deviceID);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

static sfx_t* ambient_sfx[NUM_AMBIENTS] = {0};

static void HRTF_StopSound(int entnum, int entchannel);
static void InitAmbients(hrtf_state_t* state) {
	ambient_sfx[AMBIENT_WATER] = PrecacheSound(&hrtf_state, "ambience/water1.wav");
	ambient_sfx[AMBIENT_SKY] = PrecacheSound(&hrtf_state, "ambience/wind2.wav");
	HRTF_StopSound(ENTNUM_STATIC, ENTCHANNEL_PLAY_GLOBALLY);

	for (size_t i = 0; i < NUM_AMBIENTS; i++) {
		if (ambient_sfx[i] == NULL) {
			continue;
		}

		ambients[i] = StartSound(
			state, ENTNUM_STATIC, ENTCHANNEL_PLAY_GLOBALLY,
			ambient_sfx[i], vec3_origin, 0.f, 1.f, true
		);
	}
}

static void SilenceAmbients(hrtf_state_t* state) {
	for (size_t i = 0; i < NUM_AMBIENTS; i++) {
		if (ambients[i] == NULL) {
			continue;
		}

		ambients[i]->volume = 0.f;
	}

	state->underwater = 0.f;
}

static qboolean ContentsIsUnderwater(const int contents) {
	switch (contents) {
		case CONTENTS_WATER:
		case CONTENTS_SLIME:
		case CONTENTS_LAVA:
			return true;
	}

	return false;
}

static void UpdateUnderwater(hrtf_state_t* state, const mleaf_t* leaf) {
	const float target = (cl.forceunderwater || ContentsIsUnderwater(leaf->contents))
		? snd_waterfx.value
		: 0.f
	;
	if (fabs(target-state->underwater) < .001) {
		state->underwater = target;
		return;
	}

	const float sign = state->underwater > target ? -1.f : 1.f;
	state->underwater = CLAMP(0.f, state->underwater + sign * host_frametime, snd_waterfx.value);
}

static void UpdateAmbients(hrtf_state_t* state) {
	if (cls.state != ca_connected || !cl.worldmodel) {
		SilenceAmbients(state);
		return;
	}

	const mleaf_t* leaf = Mod_PointInLeaf(state->listenerOrigin, cl.worldmodel);
	if (leaf == NULL) {
		SilenceAmbients(state);
		return;
	}

	UpdateUnderwater(state, leaf);

	for (size_t i = 0; i < NUM_AMBIENTS; i++) {
		if (ambients[i] == NULL) {
			continue;
		}

		const float target = ambient_level.value * (leaf->ambient_sound_level[i] / 255.f);
		if (fabs(target-ambients[i]->volume) < .001) {
			ambients[i]->volume = target;
			continue;
		}

		const float sign = ambients[i]->volume > target ? -1.f : 1.f;
		ambients[i]->volume = CLAMP(0.f, ambients[i]->volume + sign * host_frametime, 1.f);
	}
}

// {{{ snd_iface_t implementation
static void HRTF_Init(void) {
	Con_Printf("HRTF: Initializing snd_hrtf.\n");
	assert(!hrtf_state.initialized);

	memset(&hrtf_state, 0, sizeof(hrtf_state_t));
	const int samplingFreq = snd_mixspeed.value;
	hrtf_state.sfxCache = (sfx_t*) Hunk_AllocName(SFX_CACHE_MAX * sizeof(sfx_t), "hrtf_sfx_t");
	hrtf_state.volume = sfxvolume.value;

	if (!sdl_init(samplingFreq)) {
		Sys_Error("HRTF: Unable to initialize SDL: %s\n", SDL_GetError());
		return;
	}

	if (!ipl_init(samplingFreq)) {
		Sys_Error("HRTF: Unable to initialize Steam Audio.\n");
		return;
	}

	if (!bgm_init(&hrtf_state)) {
		Sys_Error("HRTF: Unable to initialize BGM.\n");
		return;
	}

	S_CodecInit();
	hrtf_state.initialized = true;
	Con_Printf("HRTF: Initialized, frame size: %d samples, rate: %d.\n", FRAME_SIZE, samplingFreq);
}

static void HRTF_BeginPrecaching(void) {
	InitAmbients(&hrtf_state);
}

static sfx_t* HRTF_PrecacheSound(const char* name) {
	assert(hrtf_state.initialized);
	return PrecacheSound(&hrtf_state, name);
}

static void HRTF_ClearBuffer(void) {
	Con_DPrintf2("HRTF: S_ClearBuffer()\n");
	assert(hrtf_state.initialized);

	SDL_LockAudioDevice(hrtf_state.deviceID);
	hrtf_state.frameBufferFull = false;
	memset(hrtf_state.frameBuffer, 0, hrtf_state.frameBufferSize);
	SDL_UnlockAudioDevice(hrtf_state.deviceID);
}

static void HRTF_BlockSound(void) {
	Con_DPrintf("HRTF: S_BlockSound()\n");
	assert(hrtf_state.initialized);

	SDL_PauseAudioDevice(hrtf_state.deviceID, 1);
}

static void HRTF_ClearPrecache(void) {
	Con_DPrintf("HRTF: S_ClearPrecache()\n");
	assert(hrtf_state.initialized);
	freeSFXCache(&hrtf_state);
}

static void HRTF_EndPrecaching(void) {
	assert(hrtf_state.initialized);

	Con_Printf(
		"HRTF: Precached %lu kB of audio data for %lu sounds.\n",
		hrtf_state.precachedAudioBuffersSize / 1024,
		hrtf_state.nextSFXCacheEntry
	);
}

static void HRTF_StopAllSounds(qboolean _);
static void HRTF_Shutdown(void) {
	Con_Printf("HRTF: S_Shutdown()\n");
	if (!hrtf_state.initialized) {
		Con_Warning("HRTF: shutdown but not initialized!\n");
		return;
	}

	SDL_PauseAudioDevice(hrtf_state.deviceID, 1);
	HRTF_StopAllSounds(true);
	freeSFXCache(&hrtf_state);

	bgm_shutdown();
	sdl_shutdown();
	ipl_shutdown();

	hrtf_state.initialized = false;
}

static void HRTF_StartSound(
	int entnum,
	int entchannel,
	sfx_t* sfx,
	vec3_t origin,
	float volume,
	float attenuation
) {
	StartSound(&hrtf_state, entnum, entchannel, sfx, origin, volume, attenuation, false);
}

static void HRTF_Startup(void) {
	Con_DPrintf("HRTF: S_Startup() not implemented\n");
	assert(hrtf_state.initialized);
}

static void HRTF_StaticSound(sfx_t* sfx, vec3_t origin, float volume, float attenuation) {
	volume /= 255.f;
	attenuation /= 64.f;

	StartSound(
		&hrtf_state, ENTNUM_STATIC, ENTCHANNEL_AUTO,
		sfx, origin, volume, attenuation, true
	);
}

static void HRTF_StopAllSounds(qboolean clear) {
	Con_DPrintf("HRTF: S_StopAllSounds\n");
	assert(hrtf_state.initialized);
	HRTF_ClearBuffer();
	for (size_t i = 0; i < CHANNELS_MAX; i++) {
		FreeChannel(&hrtf_state, &hrtf_state.mixer[i]);
	}
}

static void HRTF_StopSound(int entnum, int entchannel) {
	assert(hrtf_state.initialized);
	size_t stopped = 0;
	for (size_t i = 0; i < CHANNELS_MAX; i++) {
		if (hrtf_state.mixer[i].sfx == NULL) {
			continue;
		}

		if (hrtf_state.mixer[i].entnum == entnum && hrtf_state.mixer[i].entchannel == entchannel) {
			FreeChannel(&hrtf_state, &hrtf_state.mixer[i]);
		}
	}
	Con_DPrintf("HRTF: S_StopSound(%d, %d): stopped %lu sounds.\n", entnum, entchannel, stopped);
}

static void HRTF_TouchSound(const char* name) {
	assert(hrtf_state.initialized);
	PrecacheSound(&hrtf_state, name);
}

static void HRTF_UnblockSound(void) {
	Con_DPrintf("HRTF: S_UnblockSound()\n");
	assert(hrtf_state.initialized);
	SDL_PauseAudioDevice(hrtf_state.deviceID, 0);
}

static void HRTF_Update(vec3_t origin, vec3_t forward, vec3_t right, vec3_t up) {
	assert(hrtf_state.initialized);

	VectorCopy(origin, hrtf_state.listenerOrigin);
	VectorCopy(forward, hrtf_state.listenerForward);
	VectorCopy(right, hrtf_state.listenerRight);
	VectorCopy(up, hrtf_state.listenerUp);

	UpdateAmbients(&hrtf_state);
	MixAndSend(&hrtf_state);
}

static void HRTF_ExtraUpdate(void) {
	assert(hrtf_state.initialized);

	MixAndSend(&hrtf_state);
}

static void HRTF_LocalSound(const char* name) {
	Con_DPrintf2("HRTF: S_LocalSound(%s)\n", name);

	sfx_t* sfx = FindOrAllocateCacheEntry(&hrtf_state, name);
	if (sfx == NULL) {
		Con_Warning("HRTF: unable to obtain cache entry for: %s\n", name);
		return;
	}

	StartSound(
		&hrtf_state, cl.viewentity, ENTCHANNEL_AUTO,
		sfx, vec3_origin, 1, 1, false
	);
	assert(hrtf_state.initialized);
}

snd_iface_t snd_new_hrtf_impl(void) {
	const snd_iface_t ret = {
		.BeginPrecaching = HRTF_BeginPrecaching,
		.BlockSound      = HRTF_BlockSound,
		.ClearBuffer     = HRTF_ClearBuffer,
		.ClearPrecache   = HRTF_ClearPrecache,
		.EndPrecaching   = HRTF_EndPrecaching,
		.ExtraUpdate     = HRTF_ExtraUpdate,
		.Init            = HRTF_Init,
		.LocalSound      = HRTF_LocalSound,
		.PrecacheSound   = HRTF_PrecacheSound,
		.Shutdown        = HRTF_Shutdown,
		.StartSound      = HRTF_StartSound,
		.Startup         = HRTF_Startup,
		.StaticSound     = HRTF_StaticSound,
		.StopAllSounds   = HRTF_StopAllSounds,
		.StopSound       = HRTF_StopSound,
		.TouchSound      = HRTF_TouchSound,
		.UnblockSound    = HRTF_UnblockSound,
		.Update          = HRTF_Update,

		// Don't wanna reimplement all that poorly abstracted codec and FS
		// stuff, reuse most of the legacy implementation.
		.BGMInit        = snd_dma_BGM_Init,
		.BGMPause       = snd_dma_BGM_Pause,
		.BGMPlay        = snd_dma_BGM_Play,
		.BGMPlayCDtrack = snd_dma_BGM_PlayCDtrack,
		.BGMResume      = snd_dma_BGM_Resume,
		.BGMShutdown    = snd_dma_BGM_Shutdown,
		.BGMStop        = snd_dma_BGM_Stop,

		.BGMUpdate   = HRTF_BGMUpdate,
	};
	return ret;
}
// }}} snd_iface_t implementation
