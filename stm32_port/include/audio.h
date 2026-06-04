#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

// --- SOUND ID MACROS ---
#define SND_ACHTUNG      0
#define SND_DIE          1
#define SND_BARK         2
#define SND_DOOR_OPEN1   3
#define SND_DOOR_CLOSE1  4
#define SND_GUN_FIRE1    5
#define SND_GUN_FIRE2    6
#define SND_HEALTH       7
#define SND_AMMO         8
#define SND_PLAYER_DEATH 9   
#define SND_LEVEL_DONE   10  

#define NUM_SOUNDS       11  

// --- BGM TRACK MACROS ---
#define BGM_NONE    0
#define BGM_SPLASH  1
#define BGM_EERIE   2

void play_bgm(int track_id);
void audio_init(void);
void play_sound(int sound_id);
void audio_tick(void);
void play_bgm(int enable);

#endif // AUDIO_H
