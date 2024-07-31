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

#define vecMulToIPL(v, f) {.x=(v)[0]*(f), .y=(v)[1]*(f), .z=(v)[2]*(f)}

static IPLAudioBuffer bufZeroes = {0};
static IPLAudioBuffer bufGarbage = {0};

// BUG: ApplyBinauralEffect will produce NaNs on the first frame.
// "Seed" the effect by feeding it a frame of zeroes.
static void ApplyBinauralEffect(hrtf_state_t* state, hrtf_channel_t* chan, IPLAudioBuffer* in, IPLAudioBuffer* out);
static void InitBinauralEffect(hrtf_state_t* state, hrtf_channel_t* chan) {
	AllocScratchBuffer(state->iplContext, state->audioSpec.channels, FRAME_SIZE, &bufGarbage);
	if (AllocScratchBuffer(state->iplContext, state->audioSpec.channels, FRAME_SIZE, &bufZeroes)) {
		iplAudioBufferZero(&bufZeroes);
	}
	ApplyBinauralEffect(state, chan, &bufZeroes, &bufGarbage);
}

qboolean AllocEffects(hrtf_state_t* state, hrtf_channel_t* chan) {
	IPLBinauralEffectSettings binauralEffectSettings = {.hrtf = state->iplHRTF};
	IPLerror err = iplBinauralEffectCreate(
		state->iplContext,
		&state->iplAudioSettings,
		&binauralEffectSettings,
		&chan->iplBinauralEffect
	);
	if (err != IPL_STATUS_SUCCESS) {
		Con_Warning("HRTF: unable to allocate IPLBinauralEffect, got error code: #%d\n", err);
		return false;
	}
	// FIXME: Find out why HRTF's first frame is always NaNs.
	InitBinauralEffect(state, chan);

	IPLDirectEffectSettings directEffectSettings = {
		.numChannels = state->audioSpec.channels,
	};
	err = iplDirectEffectCreate(
		state->iplContext,
		&state->iplAudioSettings,
		&directEffectSettings,
		&chan->iplDirectEffect
	);
	if (err != IPL_STATUS_SUCCESS) {
		Con_Warning("HRTF: unable to allocate IPLDirectEffectSettings, got error code: #%d\n", err);
		return false;
	}

	return true;
}

static void ApplyDirectEffect(hrtf_state_t* state, hrtf_channel_t* chan, IPLAudioBuffer* in, IPLAudioBuffer* out) {
	const float scale = 1.f / (128.f / chan->attenuation);
	const IPLVector3 sourcePosition = vecMulToIPL(chan->origin, scale);
	const IPLVector3 listenerPosition = vecMulToIPL(state->listenerOrigin, scale);
	const float distanceAttenuation = ComputeDistanceAttenuation(
		state->listenerOrigin, chan->origin, chan->attenuation
	);

	IPLDirectEffectParams directEffectParams = {
		.flags =
			IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION |
			IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION,
		.distanceAttenuation = distanceAttenuation,
	};
	IPLAirAbsorptionModel airAbsorptionModel = {
		.type = IPL_AIRABSORPTIONTYPE_DEFAULT,
	};
	iplAirAbsorptionCalculate(
		state->iplContext,
		sourcePosition,
		listenerPosition,
		&airAbsorptionModel,
		directEffectParams.airAbsorption
	);

	iplDirectEffectApply(chan->iplDirectEffect, &directEffectParams, in, out);

	return true;
}

static void ApplyBinauralEffect(hrtf_state_t* state, hrtf_channel_t* chan, IPLAudioBuffer* in, IPLAudioBuffer* out) {
	vec3_t delta = {0};
	VectorSubtract(chan->origin, state->listenerOrigin, delta);
	VectorNormalize(delta);
	IPLVector3 direction = {
		.x = DotProduct(delta, state->listenerRight),
		.y = DotProduct(delta, state->listenerForward),
		.z = DotProduct(delta, state->listenerUp),
	};

	/* > Incurs a relatively high CPU overhead as compared to nearest-neighbor
	> filtering, so use this for sounds where it has a significant benefit.
	> Typically, bilinear filtering is most useful for wide-band noise-like
	> sounds, such as radio static, mechanical noise, fire, etc.

	We have a lot of those in static sounds, other sounds are
	probably too short-lived to be worth interpolating. */
	int interpolation = IPL_HRTFINTERPOLATION_NEAREST;
	if (chan->entnum == ENTNUM_STATIC) {
		interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
	}

	IPLBinauralEffectParams binauralEffectParams = {
        .direction = direction,
        .interpolation = interpolation,
        .spatialBlend = 1.0f,
        .hrtf = state->iplHRTF,
	};

	iplBinauralEffectApply(chan->iplBinauralEffect, &binauralEffectParams, in, out);
}

static IPLAudioBuffer scratch = {0};
void ApplyEffects(hrtf_state_t* state, IPLAudioBuffer* buf, hrtf_channel_t* chan) {
	AllocScratchBuffer(state->iplContext, buf->numChannels, buf->numSamples, &scratch);

	// FIXME: Find out why I get all NaNs when inverting the two effects.
	ApplyDirectEffect(state, chan, buf, &scratch);
	iplAudioBufferZero(buf);
	ApplyBinauralEffect(state, chan, &scratch, buf);
}

void FreeEffects(hrtf_state_t* state, hrtf_channel_t* chan) {
	iplBinauralEffectRelease(&chan->iplBinauralEffect);
	iplDirectEffectRelease(&chan->iplDirectEffect);
}

// FIXME: We re-use the vanilla attenuation calculations because the game
// was designed around it. The default IPL model doesn't cut sound off
// early enough and has a very long tail. When proper propagation is
// implemented using actual level-geometry, switch back and see if we can
// still hear 5 moans/s at the start of e1m1.
float ComputeDistanceAttenuation(
       const vec3_t listenerOrigin,
       const vec3_t sourceOrigin,
       const float attenuation
) {
#if 1
   vec3_t delta = {0};
   VectorSubtract(sourceOrigin, listenerOrigin, delta);
   const float dist = VectorNormalize(delta) * (attenuation / 1000.f);

   return CLAMP(0.f, 1.f - dist, 1.f);
#else
   const float scale = 1.f / (128.f / attenuation);
   const IPLVector3 sourcePosition = vecMulToIPL(sourceOrigin, scale);
   const IPLVector3 listenerPosition = vecMulToIPL(listenerOrigin, scale);

   IPLDistanceAttenuationModel distanceAttenuationModel = {
       .type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT,
   };
   return iplDistanceAttenuationCalculate(
       state->iplContext,
       sourcePosition,
       listenerPosition,
       &distanceAttenuationModel
   );
#endif
}
