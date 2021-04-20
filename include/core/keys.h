/*
 * keys.h
 *
 * Copyright (c) 2018-2020, SciresM.
 * Copyright (c) 2019, shchmue.
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __KEYS_H__
#define __KEYS_H__

#ifdef __cplusplus
extern "C" {
#endif

bool keysLoadNcaKeyset(void);

const u8 *keysGetNcaHeaderKey(void);
const u8 *keysGetKeyAreaEncryptionKeySource(u8 kaek_index);
const u8 *keysGetEticketRsaKek(bool personalized);
const u8 *keysGetTitlekek(u8 key_generation);
const u8 *keysGetKeyAreaEncryptionKey(u8 key_generation, u8 kaek_index);

#ifdef __cplusplus
}
#endif

#endif /* __KEYS_H__ */