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

hrtf_channel_t* StartSound(
		hrtf_state_t* state,
		int entnum,
		int entchannel,
		sfx_t* sfx,
		vec3_t origin,
		float volume,
		float attenuation,
		qboolean randomizeStart
) {
	assert(volume >= 0.f);
	assert(attenuation >= 0.f);
	if (sfx->cache.data == NULL) {
		sfx = PrecacheSound(state, sfx->name);
	}

	Con_DPrintf2(
		"HRTF: StartSound(entnum %d, entchannel %d, sfx %s, origin(% 8.2f, % 8.2f, % 8.2f), volume %.2f, attenuation %.2f, randomizeStart: %d) = ",
		entnum, entchannel, sfx->name, origin[0], origin[1], origin[2], volume, attenuation, randomizeStart
	);

	hrtf_channel_t* chan = PickChannel(state->mixer, entnum, entchannel);
	hrtf_channel_t oldChan = {0};
	memcpy(&oldChan, chan, sizeof(hrtf_channel_t));
	if (chan == NULL) {
		Con_Warning("\nHRTF: unable to find a free channel for sound: %s (entnum: %d, entchannel: %d)\n", sfx->name, entnum, entchannel);
		return NULL;
	}

	// In use, reset.
	if (chan->sfx != NULL) {
		Con_DPrintf2("reusing ");
		FreeChannel(state, chan);
	}

	if (!AllocEffects(state, chan)) {
		Con_Warning("\nHRTF: unable to alloc effects for channel #%lu\n", chan-state->mixer);
		return NULL;
	}

	chan->entnum = entnum;
	chan->entchannel = entchannel;
	// No TO_PERCEPTIBLE_VOLUME here to keep vanilla behavior, meaning all
	// hardcoded and level-defined volumes use the non-intuitive scaling.
	chan->volume = volume;
	chan->attenuation = attenuation;
	chan->sfx = sfx;
	VectorCopy(origin, chan->origin);

	const hrtf_sfx_cache_entry_t* entry = (hrtf_sfx_cache_entry_t*) sfx->cache.data;
	chan->spent = randomizeStart ? rand() % entry->buf.numSamples : 0;
	chan->loopStartSample = entry->loopStart;

	// If this is a looping sound and we're not rewinding, keep track of the
	// previous sound playing on the same ent/channel so that when we do
	// rewind past this frame we start playing it instead.
	if (
		oldChan.entnum > 0 && oldChan.entchannel > 0
		&& oldChan.entnum == entnum && oldChan.entchannel == entchannel
		&& cls.demoplayback && cls.demospeed > 0.f && entry->loopStart > 0
	) {
		CL_AddDemoRewindSound(
			entnum, entchannel,
			oldChan.sfx, oldChan.origin, oldChan.volume, oldChan.attenuation
		);
	}

	Con_DPrintf2("channel #%lu\n", chan - state->mixer);
	return chan;
}

static void ApplyGlobalVolume(hrtf_state_t* state, IPLAudioBuffer* buf) {
	const float fvol = CLAMP(0.f, state->volume, 1.f);
	size_t clips = 0;

	for (int i = 0; i < buf->numChannels; i++) {
		for (int j = 0; j < buf->numSamples; j++) {
			buf->data[i][j] *= fvol;

			if (buf->data[i][j] < -1.f || buf->data[i][j] > 1.f) {
				buf->data[i][j] = CLAMP(-1.f, buf->data[i][j], 1.f);
				clips++;
			}
		}
	}

	if (clips > 0) {
		Con_DPrintf2("HRTF: %lu samples clipped!\n", clips);
	}
}

static void ApplyGain(IPLAudioBuffer* buf, const float gain) {
	assert(!isnan(gain));
	for (int i = 0; i < buf->numChannels; i++) {
		for (int j = 0; j < buf->numSamples; j++) {
			buf->data[i][j] *= gain;
		}
	}
}

void BGM_Update();
static void ApplyUnderwaterEffect(const float intensity, const IPLAudioBuffer* in, IPLAudioBuffer* out);
static IPLAudioBuffer mix = {0};
void MixAndSend(hrtf_state_t* state) {
	SDL_LockAudioDevice(state->deviceID);
	if (state->frameBufferFull) {
		SDL_UnlockAudioDevice(state->deviceID);
		return;
	}
	SDL_UnlockAudioDevice(state->deviceID);

	AllocScratchBuffer(state->iplContext, state->audioSpec.channels, FRAME_SIZE, &mix);
	iplAudioBufferZero(&mix);

	MixAudio(state, &mix);
	ApplyUnderwaterEffect(state->underwater, &mix, &mix);

	ApplyGlobalVolume(state, &mix);

	BGM_Update();
	SDL_LockAudioDevice(state->deviceID);
	if (state->bgmFrameFull) {
		ApplyGain(&state->bgmFrame, TO_PERCEPTIBLE_VOLUME(bgmvolume.value));
		iplAudioBufferMix(state->iplContext, &state->bgmFrame, &mix);
		state->bgmFrameFull = false;
	}

	iplAudioBufferInterleave(state->iplContext, &mix, state->frameBuffer);
	state->frameBufferFull = true;
	if (state->starved > 0) {
		Con_DPrintf2("HRTF: starved for %d samples!\n", state->starved * FRAME_SIZE);
		state->starved = 0;
	}
	SDL_UnlockAudioDevice(state->deviceID);
}

// Copies the next frame of a channel's sfx source buffer to dst.
static void CopyNextChannelFrame(hrtf_state_t* state, hrtf_channel_t* chan, const IPLAudioBuffer* src, IPLAudioBuffer* dst) {
	iplAudioBufferZero(dst);

	for (int j = 0; j < dst->numSamples; j++) {
		for (int i = 0; i < dst->numChannels; i++) {
			dst->data[i][j] = src->data[0][chan->spent];
			assert(!isnan(dst->data[i][j]));
		}

		chan->spent++;
		if (chan->spent >= src->numSamples) {
			if (chan->loopStartSample < 0) {
				return;
			}

			chan->spent = chan->loopStartSample;
		}
	}
}

static qboolean ShouldSkipProcessingChannel(hrtf_state_t* state, hrtf_channel_t* chan) {
	if (chan->attenuation <= 0.f || chan->volume <= 0.f) {
		return true;
	}

	if (chan->entnum == cl.viewentity || chan->entchannel == ENTCHANNEL_PLAY_GLOBALLY) {
		return false;
	}

	const float distanceAttenuation = ComputeDistanceAttenuation(
		state->listenerOrigin, chan->origin, chan->attenuation
	);

	return distanceAttenuation <= 0.01f;
}

static IPLAudioBuffer subset = {0};
void MixAudio(hrtf_state_t* state, IPLAudioBuffer* mix) {
	AllocScratchBuffer(state->iplContext, mix->numChannels, mix->numSamples, &subset);

	for (size_t i = 0; i < CHANNELS_MAX; i++) {
		hrtf_channel_t* chan = &state->mixer[i];
		if (chan->sfx == NULL) {
			continue;
		}

		const hrtf_sfx_cache_entry_t* entry = (hrtf_sfx_cache_entry_t*) chan->sfx->cache.data;
		const IPLAudioBuffer* src = &entry->buf;
		CopyNextChannelFrame(state, chan, src, &subset);

		if (!ShouldSkipProcessingChannel(state, chan)) {
			// Self-sounds are always at full volume in both ears.
			if (chan->entnum != cl.viewentity && chan->entchannel != ENTCHANNEL_PLAY_GLOBALLY) {
				ApplyEffects(state, &subset, chan);
			}

			// FIXME: Find out why ApplyEffects generates NaNs then remove this.
			const int nans = CleanupNaNs(&subset);
			if (nans > 0 && developer.value > 0) {
				Con_Warning("HRTF: %d NaN samples before mixing channel #%lu\n", nans, i);
			}

			ApplyGain(&subset, chan->volume);
			iplAudioBufferMix(state->iplContext, &subset, mix);
		}

		if (chan->spent >= src->numSamples) {
			Con_DPrintf2("HRTF: channel #%lu spent\n", i);
			FreeChannel(state, chan);
			continue;
		}
	}
}

void FreeChannel(hrtf_state_t* state, hrtf_channel_t* chan) {
	if (chan->sfx != NULL) {
		FreeEffects(state, chan);
	}

	memset(chan, 0, sizeof(hrtf_channel_t));
}

static hrtf_channel_t* PickRandomChannel(hrtf_channel_t mixer[CHANNELS_MAX]) {
	for (size_t i = 0; i < CHANNELS_MAX; i++) {
		if (mixer[i].sfx == NULL) {
			return &mixer[i];
		}
	}

	return NULL;
}

hrtf_channel_t* PickChannel(hrtf_channel_t mixer[CHANNELS_MAX], int entnum, int entchannel) {
	if (entnum < 0 || entchannel == 0) {
		return PickRandomChannel(mixer);
	}

	// Replace existing channel if it exists.
	for (size_t i = 0; i < CHANNELS_MAX; i++) {
		if (mixer[i].sfx == NULL) {
			continue;
		}

		if (mixer[i].entnum == entnum && mixer[i].entchannel == entchannel) {
			return &mixer[i];
		}
	}

	return PickRandomChannel(mixer);
}

static IPLfloat32* acc;
// Can be applied in-place. Behavior ported from the vanilla driver, I have no
// idea what's happening here.
static void ApplyUnderwaterEffect(const float intensity, const IPLAudioBuffer* in, IPLAudioBuffer* out) {
	if (acc == NULL) {
		acc = malloc(sizeof(IPLfloat32) * out->numChannels);
		memset(acc, 0, sizeof(IPLfloat32) * out->numChannels);
	}

	if (intensity <= 0.f) {
		for (int i = 0; i < out->numChannels; i++) {
			acc[i] = in->data[i][out->numSamples-1];
		}
		iplAudioBufferCopy(out, in);
		return;
	}

	const float alpha = exp(-intensity * log(12.f));
	for (int i = 0; i < out->numChannels; i++) {
		for (int j = 0; j < out->numSamples; j++) {
			acc[i] += alpha * (in->data[i][j] - acc[i]);
			out->data[i][j] = acc[i];
		}
	}
}
