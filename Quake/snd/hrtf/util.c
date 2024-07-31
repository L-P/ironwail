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

#include "hrtf.h"

const float MIX_HEADROOM = -6.f; // dB

void iplAudioBufferZero(IPLAudioBuffer* dst) {
	for (int i = 0; i < dst->numChannels; i++) {
		memset(dst->data[i], 0, sizeof(IPLfloat32) * dst->numSamples);
	}
}

void iplAudioBufferCopy(IPLAudioBuffer* dst, const IPLAudioBuffer* src) {
	assert(dst->numSamples == src->numSamples);
	assert(dst->numChannels == src->numChannels);

	for (int i = 0; i < dst->numChannels; i++) {
		memcpy(dst->data[i], src->data[i], sizeof(IPLfloat32) * dst->numSamples);
	}
}

// Loads a WAV into a mono 32bit float IPLAudioBuffer ready to be spacialized.
// Returns true on success, the dst buffer will be allocated and the caller is
// responsible for freeing it.
// Returns false on failure and cleans up after itself.
qboolean LoadWAV(hrtf_state_t* state, const char* name, IPLAudioBuffer* dst, int* loopStart) {
	// 1. Buffer entire file in memory.
	char path[MAX_QPATH] = {0};
	q_strlcpy(path, "sound/", sizeof path);
	q_strlcat(path, name, sizeof path);
	byte* wavFile = COM_LoadMallocFile(path, NULL);
	if (wavFile == NULL) {
		Con_Warning("HRTF: could not COM_LoadMallocFile: %s\n", path);
		goto error;
	}

	// 2. Parse WAV.
	SDL_RWops* reader = SDL_RWFromConstMem(wavFile, com_filesize);
	SDL_AudioSpec spec = {0};
	uint8_t* buf = NULL;
	uint32_t len = 0;
	if (SDL_LoadWAV_RW(reader, 1, &spec, &buf, &len) == NULL) {
		Con_Warning("HRTF: unable to load WAV %s: %s\n", path, SDL_GetError());
		goto error;
	}

	const wavinfo_t info = GetWavinfo(path, wavFile, com_filesize);
	const float scale = (float) spec.freq / (float) state->audioSpec.freq;
	*loopStart = info.loopstart < 0 ? -1 : info.loopstart * scale;

	// 3. Resample/convert.
	if (!WAVToIPLAudioBuffer(state, spec, buf, len, dst)) {
		Con_Warning("HRTF: unable to resample WAV to IPLAudioBuffer: %s\n", path);
		goto error;
	}

	// 4. Reduce gain for later mixing.
	const float gain = powf(10.f, MIX_HEADROOM * 0.05);
	for (int i = 0; i < dst->numChannels; i++) {
		for (int j = 0; j < dst->numSamples; j++) {
			assert(!isnan(dst->data[i][j]));
			dst->data[i][j] *= gain;
		}
	}

	free(wavFile);

	return true;

error:
	iplAudioBufferFree(state->iplContext, dst);
	free(wavFile);

	return false;
}

qboolean WAVToIPLAudioBuffer(hrtf_state_t* state, SDL_AudioSpec spec, byte* src, size_t srcLen, IPLAudioBuffer* dst) {
	memset(dst, 0, sizeof(IPLAudioBuffer));

	SDL_AudioStream* stream = SDL_NewAudioStream(
		spec.format, spec.channels, spec.freq,
		AUDIO_F32SYS, 1, state->audioSpec.freq
	);
	if (SDL_AudioStreamPut(stream, src, srcLen) == -1) {
		Con_Warning("HRTF: unable to write WAV samples to stream: %s\n", SDL_GetError());
		goto error;
	}
    SDL_AudioStreamFlush(stream);

	const size_t dstBufLen = SDL_AudioStreamAvailable(stream);
	const size_t numDstSamples = dstBufLen / SDL_AUDIO_SAMPLESIZE(state->audioSpec.format);
	const IPLerror err = iplAudioBufferAllocate(state->iplContext, 1, numDstSamples, dst);
	if (err != IPL_STATUS_SUCCESS) {
		Con_Warning("HRTF: unable to allocate output buffer, got error code: #%d\n", err);
		goto error;
	}
	iplAudioBufferZero(dst);

	size_t totalRead = 0;
	do {
		const int bytesRead = SDL_AudioStreamGet(stream, dst->data[0]+totalRead, dstBufLen-totalRead);
		if (bytesRead == -1) {
			Con_Warning("HRTF: unable to read resampled WAV: %s\n", SDL_GetError());
			goto error;
		}
		if (bytesRead == 0) {
			break;
		}
		totalRead += bytesRead;
	} while(totalRead < dstBufLen);
	if (totalRead < dstBufLen) {
		Con_Warning("HRTF: short read: %lu/%lu (missing %d bytes)\n", totalRead, dstBufLen, (int) dstBufLen - (int) totalRead);
		goto error;
	}

	SDL_FreeAudioStream(stream);
	return true;

error:
	iplAudioBufferFree(state->iplContext, dst);
	SDL_FreeAudioStream(stream);

	return false;
}

sfx_t* FindOrAllocateCacheEntry(hrtf_state_t* state, const char* name) {
	if (strnlen(name, MAX_QPATH) >= MAX_QPATH) {
		Sys_Error("HRTF: name too long to cache sound for: %s\n", name);
		return NULL;
	}

	for (size_t i = 0; i < state->nextSFXCacheEntry; i++) {
		if (strncmp(name, state->sfxCache[i].name, MAX_QPATH) == 0) {
			return &(state->sfxCache[i]);
		}
	}

	if (state->nextSFXCacheEntry >= SFX_CACHE_MAX) {
		Sys_Error("HRTF: exhausted SFX cache\n");
		return NULL;
	}

	sfx_t* ret = &(state->sfxCache[state->nextSFXCacheEntry]);
	state->nextSFXCacheEntry++;
	q_strlcpy(ret->name, name, sizeof ret->name);

	return ret;
}

// Returns the declared size of an allocated buffer.
size_t iplAudioBufferDataSize(const IPLAudioBuffer* buf) {
	return sizeof(IPLfloat32) * buf->numChannels * buf->numSamples;
}

void freeSFXCache(hrtf_state_t* state) {
	for (size_t i = 0; i < state->nextSFXCacheEntry; i++) {
		hrtf_sfx_cache_entry_t* entry = state->sfxCache[i].cache.data;
		iplAudioBufferFree(state->iplContext, &entry->buf);
		free(entry);
	}
	state->nextSFXCacheEntry = 0;
	memset(state->sfxCache, 0, SFX_CACHE_MAX * sizeof(sfx_t));
	state->precachedAudioBuffersSize = 0;
}

sfx_t* PrecacheSound(hrtf_state_t* state, const char* name) {
	sfx_t* sfx = FindOrAllocateCacheEntry(state, name);
	if (sfx == NULL) {
		Con_Warning("HRTF: unable to obtain cache entry for: %s\n", name);
		return NULL;
	}
	if (sfx->cache.data != NULL) {
		return sfx;
	}

	// Don't bother putting things in Cache_*, worry about memory later.
	// For now I think we can handle the entire 2.8 MiB worth of sound of id1.
	// (That's ~25 MiB after resampling.)
	hrtf_sfx_cache_entry_t* entry = malloc(sizeof(hrtf_sfx_cache_entry_t));
	memset(entry, 0, sizeof(hrtf_sfx_cache_entry_t));

	if (!LoadWAV(state, name, &entry->buf, &entry->loopStart)) {
		Con_Warning("HRTF: unable to precache sound: %s\n", name);
		return NULL;
	}

	sfx->cache.data = entry;
	state->precachedAudioBuffersSize += iplAudioBufferDataSize(&entry->buf);

	return sfx;
}

int CleanupNaNs(IPLAudioBuffer* buf) {
	int nans = 0;
	for (int i = 0; i < buf->numChannels; i++) {
		for (int j = 0; j < buf->numSamples; j++) {
			if (isnan(buf->data[i][j])) {
				nans++;
				buf->data[i][j] = 0;
			}
		}
	}
	return nans;
}

qboolean AllocScratchBuffer(IPLContext ctx, int channels, int samples, IPLAudioBuffer* dst) {
	assert(dst != NULL);

	if (dst->data != NULL
		&& dst->numChannels == channels
		&& dst->numSamples == samples
	) {
		return false;
	}

	if (dst->data != NULL) {
		Con_Warning(
			"Re-allocating scratch %p: %dx%d -> %dx%d.\n",
			(void*) dst,
			dst->numChannels, dst->numSamples,
			channels, samples
		);
		iplAudioBufferFree(ctx, dst);
		memset(dst, 0, sizeof(IPLAudioBuffer));
	}

	const IPLerror err = iplAudioBufferAllocate(ctx, channels, samples, dst);
	if (err != IPL_STATUS_SUCCESS) {
		Sys_Error("HRTF: unable to allocate scratch buffer: %d", err);
	}

	return true;
}
