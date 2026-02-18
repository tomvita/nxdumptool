/*
 * title_extract.h
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __TITLE_EXTRACT_H__
#define __TITLE_EXTRACT_H__

#include "title.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool main_extracted;
    bool metadata_extracted;
    char output_dir[FS_MAX_PATH];
    char main_path[FS_MAX_PATH];
    char metadata_path[FS_MAX_PATH];
} TitleExtractResult;

/// Extracts `main` and `global-metadata.dat` from the selected user title.
/// If a patch is available, Program data from the patch is preferred.
/// `base_output_dir` is expected to be a writable path (e.g. "sdmc:/switch/nxdumptool/extracted").
/// Returns true only if both files were successfully extracted.
bool titleExtractMainAndGlobalMetadata(u64 app_title_id, const char *base_output_dir, TitleExtractResult *out_result);

#ifdef __cplusplus
}
#endif

#endif /* __TITLE_EXTRACT_H__ */
