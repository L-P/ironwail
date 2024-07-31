#pragma once

extern cvar_t ambient_fade;
extern cvar_t ambient_level;
extern cvar_t bgmvolume;
extern cvar_t sfxvolume;
extern cvar_t snd_mixspeed;
extern cvar_t snd_waterfx;

typedef struct sfx_s sfx_t;

typedef struct {
	sfx_t* (*PrecacheSound) (const char *name);
	void (*BeginPrecaching) (void);
	void (*BlockSound) (void);
	void (*ClearBuffer) (void);
	void (*ClearPrecache) (void);
	void (*EndPrecaching) (void);
	void (*ExtraUpdate) (void);
	void (*Init) (void);
	void (*LocalSound) (const char *name);
	void (*Shutdown) (void);
	void (*StartSound) (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation);
	void (*Startup) (void);
	void (*StaticSound) (sfx_t *sfx, vec3_t origin, float fvol, float attenuation);
	void (*StopAllSounds) (qboolean clear);
	void (*StopSound) (int entnum, int entchannel);
	void (*TouchSound) (const char *sample);
	void (*UnblockSound) (void);
	void (*Update) (vec3_t origin, vec3_t forward, vec3_t right, vec3_t up);

	qboolean (*BGMInit) (void); 
	void (*BGMPause) (void); 
	void (*BGMPlay) (const char *filename); 
	void (*BGMPlayCDtrack) (byte track, qboolean looping);
	void (*BGMResume) (void); 
	void (*BGMShutdown) (void); 
	void (*BGMStop) (void); 
	void (*BGMUpdate) (void); 
} snd_iface_t;

snd_iface_t snd_new_null_impl(void);
snd_iface_t snd_new_legacy_impl(void);
void SND_SetDriver(snd_iface_t impl);
