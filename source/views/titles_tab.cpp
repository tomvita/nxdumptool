/*
 * titles_tab.cpp
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

#include <views/titles_tab.hpp>
#include <core/nca.h>
#include <core/pfs.h>
#include <core/romfs.h>
#include <core/tik.h>
#include <core/title_extract.h>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::views
{
    typedef struct {
        u32 entry_count;
        u32 max_entries;
        bool truncated;
    } TitlesTabLogWalkState;

    static void TitlesTabAppendToLogFile(const char *fmt, ...)
    {
        const char *primary_log_path = DEVOPTAB_SDMC_DEVICE APP_BASE_PATH "title.log";
        const char *fallback_log_path = DEVOPTAB_SDMC_DEVICE "/title.log";

        /* Make sure the output directory exists on the SD card. */
        utilsCreateDirectoryTree(DEVOPTAB_SDMC_DEVICE HBMENU_BASE_PATH APP_TITLE, true);

        FILE *fp = fopen(primary_log_path, "ab");
        const char *used_path = primary_log_path;
        if (!fp)
        {
            fp = fopen(fallback_log_path, "ab");
            used_path = fallback_log_path;
        }

        if (!fp)
        {
            LOG_MSG_ERROR("Failed to open title log file for append at \"%s\" and \"%s\"!", primary_log_path, fallback_log_path);
            return;
        }

        (void)used_path;

        va_list args;
        va_start(args, fmt);
        vfprintf(fp, fmt, args);
        va_end(args);

        fwrite(CRLF, 1, strlen(CRLF), fp);
        fclose(fp);

        LOG_MSG_DEBUG("Wrote selected title details to \"%s\".", used_path);
    }

    static void TitlesTabResetLogFile(void)
    {
        const char *primary_log_path = DEVOPTAB_SDMC_DEVICE APP_BASE_PATH "title.log";
        const char *fallback_log_path = DEVOPTAB_SDMC_DEVICE "/title.log";

        utilsCreateDirectoryTree(DEVOPTAB_SDMC_DEVICE HBMENU_BASE_PATH APP_TITLE, true);

        FILE *fp = fopen(primary_log_path, "wb");
        if (!fp) fp = fopen(fallback_log_path, "wb");
        if (fp) fclose(fp);
    }

    static bool TitlesTabLogWalkCanProceed(TitlesTabLogWalkState *state)
    {
        return (state && !state->truncated && state->entry_count < state->max_entries);
    }

    static void TitlesTabLogWalkCount(TitlesTabLogWalkState *state)
    {
        if (!state) return;
        state->entry_count++;
        if (state->entry_count >= state->max_entries) state->truncated = true;
    }

    static void TitlesTabLogRomFsDirectoryRecursive(RomFileSystemContext *romfs_ctx, RomFileSystemDirectoryEntry *dir_entry, const std::string& base_path, TitlesTabLogWalkState *state)
    {
        if (!romfs_ctx || !dir_entry || !TitlesTabLogWalkCanProceed(state)) return;

        /* Process child directories. */
        u64 cur_dir_offset = dir_entry->directory_offset;
        while(cur_dir_offset != ROMFS_VOID_ENTRY && TitlesTabLogWalkCanProceed(state))
        {
            RomFileSystemDirectoryEntry *child_dir = romfsGetDirectoryEntryByOffset(romfs_ctx, cur_dir_offset);
            if (!child_dir) break;

            std::string child_name(child_dir->name, child_dir->name_length);
            std::string child_path = base_path + "/" + child_name;

            TitlesTabAppendToLogFile("[DIR] %s", child_path.c_str());
            TitlesTabLogWalkCount(state);

            TitlesTabLogRomFsDirectoryRecursive(romfs_ctx, child_dir, child_path, state);
            cur_dir_offset = child_dir->next_offset;
        }

        /* Process child files. */
        u64 cur_file_offset = dir_entry->file_offset;
        while(cur_file_offset != ROMFS_VOID_ENTRY && TitlesTabLogWalkCanProceed(state))
        {
            RomFileSystemFileEntry *file_entry = romfsGetFileEntryByOffset(romfs_ctx, cur_file_offset);
            if (!file_entry) break;

            std::string file_name(file_entry->name, file_entry->name_length);
            std::string file_path = base_path + "/" + file_name;

            TitlesTabAppendToLogFile("[FILE] %s (0x%lX bytes)", file_path.c_str(), file_entry->size);
            if (file_name == "global-metadata.dat")
            {
                TitlesTabAppendToLogFile("[IMPORTANT] Found Unity metadata file: %s (0x%lX bytes)", file_path.c_str(), file_entry->size);
            }
            TitlesTabLogWalkCount(state);

            cur_file_offset = file_entry->next_offset;
        }
    }

    static void TitlesTabLogRomFsTree(NcaFsSectionContext *nca_fs_ctx, const char *root_name, TitlesTabLogWalkState *state)
    {
        if (!nca_fs_ctx || !root_name || !TitlesTabLogWalkCanProceed(state)) return;

        RomFileSystemContext romfs_ctx = {0};
        RomFileSystemDirectoryEntry *root_dir = NULL;

        NcaFsSectionContext *base_nca_fs_ctx = (nca_fs_ctx->section_type == NcaFsSectionType_PatchRomFs ? NULL : nca_fs_ctx);
        NcaFsSectionContext *patch_nca_fs_ctx = (nca_fs_ctx->section_type == NcaFsSectionType_PatchRomFs ? nca_fs_ctx : NULL);

        if (!romfsInitializeContext(&romfs_ctx, base_nca_fs_ctx, patch_nca_fs_ctx))
        {
            TitlesTabAppendToLogFile("Failed to initialize RomFS context for \"%s\".", root_name);
            return;
        }

        root_dir = romfsGetDirectoryEntryByPath(&romfs_ctx, "/");
        if (!root_dir)
        {
            TitlesTabAppendToLogFile("Failed to retrieve RomFS root directory for \"%s\".", root_name);
            romfsFreeContext(&romfs_ctx);
            return;
        }

        TitlesTabAppendToLogFile("[DIR] /%s", root_name);
        TitlesTabLogWalkCount(state);
        TitlesTabLogRomFsDirectoryRecursive(&romfs_ctx, root_dir, std::string("/") + root_name, state);

        romfsFreeContext(&romfs_ctx);
    }

    static void TitlesTabLogPatchRomFsTree(NcaFsSectionContext *base_nca_fs_ctx, NcaFsSectionContext *patch_nca_fs_ctx, const char *root_name, TitlesTabLogWalkState *state)
    {
        if (!base_nca_fs_ctx || !patch_nca_fs_ctx || !root_name || !TitlesTabLogWalkCanProceed(state)) return;

        RomFileSystemContext romfs_ctx = {0};
        RomFileSystemDirectoryEntry *root_dir = NULL;

        if (!romfsInitializeContext(&romfs_ctx, base_nca_fs_ctx, patch_nca_fs_ctx))
        {
            TitlesTabAppendToLogFile("Failed to initialize merged Patch RomFS context for \"%s\".", root_name);
            return;
        }

        root_dir = romfsGetDirectoryEntryByPath(&romfs_ctx, "/");
        if (!root_dir)
        {
            TitlesTabAppendToLogFile("Failed to retrieve merged Patch RomFS root directory for \"%s\".", root_name);
            romfsFreeContext(&romfs_ctx);
            return;
        }

        TitlesTabAppendToLogFile("[DIR] /%s", root_name);
        TitlesTabLogWalkCount(state);
        TitlesTabLogRomFsDirectoryRecursive(&romfs_ctx, root_dir, std::string("/") + root_name, state);

        romfsFreeContext(&romfs_ctx);
    }

    static void TitlesTabLogPfsTree(NcaFsSectionContext *nca_fs_ctx, const char *root_name, TitlesTabLogWalkState *state)
    {
        if (!nca_fs_ctx || !root_name || !TitlesTabLogWalkCanProceed(state)) return;

        PartitionFileSystemContext pfs_ctx = {};
        if (!pfsInitializeContext(&pfs_ctx, nca_fs_ctx))
        {
            TitlesTabAppendToLogFile("Failed to initialize Partition FS context for \"%s\".", root_name);
            return;
        }

        TitlesTabAppendToLogFile("[DIR] /%s", root_name);
        TitlesTabLogWalkCount(state);

        u32 entry_count = pfsGetEntryCount(&pfs_ctx);
        for(u32 i = 0; i < entry_count && TitlesTabLogWalkCanProceed(state); i++)
        {
            PartitionFileSystemEntry *fs_entry = pfsGetEntryByIndex(&pfs_ctx, i);
            char *name = pfsGetEntryNameByIndex(&pfs_ctx, i);
            if (!fs_entry || !name || !*name) continue;

            TitlesTabAppendToLogFile("[FILE] /%s/%s (0x%lX bytes)", root_name, name, fs_entry->size);
            TitlesTabLogWalkCount(state);
        }

        pfsFreeContext(&pfs_ctx);
    }

    static std::string TitlesTabSanitizePathComponent(const char *str)
    {
        if (!str || !*str) return "unknown";

        std::string out{};
        for(const char *p = str; *p; p++)
        {
            char c = *p;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            {
                out.push_back((char)tolower(c));
            } else {
                out.push_back('-');
            }
        }

        while(!out.empty() && out.front() == '-') out.erase(out.begin());
        while(!out.empty() && out.back() == '-') out.pop_back();

        return (out.empty() ? "unknown" : out);
    }

    static void TitlesTabLogContentFsTree(TitleInfo *title_info, NcmContentInfo *content_info, Ticket *tik, TitlesTabLogWalkState *state)
    {
        if (!title_info || !content_info || !tik || !state || !TitlesTabLogWalkCanProceed(state)) return;

        NcaContext nca_ctx = {};
        u64 content_size = 0;
        ncmContentInfoSizeToU64(content_info, &content_size);

        if (!ncaInitializeContext(&nca_ctx, title_info->storage_id, HashFileSystemPartitionType_None, &(title_info->meta_key), content_info, tik))
        {
            TitlesTabAppendToLogFile("Failed to initialize NCA context for content type %s (id_offset 0x%02X, size 0x%lX).",
                                     titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset, content_size);
            return;
        }

        TitlesTabAppendToLogFile("Content NCA: %s \"%s\" (id_offset 0x%02X, size %s).",
                                 titleGetNcmContentTypeName(nca_ctx.content_type), nca_ctx.content_id_str, nca_ctx.id_offset, nca_ctx.content_size_str);

        std::string content_type_name = TitlesTabSanitizePathComponent(titleGetNcmContentTypeName(nca_ctx.content_type));
        std::string nca_prefix = std::string("nca-") + nca_ctx.content_id_str;

        for(u32 i = 0; i < NCA_FS_HEADER_COUNT && TitlesTabLogWalkCanProceed(state); i++)
        {
            NcaFsSectionContext *nca_fs_ctx = &(nca_ctx.fs_ctx[i]);
            if (!nca_fs_ctx->enabled) continue;

            std::string section_type_name = TitlesTabSanitizePathComponent(ncaGetFsSectionTypeName(nca_fs_ctx));
            std::string dynamic_root = nca_prefix + "/sec" + std::to_string(nca_fs_ctx->section_idx) + "-" + content_type_name + "-" + section_type_name;

            switch(nca_fs_ctx->section_type)
            {
                case NcaFsSectionType_PartitionFs:
                    TitlesTabLogPfsTree(nca_fs_ctx, dynamic_root.c_str(), state);
                    break;
                case NcaFsSectionType_RomFs:
                case NcaFsSectionType_Nca0RomFs:
                    TitlesTabLogRomFsTree(nca_fs_ctx, dynamic_root.c_str(), state);
                    break;
                case NcaFsSectionType_PatchRomFs:
                {
                    /* Pair patch Program RomFS with its matching base Program RomFS. */
                    bool used_merged_view = false;

                    if (title_info->meta_key.type == NcmContentMetaType_Patch && nca_ctx.content_type == NcmContentType_Program)
                    {
                        TitleInfo *base_title_info = titleGetTitleInfoEntryFromStorageByTitleId(NcmStorageId_Any, titleGetApplicationIdByPatchId(title_info->meta_key.id));
                        if (base_title_info)
                        {
                            NcmContentInfo *base_content_info = titleGetContentInfoByTypeAndIdOffset(base_title_info, NcmContentType_Program, nca_ctx.id_offset);
                            if (base_content_info)
                            {
                                NcaContext base_nca_ctx = {};
                                if (ncaInitializeContext(&base_nca_ctx, base_title_info->storage_id, HashFileSystemPartitionType_None, &(base_title_info->meta_key), base_content_info, tik))
                                {
                                    NcaFsSectionContext *base_fs_ctx = &(base_nca_ctx.fs_ctx[nca_fs_ctx->section_idx]);
                                    if (base_fs_ctx->enabled && (base_fs_ctx->section_type == NcaFsSectionType_RomFs || base_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs))
                                    {
                                        TitlesTabLogPatchRomFsTree(base_fs_ctx, nca_fs_ctx, dynamic_root.c_str(), state);
                                        used_merged_view = true;
                                    }
                                }
                            }

                            titleFreeTitleInfo(&base_title_info);
                        }
                    }

                    if (!used_merged_view) TitlesTabLogRomFsTree(nca_fs_ctx, dynamic_root.c_str(), state);
                    break;
                }
                default:
                    break;
            }
        }
    }

    static void TitlesTabLogInfoBlockTree(const char *prefix, TitleInfo *info, TitlesTabLogWalkState *state)
    {
        if (!prefix || !info || !state) return;

        /* Walk to first entry in list (if needed), then traverse forward. */
        while(info->previous) info = info->previous;

        Ticket tik = {};

        for(TitleInfo *cur = info; cur && TitlesTabLogWalkCanProceed(state); cur = cur->next)
        {
            const char *storage_name = titleGetNcmStorageIdName(cur->storage_id);
            const char *meta_type_name = titleGetNcmContentMetaTypeName(cur->meta_key.type);

            TitlesTabAppendToLogFile("[%s] title_id %016lX, storage %s, meta_type %s, version %u, content_count %u.",
                                     prefix, cur->meta_key.id, (storage_name ? storage_name : "Unknown"),
                                     (meta_type_name ? meta_type_name : "Unknown"), cur->version.value, cur->content_count);

            for(u32 i = 0; i < cur->content_count && TitlesTabLogWalkCanProceed(state); i++)
            {
                TitlesTabLogContentFsTree(cur, &(cur->content_infos[i]), &tik, state);
            }
        }
    }

    static u32 TitlesTabCountInfoEntries(const TitleInfo *info)
    {
        u32 count = 0;
        for(; info; info = info->next) count++;
        return count;
    }

    static void TitlesTabLogInfoEntry(const char *prefix, const TitleInfo *info)
    {
        if (!info)
        {
            TitlesTabAppendToLogFile("%s: <none>", prefix);
            return;
        }

        const char *storage_name = titleGetNcmStorageIdName(info->storage_id);
        const char *meta_type_name = titleGetNcmContentMetaTypeName(info->meta_key.type);

        TitlesTabAppendToLogFile("%s: title_id %016lX, storage %s (%u), meta_type %s (%u), version %u, content_count %u, size %s (0x%lX).",
                                 prefix, info->meta_key.id, (storage_name ? storage_name : "Unknown"), info->storage_id,
                                 (meta_type_name ? meta_type_name : "Unknown"), info->meta_key.type, info->version.value, info->content_count, info->size_str, info->size);
    }

    TitlesTabPopup::TitlesTabPopup(const TitleApplicationMetadata *app_metadata, bool is_system) : brls::TabFrame(), app_metadata(app_metadata), is_system(is_system)
    {
        u64 title_id = this->app_metadata->title_id;
        bool user_ret = false;

        if (!this->is_system)
        {
            /* Get user application data. */
            user_ret = titleGetUserApplicationData(title_id, &(this->user_app_data));
        } else {
            /* Get system title info. */
            this->system_title_info = titleGetTitleInfoEntryFromStorageByTitleId(NcmStorageId_BuiltInSystem, title_id);
        }

        /* Make sure we got title information. This should never get triggered. */
        if ((!this->is_system && !user_ret) || (this->is_system && !this->system_title_info)) throw fmt::format("Failed to retrieve title information for {:016X}.", title_id);

        /* Add tabs. */
        this->addTab("Red", new brls::Rectangle(nvgRGB(255, 0, 0)));
        this->addTab("Green", new brls::Rectangle(nvgRGB(0, 255, 0)));
        this->addTab("Blue", new brls::Rectangle(nvgRGB(0, 0, 255)));
    }

    TitlesTabPopup::~TitlesTabPopup()
    {
        /* Free title information. */
        if (!this->is_system)
        {
            titleFreeUserApplicationData(&(this->user_app_data));
        } else {
            titleFreeTitleInfo(&(this->system_title_info));
        }
    }

    TitlesTabItem::TitlesTabItem(const TitleApplicationMetadata *app_metadata, bool is_system, bool click_anim) : brls::ListItem(std::string(app_metadata->name), "", ""), \
                                                                                                                  app_metadata(app_metadata), \
                                                                                                                  is_system(is_system), \
                                                                                                                  click_anim(click_anim)
    {
        /* Set sublabel. */
        if (!this->is_system) this->setSubLabel(std::string(app_metadata->publisher));

        /* Set thumbnail (if needed). */
        if (app_metadata->icon_data && app_metadata->icon_size) this->setThumbnail(static_cast<u8*>(app_metadata->icon_data), app_metadata->icon_size);

        /* Set value. */
        this->setValue(fmt::format("{:016X}", this->app_metadata->title_id), false, false);
    }

    void TitlesTabItem::playClickAnimation(void)
    {
        if (this->click_anim) brls::View::playClickAnimation();
    }

    TitlesTab::TitlesTab(RootView *root_view, bool is_system) : LayeredErrorFrame("titles_tab/no_titles_available"_i18n), root_view(root_view), is_system(is_system)
    {
        /* Populate list. */
        this->PopulateList(this->root_view->GetApplicationMetadataInfo(this->is_system));

        /* Subscribe to the title event if this is the user titles tab. */
        if (!this->is_system)
        {
            this->title_task_sub = this->root_view->RegisterTitleMetadataTaskListener([this](const nxdt::tasks::TitleApplicationMetadataInfo& app_metadata_info) {
                /* Update list. */
                this->PopulateList(app_metadata_info);
            });
        }
    }

    TitlesTab::~TitlesTab()
    {
        /* Unregister task listener if this is the user titles tab. */
        if (!this->is_system) this->root_view->UnregisterTitleMetadataTaskListener(this->title_task_sub);
    }

    void TitlesTab::PopulateList(const nxdt::tasks::TitleApplicationMetadataInfo& app_metadata_info)
    {
        /* Block user inputs. */
        brls::Application::blockInputs();

        /* Populate variables. */
        TitleApplicationMetadata **app_metadata = app_metadata_info.app_metadata;
        const u32 app_metadata_count = app_metadata_info.app_metadata_count;

        bool update_focused_view = this->IsListItemFocused();
        int focus_stack_index = this->GetFocusStackViewIndex();

        /* If needed, switch to the error frame *before* cleaning up our list. */
        if (!app_metadata_count) this->SwitchLayerView(true);

        /* Clear list. */
        this->list->clear();
        this->list->invalidate(true);

        /* Return immediately if we have no application metadata. */
        if (!app_metadata_count)
        {
            brls::Application::unblockInputs();
            return;
        }

        /* Populate list. */
        for(u32 i = 0; i < app_metadata_count; i++)
        {
            /* Create list item. */
            TitlesTabItem *item = new TitlesTabItem(app_metadata[i], this->is_system);

            /* Register click event. */
            item->getClickEvent()->subscribe([](brls::View *view) {
                TitlesTabItem *item = static_cast<TitlesTabItem*>(view);
                const TitleApplicationMetadata *item_app_metadata = item->GetApplicationMetadata();
                bool is_system = item->IsSystemTitle();

                TitlesTabResetLogFile();

                /* Log selected title information instead of showing the placeholder RGB popup. */
                TitlesTabAppendToLogFile("Selected %s title: \"%s\" (%016lX).",
                                         (is_system ? "system" : "user"), (item_app_metadata->name ? item_app_metadata->name : ""), item_app_metadata->title_id);
                TitlesTabAppendToLogFile("Metadata: publisher \"%s\", language %u, icon_size 0x%lX.",
                                         (item_app_metadata->publisher ? item_app_metadata->publisher : ""), item_app_metadata->language, item_app_metadata->icon_size);

                if (is_system)
                {
                    TitleInfo *system_title_info = titleGetTitleInfoEntryFromStorageByTitleId(NcmStorageId_BuiltInSystem, item_app_metadata->title_id);
                    if (!system_title_info)
                    {
                        TitlesTabAppendToLogFile("Unable to retrieve system title info for %016lX.", item_app_metadata->title_id);
                        return;
                    }

                    TitlesTabLogInfoEntry("System info", system_title_info);
                    TitlesTabLogWalkState walk_state = {0};
                    walk_state.max_entries = 20000;
                    TitlesTabLogInfoBlockTree("system", system_title_info, &walk_state);
                    if (walk_state.truncated) TitlesTabAppendToLogFile("Filesystem listing truncated after %u entries.", walk_state.max_entries);
                    titleFreeTitleInfo(&system_title_info);
                } else {
                    TitleUserApplicationData user_app_data{};
                    if (!titleGetUserApplicationData(item_app_metadata->title_id, &user_app_data))
                    {
                        TitlesTabAppendToLogFile("Unable to retrieve user title info for %016lX.", item_app_metadata->title_id);
                        return;
                    }

                    TitlesTabAppendToLogFile("User title linked entry counts: app %u, patch %u, dlc %u, dlc_patch %u.",
                                             TitlesTabCountInfoEntries(user_app_data.app_info), TitlesTabCountInfoEntries(user_app_data.patch_info),
                                             TitlesTabCountInfoEntries(user_app_data.aoc_info), TitlesTabCountInfoEntries(user_app_data.aoc_patch_info));

                    TitlesTabLogInfoEntry("App info", user_app_data.app_info);
                    TitlesTabLogInfoEntry("Patch info", user_app_data.patch_info);
                    TitlesTabLogInfoEntry("DLC info", user_app_data.aoc_info);
                    TitlesTabLogInfoEntry("DLC patch info", user_app_data.aoc_patch_info);

                    TitleExtractResult extract_result = {0};
                    bool extract_ok = titleExtractMainAndGlobalMetadata(item_app_metadata->title_id, DEVOPTAB_SDMC_DEVICE APP_BASE_PATH "extracted", &extract_result);
                    TitlesTabAppendToLogFile("main extraction: %s (%s)", extract_result.main_extracted ? "OK" : "FAILED", extract_result.main_path);
                    TitlesTabAppendToLogFile("global-metadata.dat extraction: %s (%s)", extract_result.metadata_extracted ? "OK" : "FAILED", extract_result.metadata_path);
                    if (!extract_ok) TitlesTabAppendToLogFile("Extraction finished with missing output(s).");

                    TitlesTabLogWalkState walk_state = {0};
                    walk_state.max_entries = 0;
                    TitlesTabLogInfoBlockTree("app", user_app_data.app_info, &walk_state);
                    TitlesTabLogInfoBlockTree("patch", user_app_data.patch_info, &walk_state);
                    TitlesTabLogInfoBlockTree("dlc", user_app_data.aoc_info, &walk_state);
                    TitlesTabLogInfoBlockTree("dlc_patch", user_app_data.aoc_patch_info, &walk_state);

                    titleFreeUserApplicationData(&user_app_data);
                }
            });

            /* Add list item to our view. */
            this->list->addView(item);
        }

        /* Update focus stack, if needed. */
        if (focus_stack_index > -1) this->UpdateFocusStackViewAtIndex(focus_stack_index, this->GetListFirstFocusableChild());

        /* Switch to the list. */
        this->list->invalidate(true);
        this->SwitchLayerView(false, update_focused_view, focus_stack_index < 0);

        /* Unblock user inputs. */
        brls::Application::unblockInputs();
    }
}
