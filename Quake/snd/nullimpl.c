#include "../quakedef.h"
#include "iface.h"

static void SNDNULL_Init(void) {
	Con_Printf("S_Init()\n");
}

static void SNDNULL_BeginPrecaching (void) {
	Con_Printf("S_BeginPrecaching()\n");
}

static sfx_t *SNDNULL_PrecacheSound (const char *sample) {
	Con_Printf("S_PrecacheSound(%s)\n", sample);
	return NULL;
}

static void SNDNULL_BlockSound (void) {
	Con_Printf("S_BlockSound()\n");
}

static void SNDNULL_ClearBuffer (void) {
	Con_Printf("S_ClearBuffer()\n");
}

static void SNDNULL_ClearPrecache (void) {
	Con_Printf("S_ClearPrecache()\n");
}

static void SNDNULL_EndPrecaching (void) {
	Con_Printf("S_EndPrecaching()\n");
}

static void SNDNULL_ExtraUpdate (void) {
	// Silenced, called multiple times per frame.
	// Con_Printf("S_ExtraUpdate()\n");
}

static void SNDNULL_Shutdown (void) {
	Con_Printf("S_Shutdown()\n");
}

static void SNDNULL_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation) {
	Con_Printf(
		"S_StartSound(entnum %d, entchannel %d, sfx %p, origin(%f, %f, %f), fvol %f, attenuation %f)\n",
		entnum, entchannel, sfx, origin[0], origin[1], origin[2], fvol, attenuation
	);
}

static void SNDNULL_Startup (void) {
	Con_Printf("S_Startup()\n");
}

static void SNDNULL_StaticSound (sfx_t *sfx, vec3_t origin, float fvol, float attenuation) {
	Con_Printf(
		"S_StaticSound(sfx %p, origin(%f, %f, %f), fvol %f, attenuation %f)\n",
		sfx, origin[0], origin[1], origin[2], fvol, attenuation
	);
}

static void SNDNULL_StopAllSounds(qboolean clear) {
	Con_Printf("S_StopAllSounds(%d)\n", clear);
}

static void SNDNULL_StopSound (int entnum, int entchannel) {
	Con_Printf("S_StopSound(%d, %d)\n", entnum, entchannel);
}

static void SNDNULL_TouchSound (const char *sample) {
	Con_Printf("S_TouchSound(%s)\n", sample);
}

static void SNDNULL_UnblockSound (void) {
	Con_Printf("S_UnblockSound()\n");
}

static void SNDNULL_Update (vec3_t origin, vec3_t forward, vec3_t right, vec3_t up) {
	/* Silenced, called once per frame.
	Con_Printf(
		"S_Update(origin(%f, %f, %f), forward(%f, %f, %f), right(%f, %f, %f), up(%f, %f, %f))\n",
		origin[0], origin[1], origin[2],
		forward[0], forward[1], forward[2],
		right[0], right[1], right[2],
		up[0], up[1], up[2]
	);
	// */
}

static void SNDNULL_LocalSound (const char *name) {
	Con_Printf("S_LocalSound(%s)\n", name);
}

// {{{ BGM
qboolean SNDNULL_BGMInit() {
	Con_Printf("BGM_Init()\n");
	return true;
}

void SNDNULL_BGMPause() {
	Con_Printf("BGM_Pause()\n");
}

void SNDNULL_BGMPlay(const char* path) {
	Con_Printf("BGM_Play(%s)\n", path);
}

void SNDNULL_BGMPlayCDtrack(byte trackID, qboolean loop) {
	Con_Printf("BGM_PlayCDtrack(%d, %d)\n", trackID, loop);
}

void SNDNULL_BGMResume() {
	Con_Printf("BGM_Resume()\n");
}

void SNDNULL_BGMShutdown() {
	Con_Printf("BGM_Shutdown()\n");
}

void SNDNULL_BGMStop() {
	Con_Printf("BGM_Stop()\n");
}

void SNDNULL_BGMUpdate() {
	// Silenced, called once per frame.
	// Con_Printf("BGM_Update()\n");
}
// }}} BGM

snd_iface_t snd_new_null_impl(void) {
	const snd_iface_t ret = {
		.BeginPrecaching = SNDNULL_BeginPrecaching,
		.BlockSound      = SNDNULL_BlockSound,
		.ClearBuffer     = SNDNULL_ClearBuffer,
		.ClearPrecache   = SNDNULL_ClearPrecache,
		.EndPrecaching   = SNDNULL_EndPrecaching,
		.ExtraUpdate     = SNDNULL_ExtraUpdate,
		.Init            = SNDNULL_Init,
		.LocalSound      = SNDNULL_LocalSound,
		.PrecacheSound   = SNDNULL_PrecacheSound,
		.Shutdown        = SNDNULL_Shutdown,
		.StartSound      = SNDNULL_StartSound,
		.Startup         = SNDNULL_Startup,
		.StaticSound     = SNDNULL_StaticSound,
		.StopAllSounds   = SNDNULL_StopAllSounds,
		.StopSound       = SNDNULL_StopSound,
		.TouchSound      = SNDNULL_TouchSound,
		.UnblockSound    = SNDNULL_UnblockSound,
		.Update          = SNDNULL_Update,

		.BGMInit = SNDNULL_BGMInit,
		.BGMPause = SNDNULL_BGMPause,
		.BGMPlay = SNDNULL_BGMPlay,
		.BGMPlayCDtrack = SNDNULL_BGMPlayCDtrack,
		.BGMResume = SNDNULL_BGMResume,
		.BGMShutdown = SNDNULL_BGMShutdown,
		.BGMStop = SNDNULL_BGMStop,
		.BGMUpdate = SNDNULL_BGMUpdate,
	};
	return ret;
}
