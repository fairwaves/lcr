/* HR (GSM 06.20) codec */

/*
 * This file is part of gapk (GSM Audio Pocket Knife).
 *
 * gapk is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gapk is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gapk.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GSM_HR_H__
#define __GSM_HR_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct gsmhr;

struct gsmhr *gsmhr_init(void);
void          gsmhr_exit(struct gsmhr *state);
int           gsmhr_encode(struct gsmhr *state, int16_t *hr_params, const int16_t *pcm);
int           gsmhr_decode(struct gsmhr *state, int16_t *pcm, const int16_t *hr_params);

#ifdef __cplusplus
}
#endif

#endif /* __GSM_HR_H__ */
