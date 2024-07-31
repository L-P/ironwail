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
#include "../quakedef.h"
#include "iface.h"
#include "hrtf/driver.h"

snd_iface_t snd_new_dma_impl(void);

cvar_t ambient_fade = {"ambient_fade", "100", CVAR_NONE};
cvar_t ambient_level = {"ambient_level", "0.3", CVAR_NONE};
cvar_t bgmvolume = {"bgmvolume", "1", CVAR_ARCHIVE};
cvar_t sfxvolume = {"volume", "0.7", CVAR_ARCHIVE};
cvar_t snd_mixspeed = {"snd_mixspeed", "44100", CVAR_NONE};
cvar_t snd_waterfx = {"snd_waterfx", "1", CVAR_ARCHIVE};

static snd_iface_t snd_impl = {0};

void SND_SetDriver(snd_iface_t impl) {
	snd_impl = impl;
}

void S_Init(void) {
	// TODO: allow switching implementations during startup.
#ifdef USE_STEAM_AUDIO
	SND_SetDriver(snd_new_hrtf_impl());
#else
	SND_SetDriver(snd_new_legacy_impl());
#endif

	Cvar_RegisterVariable(&ambient_fade);
	Cvar_RegisterVariable(&ambient_level);
	Cvar_RegisterVariable(&bgmvolume);
	Cvar_RegisterVariable(&sfxvolume);
	Cvar_RegisterVariable(&snd_mixspeed);
	Cvar_RegisterVariable(&snd_waterfx);

	snd_impl.Init();
}

void S_BeginPrecaching(void) {
	snd_impl.BeginPrecaching();
}

sfx_t *S_PrecacheSound(const char* name) {
	return snd_impl.PrecacheSound(name);
}

void S_BlockSound(void) {
	snd_impl.BlockSound();
}

void S_ClearBuffer(void) {
	snd_impl.ClearBuffer();
}

void S_ClearPrecache(void) {
	snd_impl.ClearPrecache();
}

void S_EndPrecaching(void) {
	snd_impl.EndPrecaching();
}

void S_ExtraUpdate(void) {
	snd_impl.ExtraUpdate();
}

void S_Shutdown(void) {
	snd_impl.Shutdown();
}

void S_StartSound(int entnum, int entchannel, sfx_t* sfx, vec3_t origin, float vol, float attenuation) {
	snd_impl.StartSound(entnum, entchannel, sfx, origin, vol, attenuation);
}

void S_Startup (void) {
	snd_impl.Startup();
}

void S_StaticSound (sfx_t* sfx, vec3_t origin, float vol, float attenuation) {
	snd_impl.StaticSound(sfx, origin, vol, attenuation);
}

void S_StopAllSounds(qboolean clear) {
	snd_impl.StopAllSounds(clear);
}

void S_StopSound (int entnum, int entchannel) {
	snd_impl.StopSound(entnum, entchannel);
}

void S_TouchSound (const char *name) {
	snd_impl.TouchSound(name);
}

void S_UnblockSound (void) {
	snd_impl.UnblockSound();
}

void S_Update (vec3_t origin, vec3_t forward, vec3_t right, vec3_t up) {
	snd_impl.Update(origin, forward, right, up);
}

void S_LocalSound (const char* name) {
	snd_impl.LocalSound(name);
}

qboolean BGM_Init() {
	return snd_impl.BGMInit();
}

void BGM_Pause() {
	// VID_SetMode calls this before sound is initialized.
	if (snd_impl.BGMPause == NULL) {
		return;
	}

	snd_impl.BGMPause();
}

void BGM_Play(const char* path) {
	snd_impl.BGMPlay(path);
}

void BGM_PlayCDtrack(byte trackID, qboolean loop) {
	snd_impl.BGMPlayCDtrack(trackID, loop);
}

void BGM_Resume() {
	// VID_SetMode calls this before sound is initialized.
	if (snd_impl.BGMResume == NULL) {
		return;
	}

	snd_impl.BGMResume();
}

void BGM_Shutdown() {
	snd_impl.BGMShutdown();
}

void BGM_Stop() {
	snd_impl.BGMStop();
}

void BGM_Update() {
	snd_impl.BGMUpdate();
}
