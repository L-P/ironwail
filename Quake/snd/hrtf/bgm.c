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
#include "../legacy/codec.h"
#include "../legacy/bgmusic.h"
#include "hrtf.h"

extern snd_stream_t* bgmstream;

typedef struct {
	IPLContext iplContext;
	SDL_AudioDeviceID deviceID;
	size_t frameSize;

	IPLAudioBuffer* outBuf;
	SDL_AudioSpec outSpec;
	qboolean* outBufFull;
	size_t outBufFrameSize;

	SDL_AudioStream* inStream;
	SDL_AudioSpec inSpec;
	size_t inFrameSize;

	qboolean initialized;

	// Buffer for SendNextConvertedFrame.
	IPLfloat32* interleaved;
	size_t interleavedSize;
} bgm_state_t;

static bgm_state_t bgm_state = {0};

qboolean bgm_init(hrtf_state_t* state) {
	assert(!bgm_state.initialized);
	memset(&bgm_state, 0, sizeof(bgm_state));
	bgm_state.iplContext = state->iplContext;
	bgm_state.outBuf = &state->bgmFrame;
	bgm_state.outBufFull = &state->bgmFrameFull;
	bgm_state.outSpec = state->audioSpec;
	bgm_state.deviceID = state->deviceID;

	const IPLerror err = iplAudioBufferAllocate(
		state->iplContext, state->audioSpec.channels, FRAME_SIZE, bgm_state.outBuf
	);
	if (err != IPL_STATUS_SUCCESS) {
		Sys_Error("HRTF BGM: unable to init bgm buffer\n");
		return false;
	}

	bgm_state.interleavedSize = sizeof(IPLfloat32) * bgm_state.outBuf->numChannels * bgm_state.outBuf->numSamples;
	bgm_state.interleaved = malloc(bgm_state.interleavedSize);
	if (bgm_state.interleaved == NULL) {
		Sys_Error("HRTF BGM: Unable to allocate interleaved output buffer.\n");
		return false;
	}

	bgm_state.initialized = true;
	return true;
}

void bgm_shutdown() {
	assert(bgm_state.initialized);
	free(bgm_state.interleaved);
	iplAudioBufferFree(bgm_state.iplContext, bgm_state.outBuf);
	memset(&bgm_state, 0, sizeof(bgm_state));
}

// cf. S_RawSamples comment.
static SDL_AudioFormat formatFromBGMStreamWidth(const int width) {
	switch(width) {
		case 1:
			return AUDIO_U8;
		case 2:
			return AUDIO_S16SYS;
	}

	Sys_Error("HRTF BGM: unable to obtain desired output format for bgm stream width: %d\n", width);
	assert(false);
}

static SDL_AudioStream* CreateConversionStream(bgm_state_t* state, const SDL_AudioSpec inSpec) {
	SDL_AudioStream* outStream = SDL_NewAudioStream(
		inSpec.format, inSpec.channels, inSpec.freq,
		state->outSpec.format, state->outSpec.channels, state->outSpec.freq
	);

	if (outStream == NULL) {
		Sys_Error("HRTF BGM: unable to create conversion stream: %s\n", SDL_GetError());
		return NULL;
	}

	return outStream;
}

static qboolean AudioSpecEquals(const SDL_AudioSpec a, const SDL_AudioSpec b) {
	return a.freq == b.freq
		&& a.channels == b.channels
		&& a.format == b.format
		&& a.samples == b.samples
	;
}

// Returns false on error only. Stream might still be null.
static qboolean UpdateConversionStream(bgm_state_t* state, snd_stream_t* inStream) {
	if (inStream == NULL) {
		if (state->inStream != NULL) {
			Con_DPrintf2("HRTF BGM: Stream ended.\n");
			SDL_FreeAudioStream(state->inStream);
			state->inStream = NULL;
		}
		return true;
	}

	const SDL_AudioSpec inSpec = {
		.freq = inStream->info.rate,
		.channels = inStream->info.channels,
		.format = formatFromBGMStreamWidth(inStream->info.width),
		.samples = FRAME_SIZE,
	};

	if (!AudioSpecEquals(state->inSpec, inSpec)) {
		Con_DPrintf2("HRTF BGM: Audio spec changed, resetting stream.\n");
		SDL_FreeAudioStream(state->inStream);
		state->inStream = NULL;
	}

	if (state->inStream == NULL) {
		Con_DPrintf2(
			"HRTF BGM: Creating conversion stream from input (%d channels, %d Hz, %d bytes per sample)\n",
			inSpec.channels, inSpec.freq, SDL_AUDIO_SAMPLESIZE(inSpec.format)
		);
		state->inStream = CreateConversionStream(state, inSpec);
		state->inSpec = inSpec;
		state->inFrameSize = inStream->info.width * FRAME_SIZE * inStream->info.channels;
		return state->inStream != NULL;
	}

	return true;
}

// Feeds the next decoded frame into our resampling stream.
static void FeedNextRawFrame(bgm_state_t* state, snd_stream_t* in , SDL_AudioStream* out) {
	assert(state->inFrameSize > 0);
	byte* buf = malloc(state->inFrameSize);
	if (buf == NULL) {
		Sys_Error("HRTF BGM: unable to allocate decoded audio buffer.\n");
		return;
	}
	memset(buf, 0, state->inFrameSize);

	qboolean looped = false;
	do {
		const size_t read = S_CodecReadStream(in, state->inFrameSize, buf);
		if (read == 0) {
			if (looped) {
				Sys_Error("HRTF BGM: looped twice in a row.\n");
				return;
			}

			looped = true;
			const int err = S_CodecRewindStream(bgmstream);
			if (err != 0) {
				Sys_Error("HRTF BGM: unable to rewind stream.\n");
				return;
			}
			continue;
		}

		if (SDL_AudioStreamPut(state->inStream, buf, read) == -1) {
			Sys_Error("HRTF BGM: unable to convert stream.\n");
			return;
		}
	} while ((size_t) SDL_AudioStreamAvailable(state->inStream) < state->inFrameSize);

	free(buf);
}

// Writes up to $len bytes of decoded data and the rest as zeroes.
qboolean GetNextConvertedFrame(
	bgm_state_t* state,
	SDL_AudioStream* stream,
	IPLfloat32* buf, size_t len
) {
	memset(bgm_state.interleaved, 0, bgm_state.interleavedSize);
	// Short reads are expected if we don't loop (which is TODO).
	size_t totalRead = 0;
	do {
		const int bytesRead = SDL_AudioStreamGet(
			bgm_state.inStream,
			bgm_state.interleaved+totalRead,
			bgm_state.interleavedSize-totalRead
		);
		if (bytesRead == -1) {
			Con_Warning("HRTF BGM: unable to read resampled BGM: %s\n", SDL_GetError());
			return false;
		}
		if (bytesRead == 0) {
			break;
		}
		totalRead += bytesRead;
	} while(totalRead < bgm_state.interleavedSize);

	return true;
}

void HRTF_BGMUpdate() {
	assert(bgm_state.initialized);

	if (!UpdateConversionStream(&bgm_state, bgmstream)) {
		Con_Warning("HRTF BGM: Unable to maintain audio stream.\n");
		return;
	}

	if (bgmstream == NULL || bgmstream->status != STREAM_PLAY || bgmvolume.value <= 0.f) {
		return;
	}

	SDL_LockAudioDevice(bgm_state.deviceID);
	if (*bgm_state.outBufFull) {
		SDL_UnlockAudioDevice(bgm_state.deviceID);
		return;
	}
	SDL_UnlockAudioDevice(bgm_state.deviceID);

	if ((size_t) SDL_AudioStreamAvailable(bgm_state.inStream) < bgm_state.inFrameSize) {
		FeedNextRawFrame(&bgm_state, bgmstream, bgm_state.inStream);
	}

	if (!GetNextConvertedFrame(
		&bgm_state, bgm_state.inStream,
		bgm_state.interleaved, bgm_state.interleavedSize
	)) {
		return;
	}

	SDL_LockAudioDevice(bgm_state.deviceID);
	iplAudioBufferDeinterleave(bgm_state.iplContext, bgm_state.interleaved, bgm_state.outBuf);
	*bgm_state.outBufFull = true;
	SDL_UnlockAudioDevice(bgm_state.deviceID);
}
