/*
 * main.cpp
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
#include <core/nxdt_log.h>
#include <core/title.h>
#include <core/title_extract.h>
#include <utils/scope_guard.hpp>
#include <views/root_view.hpp>

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

bool g_borealisInitialized = false;
static bool g_breezeConsoleInitialized = false;

static void breezeRenderStatusScreen(const char *status, u64 title_id, const TitleExtractResult *extract_result, bool finished)
{
    if (!g_breezeConsoleInitialized) return;

    consoleClear();

    printf("nxdumptool - Breeze Helper\n\n");
    if (title_id) printf("Target title: %016lX\n\n", title_id);
    if (status && *status) printf("%s\n\n", status);

    if (extract_result)
    {
        printf("main: %s\n", extract_result->main_extracted ? "OK" : "FAILED");
        printf("global-metadata.dat: %s\n", extract_result->metadata_extracted ? "OK" : "FAILED");
    }

    if (finished) printf("\nReturning to Breeze...");

    consoleUpdate(NULL);
}

static bool parseBreezeTitleIdFromConfig(u64 *out_title_id)
{
    if (!out_title_id)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    FILE *fp = fopen("sdmc:/switch/breeze/config.ini", "rb");
    if (!fp)
    {
        LOG_MSG_ERROR("Failed to open Breeze config file.");
        return false;
    }

    bool success = false;
    char line[512] = {0};

    while(fgets(line, sizeof(line), fp))
    {
        utilsTrimString(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;

        /* Support INI-style "key = value", with optional inline comments. */
        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        utilsTrimString(key);
        utilsTrimString(value);

        if (strcmp(key, "save_application_id") != 0) continue;

        /* Strip inline comments. */
        char *comment = strpbrk(value, "#;");
        if (comment)
        {
            *comment = '\0';
            utilsTrimString(value);
        }

        if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) value += 2;

        size_t value_len = strlen(value);
        if (!value_len || value_len > 16)
        {
            LOG_MSG_ERROR("Invalid save_application_id length in Breeze config.");
            break;
        }

        for(size_t i = 0; i < value_len; i++)
        {
            if (!isxdigit((unsigned char)value[i]))
            {
                LOG_MSG_ERROR("Invalid save_application_id value in Breeze config.");
                goto end;
            }
        }

        *out_title_id = strtoull(value, NULL, 16);
        success = (*out_title_id != 0);
        if (success) LOG_MSG_INFO("Parsed Breeze target title ID: %016lX.", *out_title_id);
        break;
    }

end:
    fclose(fp);

    if (!success) LOG_MSG_ERROR("save_application_id not found or invalid in Breeze config.");
    return success;
}

static void runBreezeExtractionAndReturn(void)
{
    u64 app_title_id = 0;
    TitleExtractResult extract_result = {0};

    breezeRenderStatusScreen("Reading Breeze config...", 0, NULL, false);

    if (parseBreezeTitleIdFromConfig(&app_title_id))
    {
        breezeRenderStatusScreen("Extracting files...", app_title_id, NULL, false);

        bool ok = titleExtractMainAndGlobalMetadata(app_title_id, "sdmc:/switch/breeze/cheats", &extract_result);

        breezeRenderStatusScreen(ok ? "Extraction completed successfully." : "Extraction completed with errors.", app_title_id, &extract_result, true);

        LOG_MSG_DEBUG("Breeze extract target title: %016lX.", app_title_id);
        LOG_MSG_DEBUG("main extraction: %s (%s).", extract_result.main_extracted ? "OK" : "FAILED", extract_result.main_path);
        LOG_MSG_DEBUG("global-metadata.dat extraction: %s (%s).", extract_result.metadata_extracted ? "OK" : "FAILED", extract_result.metadata_path);
        LOG_MSG_DEBUG("Breeze extract result: %s.", ok ? "SUCCESS" : "PARTIAL/FAILED");
    } else {
        breezeRenderStatusScreen("Failed to read Breeze config.", 0, NULL, true);
    }

    /* Flush pending SD filesystem changes before handing control back. */
    utilsCommitSdCardFileSystemChanges();

    envSetNextLoad("sdmc:/switch/breeze/Breeze.nro", "sdmc:/switch/breeze/Breeze.nro");
}

int main(int argc, char *argv[])
{
    NX_IGNORE_ARG(argc);
    NX_IGNORE_ARG(argv);

    /* Set scope guard to clean up resources at exit. */
    ON_SCOPE_EXIT { utilsCloseResources(); };

    consoleInit(NULL);
    g_breezeConsoleInitialized = true;
    ON_SCOPE_EXIT {
        if (g_breezeConsoleInitialized)
        {
            consoleExit(NULL);
            g_breezeConsoleInitialized = false;
        }
    };

    /* Extraction helper mode doesn't need full title metadata/UI preparation. */
    titleSetFastInitialization(true);
    utilsSetGameCardInitialization(false);
    utilsSetBfttfInitialization(false);
    utilsSetSystemUpdateInitialization(false);
    utilsSetBisStorageSystemPartitionOnly(true);
    logSetNxLinkOutputEnabled(false);

    breezeRenderStatusScreen("Initializing...", 0, NULL, false);

    /* Initialize application resources. */
    if (!utilsInitializeResources()) return EXIT_FAILURE;

    /* Breeze helper mode: extract target files and chainload back to Breeze. */
    runBreezeExtractionAndReturn();
    return EXIT_SUCCESS;

    /* Load Borealis translation files. */
    brls::i18n::loadTranslations();

    /* Set common footer. */
    brls::Application::setCommonFooter("v" APP_VERSION " (" GIT_REV ")");

    /* Initialize Borealis. */
    if (!brls::Application::init(APP_TITLE)) return EXIT_FAILURE;
    g_borealisInitialized = true;

    try {
        /* Check if we're running under applet mode. */
        if (utilsIsAppletMode())
        {
            /* Push crash frame with the applet mode warning. */
            brls::Application::pushView(new brls::CrashFrame("generic/applet_mode_warning"_i18n, [](brls::View *view) {
                /* Swap crash frame with root view whenever the crash frame button is clicked. */
                //brls::Application::swapView(new nxdt::views::RootView());
                /* TODO: restore original behavior after fixing the applet mode issues. */
                brls::Application::quit();
            }));
        } else {
            /* Push root view. */
            brls::Application::pushView(new nxdt::views::RootView());
        }

        /* Run the application. */
        while(brls::Application::mainLoop());
    } catch (...) {
        std::exception_ptr p = std::current_exception();
        LOG_MSG_ERROR("Exception caught! (%s).", p ? p.__cxa_exception_type()->name() : "unknown");
        brls::Application::crash(i18n::getStr("generic/exception_caught", p ? p.__cxa_exception_type()->name() : "generic/unknown_exception"_i18n));
        while(brls::Application::mainLoop());
    }

    /* Exit. */
    return EXIT_SUCCESS;
}
