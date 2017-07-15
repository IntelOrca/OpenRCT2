#pragma region Copyright (c) 2014-2017 OpenRCT2 Developers
/*****************************************************************************
* OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
*
* OpenRCT2 is the work of many authors, a full list can be found in contributors.md
* For more information, visit https://github.com/OpenRCT2/OpenRCT2
*
* OpenRCT2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* A full copy of the GNU General Public License can be found in licence.txt
*****************************************************************************/
#pragma endregion

#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include "audio/AudioContext.h"
#include "Context.h"
#include "ui/UiContext.h"
#include "core/Console.hpp"
#include "core/File.h"
#include "core/FileScanner.h"
#include "core/FileStream.hpp"
#include "core/Guard.hpp"
#include "core/Math.hpp"
#include "core/MemoryStream.h"
#include "core/Path.hpp"
#include "core/String.hpp"
#include "FileClassifier.h"
#include "scripting/Scripting.h"
#include "network/network.h"
#include "object/ObjectManager.h"
#include "object/ObjectRepository.h"
#include "OpenRCT2.h"
#include "ParkImporter.h"
#include "platform/crash.h"
#include "PlatformEnvironment.h"
#include "ride/TrackDesignRepository.h"
#include "scenario/ScenarioRepository.h"
#include "title/TitleScreen.h"
#include "title/TitleSequenceManager.h"
#include "ui/WindowManager.h"
#include "Version.h"

extern "C"
{
    #include "audio/audio.h"
    #include "config/Config.h"
    #include "drawing/lightfx.h"
    #include "editor.h"
    #include "game.h"
    #include "input.h"
    #include "interface/chat.h"
    #include "interface/console.h"
    #include "interface/themes.h"
    #include "intro.h"
    #include "localisation/localisation.h"
    #include "network/http.h"
    #include "network/network.h"
    #include "network/twitch.h"
    #include "object_list.h"
    #include "platform/platform.h"
    #include "rct1.h"
    #include "rct2.h"
    #include "rct2/interop.h"
    #include "util/util.h"
}

#pragma warning(push, 0)
#include "thirdparty/linenoise.hpp"
#pragma warning(pop)

using namespace OpenRCT2;
using namespace OpenRCT2::Audio;
using namespace OpenRCT2::Scripting;
using namespace OpenRCT2::Ui;

namespace OpenRCT2
{
    class Context : public IContext
    {
    private:
        // Dependencies
        IPlatformEnvironment * const    _env = nullptr;
        IAudioContext * const           _audioContext = nullptr;
        IUiContext * const              _uiContext = nullptr;

        // Services
        IObjectRepository *         _objectRepository       = nullptr;
        IObjectManager *            _objectManager          = nullptr;
        ITrackDesignRepository *    _trackDesignRepository  = nullptr;
        IScenarioRepository *       _scenarioRepository     = nullptr;
        IScriptEngine *             _scriptEngine           = nullptr;

        bool    _initialised = false;
        bool    _isWindowMinimised = false;
        uint32  _lastTick = 0;
        uint32  _accumulator = 0;
        uint32  _lastUpdateTick = 0;

        /** If set, will end the OpenRCT2 game loop. Intentially private to this module so that the flag can not be set back to false. */
        bool _finished = false;

    public:
        // Singleton of Context.
        // Remove this when GetContext() is no longer called so that
        // multiple instances can be created in parallel
        static Context * Instance;

    public:
        Context(IPlatformEnvironment * env, IAudioContext * audioContext, IUiContext * uiContext)
            : _env(env),
              _audioContext(audioContext),
              _uiContext(uiContext)
        {
            Instance = this;
        }

        ~Context() override
        {
            network_close();
            http_dispose();
            language_close_all();
            object_manager_unload_all_objects();
            gfx_object_check_all_images_freed();
            gfx_unload_g2();
            gfx_unload_g1();
            config_release();
#ifndef DISABLE_NETWORK
            EVP_MD_CTX_destroy(gHashCTX);
#endif // DISABLE_NETWORK
            rct2_interop_dispose();

            delete _scenarioRepository;
            delete _trackDesignRepository;
            delete _objectManager;
            delete _objectRepository;

            Instance = nullptr;
        }

        IAudioContext * GetAudioContext() override
        {
            return _audioContext;
        }

        IUiContext * GetUiContext() override
        {
            return _uiContext;
        }

        IScriptEngine * GetScriptEngine() override
        {
            return _scriptEngine;
        }

        sint32 RunOpenRCT2(int argc, char * * argv) override
        {
            if (Initialise())
            {
                Launch();
            }
            return gExitCode;
        }

        /**
         * Causes the OpenRCT2 game loop to finish.
         */
        void Finish() override
        {
            _finished = true;
        }

        bool Initialise() final override
        {
            if (_initialised)
            {
                throw std::runtime_error("Context already initialised.");
            }
            _initialised = true;

#ifndef DISABLE_NETWORK
            gHashCTX = EVP_MD_CTX_create();
            Guard::Assert(gHashCTX != nullptr, "EVP_MD_CTX_create failed");
#endif // DISABLE_NETWORK

            crash_init();

            if (!rct2_interop_setup_segment())
            {
                log_fatal("Unable to load RCT2 data sector");
                return false;
            }

            if (gConfigGeneral.last_run_version != nullptr && String::Equals(gConfigGeneral.last_run_version, OPENRCT2_VERSION))
            {
                gOpenRCT2ShowChangelog = false;
            }
            else
            {
                gOpenRCT2ShowChangelog = true;
                gConfigGeneral.last_run_version = String::Duplicate(OPENRCT2_VERSION);
                config_save_default();
            }

            if (!rct2_init_directories())
            {
                return false;
            }
            _env->SetBasePath(DIRBASE::RCT2, gRCT2AddressAppPath);

            if (!gOpenRCT2Headless)
            {
                _uiContext->CreateWindow();
            }

            // TODO add configuration option to allow multiple instances
            // if (!gOpenRCT2Headless && !platform_lock_single_instance()) {
            //  log_fatal("OpenRCT2 is already running.");
            //  return false;
            // }

            _objectRepository = CreateObjectRepository(_env);
            _objectManager = CreateObjectManager(_objectRepository);
            _trackDesignRepository = CreateTrackDesignRepository(_env);
            _scenarioRepository = CreateScenarioRepository(_env);

            if (!language_open(gConfigGeneral.language))
            {
                log_error("Failed to open configured language...");
                if (!language_open(LANGUAGE_ENGLISH_UK))
                {
                    log_fatal("Failed to open fallback language...");
                    return false;
                }
            }

            // TODO Ideally we want to delay this until we show the title so that we can
            //      still open the game window and draw a progress screen for the creation
            //      of the object cache.
            _objectRepository->LoadOrConstruct();

            // TODO Like objects, this can take a while if there are a lot of track designs
            //      its also really something really we might want to do in the background
            //      as its not required until the player wants to place a new ride.
            _trackDesignRepository->Scan();

            _scenarioRepository->Scan();
            TitleSequenceManager::Scan();

            if (!gOpenRCT2Headless)
            {
                audio_init();
                audio_populate_devices();
                audio_init_ride_sounds_and_info();
            }

            http_init();
            network_set_env(_env);
            chat_init();
            theme_manager_initialise();
            CopyOriginalUserFilesOver();

            rct2_interop_setup_hooks();

            if (!gOpenRCT2NoGraphics)
            {
                LoadBaseGraphics();
#ifdef __ENABLE_LIGHTFX__
                lightfx_init();
#endif
            }
            gScenarioTicks = 0;
            util_srand((uint32)time(0));
            input_reset_place_obj_modifier();
            viewport_init_all();
            game_init_all(150);
            _scriptEngine = CreateScriptEngine(_env);
            return true;
        }

        void Open(const std::string &path) final override
        {
            auto fs = FileStream(path, FILE_MODE_OPEN);
            OpenParkAutoDetectFormat(&fs, path);
        }

    private:
        bool LoadBaseGraphics()
        {
            if (!gfx_load_g1(_env))
            {
                return false;
            }
            if (!gfx_load_g2())
            {
                return false;
            }
            gfx_load_csg();
            font_sprite_initialise_characters();
            return true;
        }

        /**
         * Launches the game, after command line arguments have been parsed and processed.
         */
        void Launch()
        {
            gIntroState = INTRO_STATE_NONE;
            if ((gOpenRCT2StartupAction == STARTUP_ACTION_TITLE) && gConfigGeneral.play_intro)
            {
                gOpenRCT2StartupAction = STARTUP_ACTION_INTRO;
            }

            switch (gOpenRCT2StartupAction) {
            case STARTUP_ACTION_INTRO:
                gIntroState = INTRO_STATE_PUBLISHER_BEGIN;
                title_load();
                break;
            case STARTUP_ACTION_TITLE:
                title_load();
                break;
            case STARTUP_ACTION_OPEN:
            {
                // A path that includes "://" is illegal with all common filesystems, so it is almost certainly a URL
                // This way all cURL supported protocols, like http, ftp, scp and smb are automatically handled
                if (strstr(gOpenRCT2StartupActionPath, "://") != nullptr)
                {
#ifndef DISABLE_HTTP
                    // Download park and open it using its temporary filename
                    void * data;
                    size_t dataSize = http_download_park(gOpenRCT2StartupActionPath, &data);
                    if (dataSize == 0)
                    {
                        title_load();
                        break;
                    }

                    auto ms = MemoryStream(data, dataSize, MEMORY_ACCESS::OWNER);
                    if (!OpenParkAutoDetectFormat(&ms, gOpenRCT2StartupActionPath))
                    {
                        Console::Error::WriteLine("Failed to load '%s'", gOpenRCT2StartupActionPath);
                        title_load();
                        break;
                    }
#endif
                }
                else
                {
                    try
                    {
                        Open(gOpenRCT2StartupActionPath);
                    }
                    catch (const std::exception &ex)
                    {
                        Console::Error::WriteLine("Failed to load '%s'", gOpenRCT2StartupActionPath);
                        Console::Error::WriteLine("%s", ex.what());
                        title_load();
                        break;
                    }
                }

                gScreenFlags = SCREEN_FLAGS_PLAYING;

#ifndef DISABLE_NETWORK
                if (gNetworkStart == NETWORK_MODE_SERVER)
                {
                    if (gNetworkStartPort == 0)
                    {
                        gNetworkStartPort = gConfigNetwork.default_port;
                    }

                    if (String::IsNullOrEmpty(gNetworkStartAddress))
                    {
                        gNetworkStartAddress = gConfigNetwork.listen_address;
                    }

                    if (String::IsNullOrEmpty(gCustomPassword))
                    {
                        network_set_password(gConfigNetwork.default_password);
                    }
                    else
                    {
                        network_set_password(gCustomPassword);
                    }
                    network_begin_server(gNetworkStartPort, gNetworkStartAddress);
                }
#endif // DISABLE_NETWORK
                break;
            }
            case STARTUP_ACTION_EDIT:
                if (String::SizeOf(gOpenRCT2StartupActionPath) == 0)
                {
                    editor_load();
                }
                else if (!editor_load_landscape(gOpenRCT2StartupActionPath))
                {
                    title_load();
                }
                break;
            }

#ifndef DISABLE_NETWORK
            if (gNetworkStart == NETWORK_MODE_CLIENT)
            {
                if (gNetworkStartPort == 0)
                {
                    gNetworkStartPort = gConfigNetwork.default_port;
                }
                network_begin_client(gNetworkStartHost, gNetworkStartPort);
            }
#endif // DISABLE_NETWORK

            RunREPL();
            RunGameLoop();
        }

        void RunREPL()
        {
            std::thread replThread ([this]() -> void
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                linenoise::SetMultiLine(true);
                linenoise::SetHistoryMaxLen(32);

                std::string prompt = "\033[32mopenrct2 $\x1b[0m ";
                std::string current;
                bool more = false;
                while (true)
                {
                    std::string line;
                    std::string left = prompt;
                    if (more) left = "\033[32m>\x1b[0m ";
                    auto quit = linenoise::Readline(left.c_str(), line);
                    if (quit) {
                        _finished = true;
                        break;
                    }

                    current += line;

                    int indent = 0;
                    for (char c : current)
                    {
                        if (c == '{') indent++;
                        else if (c == '}') indent--;
                    }

                    if (indent == 0)
                    {
                        linenoise::AddHistory(current.c_str());
                        _scriptEngine->ConsoleEval(current);
                        current = "";
                        more = false;
                    }
                    else
                    {
                        current += "\n";
                        more = true;
                    }
                }
            });
            replThread.detach();
        }

        bool ShouldRunVariableFrame()
        {
            if (!gConfigGeneral.uncap_fps) return false;
            if (gGameSpeed > 4) return false;
            if (gOpenRCT2Headless) return false;
            if (_uiContext->IsMinimised()) return false;
            return true;
        }

        /**
        * Run the main game loop until the finished flag is set.
        */
        void RunGameLoop()
        {
            log_verbose("begin openrct2 loop");
            _finished = false;

            bool variableFrame = ShouldRunVariableFrame();
            bool useVariableFrame;

            do
            {
                useVariableFrame = ShouldRunVariableFrame();
                // Make sure we catch the state change and reset it.
                if (variableFrame != useVariableFrame)
                {
                    _lastTick = 0;
                    variableFrame = useVariableFrame;
                }

                if (useVariableFrame)
                {
                    RunVariableFrame();
                }
                else
                {
                    RunFixedFrame();
                }
            } while (!_finished);
            log_verbose("finish openrct2 loop");
        }

        void RunFixedFrame()
        {
            uint32 currentTick = platform_get_ticks();

            if (_lastTick == 0)
            {
                _lastTick = currentTick;
            }

            uint32 elapsed = currentTick - _lastTick;
            _lastTick = currentTick;
            _accumulator = Math::Min(_accumulator + elapsed, (uint32)GAME_UPDATE_MAX_THRESHOLD);

            _uiContext->ProcessMessages();

            if (_accumulator < GAME_UPDATE_TIME_MS)
            {
                platform_sleep(GAME_UPDATE_TIME_MS - _accumulator - 1);
                return;
            }

            _accumulator -= GAME_UPDATE_TIME_MS;

            Update();
            if (!_isWindowMinimised && !gOpenRCT2Headless)
            {
                drawing_engine_draw();
            }
        }

        void RunVariableFrame()
        {
            uint32 currentTick = platform_get_ticks();

            bool draw = !_isWindowMinimised && !gOpenRCT2Headless;

            if (_lastTick == 0)
            {
                sprite_position_tween_reset();
                _lastTick = currentTick;
            }

            uint32 elapsed = currentTick - _lastTick;

            _lastTick = currentTick;
            _accumulator = Math::Min(_accumulator + elapsed, (uint32)GAME_UPDATE_MAX_THRESHOLD);

            _uiContext->ProcessMessages();

            while (_accumulator >= GAME_UPDATE_TIME_MS)
            {
                // Get the original position of each sprite
                if(draw)
                    sprite_position_tween_store_a();

                Update();

                _accumulator -= GAME_UPDATE_TIME_MS;

                // Get the next position of each sprite
                if(draw)
                    sprite_position_tween_store_b();
            }

            if (draw)
            {
                const float alpha = (float)_accumulator / GAME_UPDATE_TIME_MS;
                sprite_position_tween_all(alpha);

                drawing_engine_draw();

                sprite_position_tween_restore();
            }
        }

        void Update()
        {
            uint32 currentUpdateTick = platform_get_ticks();
            gTicksSinceLastUpdate = std::min<uint32>(currentUpdateTick - _lastUpdateTick, 500);
            _lastUpdateTick = currentUpdateTick;
            
            if (game_is_not_paused())
            {
                gPaletteEffectFrame += gTicksSinceLastUpdate;
            }

            date_update_real_time_of_day();

            if (gIntroState != INTRO_STATE_NONE)
            {
                intro_update();
            }
            else if ((gScreenFlags & SCREEN_FLAGS_TITLE_DEMO) && !gOpenRCT2Headless)
            {
                title_update();
            }
            else
            {
                game_update();
            }

            twitch_update();
            chat_update();
            console_update();
            script_engine_update();
        }

        bool OpenParkAutoDetectFormat(IStream * stream, const std::string &path)
        {
            ClassifiedFile info;
            if (TryClassifyFile(stream, &info))
            {
                if (info.Type == FILE_TYPE::SAVED_GAME ||
                    info.Type == FILE_TYPE::SCENARIO)
                {
                    std::unique_ptr<IParkImporter> parkImporter;
                    if (info.Version <= 2)
                    {
                        parkImporter.reset(ParkImporter::CreateS4());
                    }
                    else
                    {
                        parkImporter.reset(ParkImporter::CreateS6(_objectRepository, _objectManager));
                    }

                    auto result = parkImporter->LoadFromStream(stream, false);
                    if (result.Error == PARK_LOAD_ERROR_OK)
                    {
                        parkImporter->Import();
                        game_fix_save_vars();
                        sprite_position_tween_reset();
                        gScreenAge = 0;
                        gLastAutoSaveUpdate = AUTOSAVE_PAUSE;
                        if (info.Type == FILE_TYPE::SAVED_GAME)
                        {
                            game_load_init();
                        }
                        else
                        {
                            scenario_begin();
                        }
                        return true;
                    }
                    else
                    {
                        handle_park_load_failure_with_title_opt(&result, path.c_str(), true);
                    }
                }
                else
                {
                    Console::Error::WriteLine("Invalid file type.");
                }
            }
            else
            {
                Console::Error::WriteLine("Unable to detect file type.");
            }
            return false;
        }

        /**
        * Copy saved games and landscapes to user directory
        */
        void CopyOriginalUserFilesOver()
        {
            CopyOriginalUserFilesOver(DIRID::SAVE, "*.sv6");
            CopyOriginalUserFilesOver(DIRID::LANDSCAPE, "*.sc6");
        }

        void CopyOriginalUserFilesOver(DIRID dirid, const std::string &pattern)
        {
            auto src = _env->GetDirectoryPath(DIRBASE::RCT2, dirid);
            auto dst = _env->GetDirectoryPath(DIRBASE::USER, dirid);
            CopyOriginalUserFilesOver(src, dst, pattern);
        }

        void CopyOriginalUserFilesOver(const std::string &srcRoot, const std::string &dstRoot, const std::string &pattern)
        {
            log_verbose("CopyOriginalUserFilesOver('%s', '%s', '%s')", srcRoot.c_str(), dstRoot.c_str(), pattern.c_str());

            auto scanPattern = Path::Combine(srcRoot, pattern);
            auto scanner = Path::ScanDirectory(scanPattern, true);
            while (scanner->Next())
            {
                auto src = std::string(scanner->GetPath());
                auto dst = Path::Combine(dstRoot, scanner->GetPathRelative());
                auto dstDirectory = Path::GetDirectory(dst);

                // Create the directory if necessary
                if (!platform_directory_exists(dstDirectory.c_str()))
                {
                    Console::WriteLine("Creating directory '%s'", dstDirectory.c_str());
                    if (!platform_ensure_directory_exists(dstDirectory.c_str()))
                    {
                        Console::Error::WriteLine("Could not create directory %s.", dstDirectory.c_str());
                        break;
                    }
                }

                // Only copy the file if it doesn't already exist
                if (!File::Exists(dst))
                {
                    Console::WriteLine("Copying '%s' to '%s'", src.c_str(), dst.c_str());
                    if (!File::Copy(src, dst, false))
                    {
                        Console::Error::WriteLine("Failed to copy '%s' to '%s'", src.c_str(), dst.c_str());
                    }
                }
            }
            delete scanner;
        }
    };

    class PlainContext final : public Context
    {
        std::unique_ptr<IPlatformEnvironment>   _env;
        std::unique_ptr<IAudioContext>          _audioContext;
        std::unique_ptr<IUiContext>             _uiContext;

    public:
        PlainContext()
            : PlainContext(CreatePlatformEnvironment(), CreateDummyAudioContext(), CreateDummyUiContext())
        {
        }

        PlainContext(IPlatformEnvironment * env, IAudioContext * audioContext, IUiContext * uiContext)
            : Context(env, audioContext, uiContext)
        {
            _env = std::unique_ptr<IPlatformEnvironment>(env);
            _audioContext = std::unique_ptr<IAudioContext>(audioContext);
            _uiContext = std::unique_ptr<IUiContext>(uiContext);
        }
    };

    Context * Context::Instance = nullptr;

    IContext * CreateContext()
    {
        return new PlainContext();
    }

    IContext * CreateContext(IPlatformEnvironment * env, Audio::IAudioContext * audioContext, IUiContext * uiContext)
    {
        return new Context(env, audioContext, uiContext);
    }

    IContext * GetContext()
    {
        return Context::Instance;
    }
}

extern "C"
{
    void openrct2_write_full_version_info(utf8 * buffer, size_t bufferSize)
    {
        String::Set(buffer, bufferSize, gVersionInfoFull);
    }

    void openrct2_finish()
    {
        GetContext()->Finish();
    }

    void context_setcurrentcursor(sint32 cursor)
    {
        GetContext()->GetUiContext()->SetCursor((CURSOR_ID)cursor);
    }

    void context_hide_cursor()
    {
        GetContext()->GetUiContext()->SetCursorVisible(false);
    }

    void context_show_cursor()
    {
        GetContext()->GetUiContext()->SetCursorVisible(true);
    }

    void context_get_cursor_position(sint32 * x, sint32 * y)
    {
        GetContext()->GetUiContext()->GetCursorPosition(x, y);
    }

    void context_get_cursor_position_scaled(sint32 * x, sint32 * y)
    {
        context_get_cursor_position(x, y);

        // Compensate for window scaling.
        *x = (sint32)ceilf(*x / gConfigGeneral.window_scale);
        *y = (sint32)ceilf(*y / gConfigGeneral.window_scale);
    }

    void context_set_cursor_position(sint32 x, sint32 y)
    {
        GetContext()->GetUiContext()->SetCursorPosition(x, y);
    }

    const CursorState * context_get_cursor_state()
    {
        return GetContext()->GetUiContext()->GetCursorState();
    }

    const uint8 * context_get_keys_state()
    {
        return GetContext()->GetUiContext()->GetKeysState();
    }

    const uint8 * context_get_keys_pressed()
    {
        return GetContext()->GetUiContext()->GetKeysPressed();
    }

    TextInputSession * context_start_text_input(utf8 * buffer, size_t maxLength)
    {
        return GetContext()->GetUiContext()->StartTextInput(buffer, maxLength);
    }

    void context_stop_text_input()
    {
        GetContext()->GetUiContext()->StopTextInput();
    }

    bool context_is_input_active()
    {
        return GetContext()->GetUiContext()->IsTextInputActive();
    }

    void context_trigger_resize()
    {
        return GetContext()->GetUiContext()->TriggerResize();
    }

    void context_set_fullscreen_mode(sint32 mode)
    {
        return GetContext()->GetUiContext()->SetFullscreenMode((FULLSCREEN_MODE)mode);
    }

    void context_recreate_window()
    {
        GetContext()->GetUiContext()->RecreateWindow();
    }

    sint32 context_get_resolutions(Resolution * * outResolutions)
    {
        auto resolutions = GetContext()->GetUiContext()->GetFullscreenResolutions();
        sint32 count = (sint32)resolutions.size();
        *outResolutions = Memory::AllocateArray<Resolution>(count);
        Memory::CopyArray(*outResolutions, resolutions.data(), count);
        return count;
    }

    sint32 context_get_width()
    {
        return GetContext()->GetUiContext()->GetWidth();
    }

    sint32 context_get_height()
    {
        return GetContext()->GetUiContext()->GetHeight();
    }

    bool context_has_focus()
    {
        return GetContext()->GetUiContext()->HasFocus();
    }

    void context_set_cursor_trap(bool value)
    {
        GetContext()->GetUiContext()->SetCursorTrap(value);
    }

    rct_window * context_open_window(rct_windowclass wc)
    {
        auto windowManager = GetContext()->GetUiContext()->GetWindowManager();
        return windowManager->OpenWindow(wc);
    }

    void context_input_handle_keyboard(bool isTitle)
    {
        auto windowManager = GetContext()->GetUiContext()->GetWindowManager();
        windowManager->HandleKeyboard(isTitle);
    }

    bool context_read_bmp(void * * outPixels, uint32 * outWidth, uint32 * outHeight, const utf8 * path)
    {
        return GetContext()->GetUiContext()->ReadBMP(outPixels, outWidth, outHeight, std::string(path));
    }

    bool platform_open_common_file_dialog(utf8 * outFilename, file_dialog_desc * desc, size_t outSize)
    {
        try
        {
            FileDialogDesc desc2;
            desc2.Type = (FILE_DIALOG_TYPE)desc->type;
            desc2.Title = String::ToStd(desc->title);
            desc2.InitialDirectory = String::ToStd(desc->initial_directory);
            desc2.DefaultFilename = String::ToStd(desc->default_filename);
            for (const auto &filter : desc->filters)
            {
                if (filter.name != nullptr)
                {
                    desc2.Filters.push_back({ String::ToStd(filter.name), String::ToStd(filter.pattern) });
                }
            }
            std::string result = GetContext()->GetUiContext()->ShowFileDialog(desc2);
            String::Set(outFilename, outSize, result.c_str());
            return !result.empty();
        }
        catch (const std::exception &ex)
        {
            log_error(ex.what());
            outFilename[0] = '\0';
            return false;
        }
    }

    utf8 * platform_open_directory_browser(const utf8 * title)
    {
        try
        {
            std::string result = GetContext()->GetUiContext()->ShowDirectoryDialog(title);
            return String::Duplicate(result.c_str());
        }
        catch (const std::exception &ex)
        {
            log_error(ex.what());
            return nullptr;
        }
    }

    bool platform_place_string_on_clipboard(utf8* target)
    {
        return GetContext()->GetUiContext()->SetClipboardText(target);
    }
}
