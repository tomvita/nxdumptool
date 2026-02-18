/*
 * title_extract.c
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

#include <core/nxdt_utils.h>
#include <core/title_extract.h>
#include <core/nca.h>
#include <core/pfs.h>
#include <core/romfs.h>
#include <core/tik.h>

#define TITLE_EXTRACT_BUFFER_SIZE  0x800000

static bool titleExtractCopyPfsEntryToFile(PartitionFileSystemContext *pfs_ctx, PartitionFileSystemEntry *pfs_entry, const char *out_path);
static bool titleExtractCopyRomFsEntryToFile(RomFileSystemContext *romfs_ctx, RomFileSystemFileEntry *file_entry, const char *out_path);
static NcmContentInfo *titleExtractGetFirstProgramContentInfo(TitleInfo *info);

bool titleExtractMainAndGlobalMetadata(u64 app_title_id, const char *base_output_dir, TitleExtractResult *out_result)
{
    if (!app_title_id || !base_output_dir || !*base_output_dir || !out_result)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    memset(out_result, 0, sizeof(TitleExtractResult));

    TitleUserApplicationData user_app_data = {0};
    if (!titleGetUserApplicationData(app_title_id, &user_app_data))
    {
        LOG_MSG_ERROR("Failed to retrieve user application data for %016lX.", app_title_id);
        return false;
    }

    TitleInfo *base_info = user_app_data.app_info;
    TitleInfo *patch_info = user_app_data.patch_info;
    TitleInfo *program_info = (patch_info ? patch_info : base_info);

    if (!program_info)
    {
        LOG_MSG_ERROR("Unable to retrieve program title info for %016lX.", app_title_id);
        titleFreeUserApplicationData(&user_app_data);
        return false;
    }

    NcmContentInfo *program_content_info = titleExtractGetFirstProgramContentInfo(program_info);
    if (!program_content_info)
    {
        LOG_MSG_ERROR("Program content info not found for %016lX.", app_title_id);
        titleFreeUserApplicationData(&user_app_data);
        return false;
    }

    Ticket tik = {0};
    NcaContext program_nca_ctx = {0};
    if (!ncaInitializeContext(&program_nca_ctx, program_info->storage_id, HashFileSystemPartitionType_None, &(program_info->meta_key), program_content_info, &tik))
    {
        LOG_MSG_ERROR("Failed to initialize Program NCA context for %016lX.", app_title_id);
        titleFreeUserApplicationData(&user_app_data);
        return false;
    }

    if (snprintf(out_result->output_dir, sizeof(out_result->output_dir), "%s/%016lX", base_output_dir, app_title_id) >= (int)sizeof(out_result->output_dir))
    {
        LOG_MSG_ERROR("Output directory path too long for %016lX.", app_title_id);
        titleFreeUserApplicationData(&user_app_data);
        return false;
    }

    utilsCreateDirectoryTree(out_result->output_dir, true);

    if (snprintf(out_result->main_path, sizeof(out_result->main_path), "%s/main", out_result->output_dir) >= (int)sizeof(out_result->main_path) ||
        snprintf(out_result->metadata_path, sizeof(out_result->metadata_path), "%s/global-metadata.dat", out_result->output_dir) >= (int)sizeof(out_result->metadata_path))
    {
        LOG_MSG_ERROR("Output file path too long for %016lX.", app_title_id);
        titleFreeUserApplicationData(&user_app_data);
        return false;
    }

    /* Extract ExeFS /main from the selected Program NCA (patch preferred). */
    for(u32 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        NcaFsSectionContext *fs_ctx = &(program_nca_ctx.fs_ctx[i]);
        if (!fs_ctx->enabled || fs_ctx->section_type != NcaFsSectionType_PartitionFs) continue;

        PartitionFileSystemContext pfs_ctx = {0};
        if (!pfsInitializeContext(&pfs_ctx, fs_ctx)) continue;

        PartitionFileSystemEntry *main_entry = pfsGetEntryByName(&pfs_ctx, "main");
        if (main_entry) out_result->main_extracted = titleExtractCopyPfsEntryToFile(&pfs_ctx, main_entry, out_result->main_path);

        pfsFreeContext(&pfs_ctx);
        if (out_result->main_extracted) break;
    }

    /* Extract RomFS /Data/Managed/Metadata/global-metadata.dat (merged patch view when possible). */
    RomFileSystemContext romfs_ctx = {0};
    RomFileSystemFileEntry *metadata_entry = NULL;
    NcaContext base_nca_ctx = {0};

    NcaFsSectionContext *program_patch_romfs_ctx = NULL;
    NcaFsSectionContext *program_base_romfs_ctx = NULL;

    for(u32 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        NcaFsSectionContext *fs_ctx = &(program_nca_ctx.fs_ctx[i]);
        if (!fs_ctx->enabled) continue;
        if (fs_ctx->section_type == NcaFsSectionType_PatchRomFs) program_patch_romfs_ctx = fs_ctx;
        if (fs_ctx->section_type == NcaFsSectionType_RomFs || fs_ctx->section_type == NcaFsSectionType_Nca0RomFs) program_base_romfs_ctx = fs_ctx;
    }

    if (program_patch_romfs_ctx)
    {
        bool fallback_to_merged_view = program_patch_romfs_ctx->has_patch_indirect_layer;

        /* Fast path: try patch-only RomFS first to avoid loading base Program NCA when possible. */
        /* Skip this when indirect patch data is present because it depends on base storage reads. */
        if (!fallback_to_merged_view)
        {
            if (romfsInitializeContext(&romfs_ctx, NULL, program_patch_romfs_ctx))
            {
                metadata_entry = romfsGetFileEntryByPath(&romfs_ctx, "/Data/Managed/Metadata/global-metadata.dat");
                if (metadata_entry)
                {
                    out_result->metadata_extracted = titleExtractCopyRomFsEntryToFile(&romfs_ctx, metadata_entry, out_result->metadata_path);
                    fallback_to_merged_view = !out_result->metadata_extracted;
                } else {
                    fallback_to_merged_view = true;
                }
            } else {
                fallback_to_merged_view = true;
            }
        }

        /* Fallback: load base Program NCA and use merged base+patch view. */
        if (fallback_to_merged_view && base_info && patch_info)
        {
            NcmContentInfo *base_program_content_info = titleGetContentInfoByTypeAndIdOffset(base_info, NcmContentType_Program, program_nca_ctx.id_offset);
            if (!base_program_content_info) base_program_content_info = titleExtractGetFirstProgramContentInfo(base_info);

            if (base_program_content_info &&
                ncaInitializeContext(&base_nca_ctx, base_info->storage_id, HashFileSystemPartitionType_None, &(base_info->meta_key), base_program_content_info, &tik))
            {
                NcaFsSectionContext *base_same_idx = &(base_nca_ctx.fs_ctx[program_patch_romfs_ctx->section_idx]);
                if (base_same_idx->enabled && (base_same_idx->section_type == NcaFsSectionType_RomFs || base_same_idx->section_type == NcaFsSectionType_Nca0RomFs))
                {
                    program_base_romfs_ctx = base_same_idx;
                }
            }

            romfsFreeContext(&romfs_ctx);
            if (program_base_romfs_ctx && romfsInitializeContext(&romfs_ctx, program_base_romfs_ctx, program_patch_romfs_ctx))
            {
                metadata_entry = romfsGetFileEntryByPath(&romfs_ctx, "/Data/Managed/Metadata/global-metadata.dat");
                if (metadata_entry) out_result->metadata_extracted = titleExtractCopyRomFsEntryToFile(&romfs_ctx, metadata_entry, out_result->metadata_path);
            }
        }
    } else
    if (program_base_romfs_ctx)
    {
        if (romfsInitializeContext(&romfs_ctx, program_base_romfs_ctx, NULL))
        {
            metadata_entry = romfsGetFileEntryByPath(&romfs_ctx, "/Data/Managed/Metadata/global-metadata.dat");
            if (metadata_entry) out_result->metadata_extracted = titleExtractCopyRomFsEntryToFile(&romfs_ctx, metadata_entry, out_result->metadata_path);
        }
    }

    romfsFreeContext(&romfs_ctx);
    titleFreeUserApplicationData(&user_app_data);

    return (out_result->main_extracted && out_result->metadata_extracted);
}

static bool titleExtractCopyPfsEntryToFile(PartitionFileSystemContext *pfs_ctx, PartitionFileSystemEntry *pfs_entry, const char *out_path)
{
    if (!pfs_ctx || !pfs_entry || !out_path) return false;

    FILE *fp = fopen(out_path, "wb");
    if (!fp) return false;

    u8 *buf = (u8*)malloc(TITLE_EXTRACT_BUFFER_SIZE);
    if (!buf)
    {
        fclose(fp);
        return false;
    }

    bool success = true;
    u64 offset = 0;

    while(offset < pfs_entry->size)
    {
        u64 chunk = MIN((u64)TITLE_EXTRACT_BUFFER_SIZE, (pfs_entry->size - offset));
        if (!pfsReadEntryData(pfs_ctx, pfs_entry, buf, chunk, offset))
        {
            success = false;
            break;
        }

        if (fwrite(buf, 1, chunk, fp) != chunk)
        {
            success = false;
            break;
        }

        offset += chunk;
    }

    free(buf);
    fclose(fp);
    return success;
}

static bool titleExtractCopyRomFsEntryToFile(RomFileSystemContext *romfs_ctx, RomFileSystemFileEntry *file_entry, const char *out_path)
{
    if (!romfs_ctx || !file_entry || !out_path) return false;

    FILE *fp = fopen(out_path, "wb");
    if (!fp) return false;

    u8 *buf = (u8*)malloc(TITLE_EXTRACT_BUFFER_SIZE);
    if (!buf)
    {
        fclose(fp);
        return false;
    }

    bool success = true;
    u64 offset = 0;

    while(offset < file_entry->size)
    {
        u64 chunk = MIN((u64)TITLE_EXTRACT_BUFFER_SIZE, (file_entry->size - offset));
        if (!romfsReadFileEntryData(romfs_ctx, file_entry, buf, chunk, offset))
        {
            success = false;
            break;
        }

        if (fwrite(buf, 1, chunk, fp) != chunk)
        {
            success = false;
            break;
        }

        offset += chunk;
    }

    free(buf);
    fclose(fp);
    return success;
}

static NcmContentInfo *titleExtractGetFirstProgramContentInfo(TitleInfo *info)
{
    if (!info || !info->content_count || !info->content_infos) return NULL;

    NcmContentInfo *program = titleGetContentInfoByTypeAndIdOffset(info, NcmContentType_Program, 0);
    if (program) return program;

    for(u32 i = 0; i < info->content_count; i++)
    {
        if (info->content_infos[i].content_type == NcmContentType_Program) return &(info->content_infos[i]);
    }

    return NULL;
}
