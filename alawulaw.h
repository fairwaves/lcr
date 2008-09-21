/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** alawulaw header file                                                      **
**                                                                           **
\*****************************************************************************/ 
extern signed int *audio_law_to_s32;
extern unsigned char audio_s16_to_law[65536];
extern short audio_alaw_relations[];
void generate_tables(char law);
