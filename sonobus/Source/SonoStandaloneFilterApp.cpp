// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2020 Jesse Chappell

/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2017 - ROLI Ltd.

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 5 End-User License
   Agreement and JUCE 5 Privacy Policy (both updated and effective as of the
   27th April 2017).

   End User License Agreement: www.juce.com/juce-5-licence
   Privacy Policy: www.juce.com/juce-5-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

// needed for crappy windows
#define NOMINMAX


#include "JuceHeader.h"

#include "juce_core/system/juce_TargetPlatform.h"
#include "juce_audio_plugin_client/detail/juce_CheckSettingMacros.h"

#if !JUCE_LINUX
#include "juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h"
#include "juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h"
#include "juce_gui_basics/native/juce_WindowsHooks_windows.h"
#endif

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>


// You can set this flag in your build if you need to specify a different
// standalone JUCEApplication class for your app to use. If you don't
// set it then by default we'll just create a simple one as below.
//#if ! JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

#include "SonoStandaloneFilterWindow.h"
#include "SonoLookAndFeel.h"

#include "SonobusPluginEditor.h"
#include "AppIdentity.h"
#include "ApiServer.h"   // P4.1
#include "EngineState.h" // P4.2
#include "AgentConnect.h" // P2.4
#include "ApiCommands.h" // P4.3, P4.4
#include <csignal>

// P2.1: --config <file.yaml>. Desktop only (rapidyaml is not part of the mobile builds).
#if !(JUCE_IOS || JUCE_ANDROID)
 #define CROSSPOINT_HAS_CONFIG 1
 #include "Config.h"
 #if JUCE_LINUX
  #include "PipeWireDevices.h"   // P2.11
  #if __has_include(<alsa/asoundlib.h>)
   #include <alsa/asoundlib.h>   // P4.4: snd_config_update_free_global()
  #endif
 #endif
#else
 #define CROSSPOINT_HAS_CONFIG 0
#endif

#if JUCE_ANDROID
#include "android/SonoBusActivity.h"

#if JUCE_USE_ANDROID_OPENSLES || JUCE_USE_ANDROID_OBOE
  #include "juce_audio_devices/native/juce_HighPerformanceAudioHelpers_android.h"
#endif

#endif

namespace juce
{

//==============================================================================
class SonobusStandaloneFilterApp  : public JUCEApplication, public Timer
{
public:
    SonobusStandaloneFilterApp()
    {
        PluginHostType::jucePlugInClientCurrentWrapperType = AudioProcessor::wrapperType_Standalone;

        PropertiesFile::Options options;

        options.applicationName     = getApplicationName();
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support/" APP_ID_SETTINGS_DIR;
       #if JUCE_LINUX
        options.folderName          = "~/.config/" APP_ID_LINUX_DIR;
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);

        // P3.9: upstream's one-time move of ~/.config/SonoBus.settings is gone
        // on purpose. That file belongs to stock SonoBus, and moving it would
        // break the user's stock install.
    }

    ~SonobusStandaloneFilterApp()
    {
#if JUCE_ANDROID && JUCE_OPENGL
        detachGL();
#endif
    }

    const String getApplicationName() override              { return JucePlugin_Name; }
    const String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return true; }

    SonoLookAndFeel  sonoLNF;

    AooServerConnectionInfo cmdlineConnInfo;

    bool copyInfo = false;
    bool doInitialConnect = false;
    bool doImmediateQuit = false;
    bool doHeadless = false;
    String loadSetupFilename;
    String cmdlineArgUrl;
    // P1.1 / F2: --role sets our advertised role; --dump-peers writes the peer
    // table to a file for the test harness (empty means no dumping).
    String cmdlineRole;
    String dumpPeersFilename;
    // whether the option appeared at all, so a missing value is reported
    bool roleWasGiven = false;
    bool dumppeersWasGiven = false;
    bool configWasGiven = false;

#if CROSSPOINT_HAS_CONFIG
    // P2.1: the YAML as loaded (its path is what Config::save() writes back to,
    // for agent.setInput/setOutput in P2.6) and the merged CLI > YAML result,
    // which also carries the audio/codec keys the CLI has no flags for.
    crosspoint::Config yamlConfig;
    crosspoint::Config effectiveConfig;
    crosspoint::Config cliConfig;            // P4.4: what the command line gave (agent.reloadConfig re-merges)
    bool haveConfig = false;
 #if JUCE_LINUX
    // P2.11: the JUCE device names of the generated crosspoint_in/out PCMs
    // (empty = that direction is not pinned to a PipeWire node).
    crosspoint::pipewire::Resolved pipewirePins;
 #endif
#endif

    // P4.1: control API (HTTP + WebSocket). CLI only for now; P2.1 can fill
    // apiConfig from the YAML `api:` section in applyApiOptions() below
    // before the CLI values are applied, so the CLI keeps winning.
    crosspoint::ApiConfig apiConfig;
    bool apiPortWasGiven = false;
    bool apiOptionError = false;   // an invalid API option: quit with a non-zero status
    std::unique_ptr<crosspoint::ApiServer> apiServer;
    std::unique_ptr<crosspoint::EngineState> engineState;   // P4.2 (declared after apiServer: destroyed first)
    std::unique_ptr<crosspoint::ApiCommands> apiCommands;   // P4.3/P4.4 (declared after engineState: destroyed first)
    String loadedConfigPath;                                 // P4.2: agent state configPath

    // P2.4: headless only. Connect on launch and keep reconnecting; reopen a lost audio device.
    std::unique_ptr<crosspoint::AgentConnector> agentConnector;
    std::unique_ptr<crosspoint::AgentDeviceWatcher> agentDeviceWatcher;

    // SIGTERM/SIGINT in headless mode request a normal quit, so shutdown()
    // runs (settings saved, API sockets closed). The handler only sets a flag;
    // a message-thread timer does the quit.
    static std::atomic<bool>& quitSignalFlag() { static std::atomic<bool> f { false }; return f; }
    struct QuitSignalWatcher : public Timer
    {
        void timerCallback() override
        {
            if (quitSignalFlag().load())
            {
                stopTimer();
                if (auto* app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            }
        }
    } quitSignalWatcher;

    // P1.5 TEST ONLY: --test-control <file>, a JSON file polled ~1/s on the
    // message thread and applied with applyTestControlFile (F2 drives talk,
    // solo, mute and mic state of a running headless peer through it).
    String testControlFilename;
    bool testcontrolWasGiven = false;

    virtual StandalonePluginHolder* createHeadlessPlugin ()
    {
#ifdef JucePlugin_PreferredChannelConfigurations
        StandalonePluginHolder::PluginInOuts channels[] = { JucePlugin_PreferredChannelConfigurations };
#endif

        AudioDeviceManager::AudioDeviceSetup setupOptions;
        setupOptions.sampleRate = 48000;
#if JUCE_MAC
        setupOptions.bufferSize = 128;
#elif JUCE_ANDROID
        setupOptions.bufferSize = 192;
        setupOptions.sampleRate = AndroidHighPerformanceAudioHelpers::getNativeSampleRate();
#else
        setupOptions.bufferSize = 256;
#endif

        File settingsFile = appProperties.getStorageParameters().getDefaultFile();
        File crashSentinelFile = settingsFile.getSiblingFile("SENTINEL");
        if (crashSentinelFile.existsAsFile()) {
            DBG("CRASH SENTINEL STILL EXISTS, moving old settings away!");
            File oldfile = settingsFile.getSiblingFile(String("POSSIBLY_BAD_") + Time::getCurrentTime().formatted("%Y-%m-%d_%H.%M.%S"));
            settingsFile.moveFileTo(oldfile);
            Timer::callAfterDelay (800, []()
                                   {
                AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon,
                                                  TRANS("Crashed Last Time"),
                                                  TRANS("Looks like you crashed on launch last time, restoring default settings!"));
            });
        }
        else {
            crashSentinelFile.create();
        }

        String prefDevname;

        auto plugh = new StandalonePluginHolder (appProperties.getUserSettings(), false,
                                                 prefDevname, &setupOptions
#ifdef JucePlugin_PreferredChannelConfigurations
                                                 , juce::Array<StandalonePluginHolder::PluginInOuts> (channels, juce::numElementsInArray (channels))
#else
                                                 , {}
#endif
                                                 , false);


        // if we got here, we didn't crash on initialization!
        crashSentinelFile.deleteFile();

        return plugh;
    }

    virtual StandaloneFilterWindow* createWindow()
    {
       #ifdef JucePlugin_PreferredChannelConfigurations
        StandalonePluginHolder::PluginInOuts channels[] = { JucePlugin_PreferredChannelConfigurations };
       #endif

        AudioDeviceManager::AudioDeviceSetup setupOptions;
        setupOptions.sampleRate = 48000;
#if JUCE_MAC
        setupOptions.bufferSize = 128;
#elif JUCE_ANDROID
        setupOptions.bufferSize = 192;
        setupOptions.sampleRate = AndroidHighPerformanceAudioHelpers::getNativeSampleRate();
#else
        setupOptions.bufferSize = 256;
#endif

        File settingsFile = appProperties.getStorageParameters().getDefaultFile();
        File crashSentinelFile = settingsFile.getSiblingFile("SENTINEL");
        if (crashSentinelFile.existsAsFile()) {
            DBG("CRASH SENTINEL STILL EXISTS, moving old settings away!");
            File oldfile = settingsFile.getSiblingFile(String("POSSIBLY_BAD_") + Time::getCurrentTime().formatted("%Y-%m-%d_%H.%M.%S"));
            settingsFile.moveFileTo(oldfile);
            Timer::callAfterDelay (800, []()
            {
                AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon,
                                                  TRANS("Crashed Last Time"),
                                                  TRANS("Looks like you crashed on launch last time, restoring default settings!"));
            });
        }
        else {
            crashSentinelFile.create();
        }

        LookAndFeel::setDefaultLookAndFeel(&sonoLNF);

        auto wind = new StandaloneFilterWindow (getApplicationName(),
                                           LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
                                           appProperties.getUserSettings(),
                                           false, {}, &setupOptions
                                          #ifdef JucePlugin_PreferredChannelConfigurations
                                           , juce::Array<StandalonePluginHolder::PluginInOuts> (channels, juce::numElementsInArray (channels))
                                          #else
                                           , {}
                                          #endif
                                          //#if JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE
                                           , false
                                          //#endif
                                           );

        // if we got here, we didn't crash on initialization!
        crashSentinelFile.deleteFile();

        return wind;
    }

    void setupDefaultConnInfo() {
        String username;// = processor.getCurrentUsername();

        if (username.isEmpty()) {
#if JUCE_IOS
            //String username = SystemStats::getFullUserName(); //SystemStats::getLogonName();
            username = SystemStats::getComputerName(); //SystemStats::getLogonName();
#else
            username = SystemStats::getFullUserName(); //SystemStats::getLogonName();
            //if (username.length() > 0) username = username.replaceSection(0, 1, username.substring(0, 1).toUpperCase());
#endif
        }
        if (username.isEmpty()) { // fallback
            username = SystemStats::getComputerName();
        }

        cmdlineConnInfo.userName = username.trim();

        cmdlineConnInfo.serverHost = DEFAULT_SERVER_HOST;
        cmdlineConnInfo.serverPort = DEFAULT_SERVER_PORT;
    }

    static void printCommandDescription (const ArgumentList& args, const ConsoleApplication::Command& command,
                                         int descriptionIndent)
    {
        auto carg = command.argumentDescription;

        if (carg.length() > descriptionIndent)
            std::cout << "  " << carg << std::endl << String().paddedRight (' ', descriptionIndent);
        else
            std::cout << "  " << carg.paddedRight (' ', descriptionIndent);

        std::cout << command.shortDescription << std::endl;

        if (command.longDescription.isNotEmpty()) {
            std::cout << "     " << command.longDescription << std::endl;
        }
    }

    void printCommandList (ConsoleApplication & capp, const ArgumentList& args) const
    {
        int descriptionIndent = 4;
        auto commands = capp.getCommands();

        for (auto& c : commands)
            descriptionIndent = std::max (descriptionIndent, c.argumentDescription.length());

        descriptionIndent = std::min (descriptionIndent + 2, 40);

        for (auto& c : commands)
            printCommandDescription (args, c, descriptionIndent);

        std::cout << std::endl;
    }

    // JUCE's ArgumentList::removeValueForOption only reads the value of a LONG
    // option when it is written as "--opt=value". With "--opt value" it removes
    // the option token, returns nothing, and leaves the value behind as a stray
    // positional argument. The help text advertises the space form, so handle
    // the space form first and fall back to JUCE for the "=" form.
    // `wasGiven` is set when the option appeared at all, so a missing value can
    // be reported instead of silently ignored.
    static String removeLongOptionValue(ArgumentList & arglist, const String & option,
                                       bool * wasGiven = nullptr)
    {
        if (wasGiven != nullptr) *wasGiven = false;

        for (int i = 0; i < arglist.arguments.size(); ++i) {
            if (arglist.arguments.getReference(i).text == option) {
                if (wasGiven != nullptr) *wasGiven = true;
                if (i < arglist.arguments.size() - 1
                    && !arglist.arguments.getReference(i + 1).isOption()) {
                    auto result = arglist.arguments.getReference(i + 1).text;
                    arglist.arguments.removeRange(i, 2);
                    return result;
                }
                // bare option with no value
                arglist.arguments.remove(i);
                return {};
            }
        }

        // "--opt=value"
        auto eq = arglist.removeValueForOption(option);
        if (wasGiven != nullptr && eq.isNotEmpty()) *wasGiven = true;
        return eq;
    }

    // P1.1: apply --role to a freshly created processor. Called immediately
    // after the plugin holder is built and before anything connects, so the
    // first peer-info advertisement already carries the real role.
    void applyCommandLineRole (AudioProcessor * proc)
    {
        if (cmdlineRole.isEmpty() || proc == nullptr) return;
        if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(proc)) {
            // P2.1: locked, so neither --load-setup nor the saved state can
            // override it afterwards (CLI > YAML > setup file > saved state).
            sonoproc->setRoleAndLock(SonobusAudioProcessor::peerRoleFromString(cmdlineRole));
        }
    }

    // P4.1: parse the control API options, validate them, and fill apiConfig.
    // Errors set doImmediateQuit (nothing has started yet, so refusing here is clean).
    void applyApiOptions (ArgumentList & arglist)
    {
        const bool isVdi = cmdlineRole == "vdi";
        apiConfig.role = isVdi ? "vdi" : "console";
        apiConfig.appName = "Crosspoint";
        apiConfig.version = JucePlugin_VersionString;
        apiConfig.port = isVdi ? 7071 : 7070;
#if CROSSPOINT_HAS_CONFIG
        // YAML `api:` (P2.1) gives the defaults; the --api-* flags below still win.
        if (haveConfig) {
            const auto & ya = effectiveConfig.api;
            if (ya.port) { apiConfig.port = *ya.port; apiPortWasGiven = true; }
            if (ya.bind) apiConfig.bindAddress = String(*ya.bind);
            if (ya.token) apiConfig.token = String(*ya.token);
            if (ya.allowedOrigins) for (auto & o : *ya.allowedOrigins) apiConfig.allowedOrigins.add(String(o));
        }
#endif
        apiConfig.features.clear();
        // P4.2: what the state really carries (clients ignore unknown features)
        apiConfig.features.add (isVdi ? "consoles" : "stations");
        apiConfig.features.add ("meters");     // P4.3
        apiConfig.features.add ("commands");   // P4.4
        if (!isVdi) apiConfig.features.add ("ptt");
        apiConfig.selfName = cmdlineConnInfo.userName.isNotEmpty() ? cmdlineConnInfo.userName
                                                                   : SystemStats::getComputerName();

        // An earlier failure (e.g. a bad --config, exit 1) must keep its status;
        // only a problem found by THIS function is an API option error (exit 2).
        const bool quitBeforeApi = doImmediateQuit;
        bool given = false;
        auto portStr = removeLongOptionValue(arglist, "--api-port", &given);
        if (given) {
            apiPortWasGiven = true;
            if (portStr.isEmpty() || !portStr.containsOnly("0123456789") || portStr.getIntValue() > 65535) {
                std::cerr << "Error: --api-port needs a number from 0 to 65535 (0 disables the API)" << std::endl;
                doImmediateQuit = true;
            } else {
                apiConfig.port = portStr.getIntValue();
            }
        }

        auto bind = removeLongOptionValue(arglist, "--api-bind", &given);
        if (given) {
            if (bind.trim().isEmpty()) {
                std::cerr << "Error: --api-bind requires an address" << std::endl;
                doImmediateQuit = true;
            } else {
                apiConfig.bindAddress = bind.trim();
            }
        }

        auto token = removeLongOptionValue(arglist, "--api-token", &given);
        if (given && token.isEmpty()) {
            std::cerr << "Error: --api-token requires a value" << std::endl;
            doImmediateQuit = true;
        }
        if (token.isEmpty())
            token = SystemStats::getEnvironmentVariable("CROSSPOINT_API_TOKEN", {});
        if (token.isNotEmpty() || apiConfig.token.isEmpty())   // YAML api.token stays unless overridden
            apiConfig.token = token;

        for (;;) {
            auto origin = removeLongOptionValue(arglist, "--api-allow-origin", &given);
            if (!given) break;
            if (origin.isEmpty()) {
                std::cerr << "Error: --api-allow-origin requires a value" << std::endl;
                doImmediateQuit = true;
                break;
            }
            apiConfig.allowedOrigins.add(origin);
        }

        auto uidir = removeLongOptionValue(arglist, "--ui-dir", &given);
        if (given) {
            File dir = File::getCurrentWorkingDirectory().getChildFile(uidir);
            if (uidir.isEmpty() || !dir.isDirectory()) {
                std::cerr << "Error: --ui-dir '" << uidir << "' is not a directory" << std::endl;
                doImmediateQuit = true;
            } else {
                apiConfig.uiDir = dir;
            }
        }

        // Never expose the control API beyond loopback without a token.
        if (apiConfig.port > 0 && !apiConfig.isLoopbackBind() && apiConfig.token.isEmpty()) {
            std::cerr << "Error: --api-bind " << apiConfig.bindAddress
                      << " is not a loopback address; refusing to start without --api-token" << std::endl;
            doImmediateQuit = true;
        }
        if (doImmediateQuit && !quitBeforeApi) apiOptionError = true;
    }

    // P4.1: start the control API. An explicit --api-port that cannot be bound is
    // fatal; a busy default port only logs, so several apps can share a machine.
    // Returns false when the app should quit.
    bool startApiServer()
    {
        if (apiConfig.port <= 0) return true;

        apiServer = std::make_unique<crosspoint::ApiServer>(apiConfig);
        auto r = apiServer->start();
        if (r.wasOk()) {
            std::cerr << "Control API listening on http://" << apiConfig.bindAddress << ":" << apiConfig.port << "/" << std::endl;
            return true;
        }
        apiServer.reset();
        std::cerr << (apiPortWasGiven ? "Error: " : "Warning: control API disabled: ") << r.getErrorMessage() << std::endl;
        return !apiPortWasGiven;
    }

    // P4.2: the engine state (snapshot + patches) behind the control API. Needs the
    // processor and device manager, so it starts at the end of initialise().
    void startEngineState()
    {
        if (apiServer == nullptr) return;
        SonobusAudioProcessor* proc = nullptr;
        AudioDeviceManager* dm = nullptr;
        if (mainWindow != nullptr && mainWindow->pluginHolder != nullptr) {
            proc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get());
            dm = &mainWindow->getDeviceManager();
        } else if (pluginHolder != nullptr) {
            proc = dynamic_cast<SonobusAudioProcessor*>(pluginHolder->processor.get());
            dm = &pluginHolder->deviceManager;
        }
        if (proc == nullptr) return;
        crosspoint::EngineState::Options o;
        o.role = apiConfig.role;
        o.selfName = apiConfig.selfName;
        o.configPath = loadedConfigPath;
        engineState = std::make_unique<crosspoint::EngineState>(*proc, dm, *apiServer, o);

        // P4.3 / P4.4: meters stream and command handler
        crosspoint::ApiCommands::Hooks hooks;
        hooks.retryNow = [this]() { if (!agentConnector) return false; agentConnector->retryNow(); return true; };
        hooks.takeOverConnection = [this, proc]() {
            if (!agentConnector) return;
            agentConnector.reset();
            proc->clearAgentConnStatus();   // the state falls back to the live view
        };
        hooks.initialConnection = cmdlineConnInfo;
        hooks.hasInitialConnection = cmdlineConnInfo.serverHost.isNotEmpty() && cmdlineConnInfo.groupName.isNotEmpty();
       #if CROSSPOINT_HAS_CONFIG
        hooks.setAgentDevice = [this, dm](bool isInput, const String & node, String & previous, String & code, String & msg) {
            return apiSetAgentDevice(dm, isInput, node, true, previous, code, msg);
        };
        hooks.reloadConfig = [this, proc, dm](String & code, String & msg) { return apiReloadConfig(proc, dm, code, msg); };
       #endif
        apiCommands = std::make_unique<crosspoint::ApiCommands>(*proc, dm, *apiServer, *engineState, apiConfig.role, std::move(hooks));
    }

#if CROSSPOINT_HAS_CONFIG
    //==========================================================================
    // P4.4: agent.setInput / agent.setOutput / agent.reloadConfig

    static String firstLine (const String & s) { return s.upToFirstOccurrenceOf("\n", false, false).trim(); }

    // Switches one audio device live and, with --config, writes it to the YAML
    // (Config::setAudioDevices + save(), comments kept). `previous` is the name
    // that was in use (the PipeWire node on Linux when the YAML named one).
    bool apiSetAgentDevice (AudioDeviceManager * dm, bool isInput, const String & node, bool writeYaml,
                            String & previous, String & code, String & msg)
    {
        if (dm == nullptr) { code = "not_supported"; msg = "No audio device manager"; return false; }
        const auto before = dm->getAudioDeviceSetup();
        previous = isInput ? before.inputDeviceName : before.outputDeviceName;

        crosspoint::Config cfg;   // only the direction being changed
        std::string wanted = node.toStdString();
       #if JUCE_LINUX
        {
            // P2.11: node names are PipeWire nodes. Validate against pw-dump and
            // regenerate the pinned ALSA PCMs (crosspoint_in / crosspoint_out), keeping
            // the other direction pinned as it was. UNTESTED on Linux (no PipeWire here).
            const auto & eff = effectiveConfig;
            if (isInput && eff.inputDevice) previous = String(*eff.inputDevice);
            if (!isInput && eff.outputDevice) previous = String(*eff.outputDevice);
            std::optional<std::string> wantIn = eff.inputDevice, wantOut = eff.outputDevice;
            (isInput ? wantIn : wantOut) = wanted;
            crosspoint::pipewire::Resolved pins;
            std::string pwErr, pwNote;
            const auto confPath = File::getSpecialLocation(File::userHomeDirectory)
                                      .getChildFile(".config/" APP_ID_LINUX_DIR "/asound.conf").getFullPathName().toStdString();
            if (!crosspoint::pipewire::prepare(wantIn, wantOut, confPath, pins, pwErr, pwNote)) {
                code = "not_found"; msg = firstLine(String(pwErr)); return false;
            }
            pipewirePins = pins;
            const auto & juceName = isInput ? pins.inputJuceName : pins.outputJuceName;
            if (!juceName.empty()) wanted = juceName;
           #if __has_include(<alsa/asoundlib.h>)
            // libasound caches its config; make it re-read the regenerated file, and
            // force the device to reopen (same JUCE name, different node behind it).
            snd_config_update_free_global();
           #endif
            dm->closeAudioDevice();
        }
       #endif
        if (isInput) cfg.inputDevice = wanted; else cfg.outputDevice = wanted;

        String err;
        if (!applyAudioConfig(*dm, cfg, err)) {
            const bool openFailed = err.startsWith("could not open");
            if (openFailed) dm->setAudioDeviceSetup(before, true);   // go back to what worked
            code = openFailed ? "internal" : "not_found";
            msg = firstLine(err);
            return false;
        }

        if (isInput) { effectiveConfig.inputDevice = node.toStdString(); yamlConfig.setAudioDevices(node.toStdString(), std::nullopt); }
        else         { effectiveConfig.outputDevice = node.toStdString(); yamlConfig.setAudioDevices(std::nullopt, node.toStdString()); }

        if (writeYaml && haveConfig && !yamlConfig.path.empty()) {
            std::string saveErr;
            if (!yamlConfig.save(saveErr)) {
                code = "io_error";
                msg = "Switched, but could not write the config: " + String(saveErr);
                return false;
            }
        }
        return true;
    }

    // Re-reads the --config file. A broken file leaves the running setup alone and
    // lands in state.configError / connection.error (and the peer-info agent block).
    bool apiReloadConfig (SonobusAudioProcessor * proc, AudioDeviceManager * dm, String & code, String & msg)
    {
        if (!haveConfig || yamlConfig.path.empty()) { code = "not_supported"; msg = "Started without --config"; return false; }
        crosspoint::Config fresh;
        std::string err;
        if (!crosspoint::Config::load(yamlConfig.path, fresh, err)) {
            proc->setApiConfigError(String(err));
            if (engineState) engineState->setConfigError(String(err));
            code = "bad_request"; msg = firstLine(String(err));
            return false;
        }
        proc->setApiConfigError({});
        if (engineState) engineState->setConfigError({});

        const auto old = effectiveConfig;
        yamlConfig = fresh;
        effectiveConfig = crosspoint::Config::merge(cliConfig, fresh);
        const auto & eff = effectiveConfig;

        // devices that differ from what is open now
        String prev, c2, m2;
        if (dm != nullptr) {
            const auto setup = dm->getAudioDeviceSetup();
            if (eff.inputDevice && String(*eff.inputDevice) != (old.inputDevice ? String(*old.inputDevice) : setup.inputDeviceName))
                apiSetAgentDevice(dm, true, String(*eff.inputDevice), false, prev, c2, m2);
            if (eff.outputDevice && String(*eff.outputDevice) != (old.outputDevice ? String(*old.outputDevice) : setup.outputDeviceName))
                apiSetAgentDevice(dm, false, String(*eff.outputDevice), false, prev, c2, m2);
        }
        // codec / bitrate (without re-applying the audio keys)
        if (eff.codec != old.codec || eff.bitrate != old.bitrate) {
            const auto keep = effectiveConfig;
            effectiveConfig.inputDevice.reset(); effectiveConfig.outputDevice.reset();
            effectiveConfig.sampleRate.reset(); effectiveConfig.buffer.reset();
            applyConfigRuntime(proc, nullptr);
            effectiveConfig = keep;
        }
        // server / group / name / password changed: reconnect
        // (with the P2.4 connector the session target is fixed at start: a changed
        // server/group needs a restart)
        if (apiCommands && !agentConnector && eff.server && eff.group
            && (eff.server != old.server || eff.group != old.group || eff.password != old.password || eff.username != old.username)) {
            AooServerConnectionInfo ci = cmdlineConnInfo;
            std::string host, e2; int port = 0;
            crosspoint::Config::splitServer(*eff.server, host, port, e2);
            ci.serverHost = host; ci.serverPort = port > 0 ? port : DEFAULT_SERVER_PORT;
            ci.groupName = String(*eff.group).trim();
            ci.groupPassword = eff.password ? String(*eff.password) : String();
            if (eff.username) ci.userName = String(*eff.username).trim();
            try { apiCommands->connectWith(ci); } catch (...) {}
        }
        return true;
    }
#endif

#if CROSSPOINT_HAS_CONFIG
    //==========================================================================
    // P2.1: --config

    // Reads --config, merges it under the values given on the command line and
    // stores the result in the cmdline* members. Returns false (after printing
    // why) if the file is unreadable, malformed or invalid.
    bool loadConfigFile (const String & path, const crosspoint::Config & cli)
    {
        File f = File::getCurrentWorkingDirectory().getChildFile(path);
        std::string err;
        if (!crosspoint::Config::load(f.getFullPathName().toStdString(), yamlConfig, err)) {
            std::cerr << "Error in --config: " << err << std::endl;
            return false;
        }
        haveConfig = true;
        loadedConfigPath = f.getFullPathName();
        cliConfig = cli;
        effectiveConfig = crosspoint::Config::merge(cli, yamlConfig);
        const auto & eff = effectiveConfig;

        // Only YAML-sourced values are applied here: whatever the command line
        // gave has already been applied (and, being highest priority, stays).
        if (!cli.server && eff.server) {
            std::string host, e2;
            int port = 0;
            crosspoint::Config::splitServer(*eff.server, host, port, e2); // validated at load
            cmdlineConnInfo.serverHost = host;
            cmdlineConnInfo.serverPort = port > 0 ? port : DEFAULT_SERVER_PORT;
            copyInfo = true;
        }
        if (!cli.group && eff.group) {
            cmdlineConnInfo.groupName = String(*eff.group).trim();
            doInitialConnect = true;
            copyInfo = true;
        }
        if (!cli.password && eff.password) {
            cmdlineConnInfo.groupPassword = String(*eff.password);
            copyInfo = true;
        }
        if (!cli.username && eff.username) {
            cmdlineConnInfo.userName = String(*eff.username).trim();
            copyInfo = true;
        }
        if (eff.role) {
            cmdlineRole = String(*eff.role); // the CLI role, if any, already won the merge
        }

       #if JUCE_LINUX
        // P2.11: audio.input_device / audio.output_device name PipeWire nodes. This
        // has to happen now, while --config is read: it points ALSA_CONFIG_PATH at
        // a generated config, which libasound only honours if it has not loaded its
        // global config yet (i.e. before the AudioDeviceManager exists).
        if (eff.inputDevice || eff.outputDevice) {
            std::string pwErr, pwNote;
            const auto confPath = File::getSpecialLocation(File::userHomeDirectory)
                                      .getChildFile(".config/" APP_ID_LINUX_DIR "/asound.conf").getFullPathName().toStdString();
            if (!crosspoint::pipewire::prepare(eff.inputDevice, eff.outputDevice, confPath, pipewirePins, pwErr, pwNote)) {
                std::cerr << "Error in --config: " << pwErr << std::endl;
                return false;
            }
            if (!pwNote.empty()) std::cerr << "Config: " << pwNote << std::endl;
            if (!pipewirePins.inputJuceName.empty() || !pipewirePins.outputJuceName.empty())
                std::cerr << "Config: PipeWire pinning via " << pipewirePins.asoundConfPath
                          << " (input=" << (pipewirePins.inputJuceName.empty() ? "-" : *eff.inputDevice)
                          << " output=" << (pipewirePins.outputJuceName.empty() ? "-" : *eff.outputDevice) << ")" << std::endl;
        }
       #endif
        return true;
    }

    // Applies the keys that need the running processor / audio device manager:
    // codec, bitrate and audio.*. Must run AFTER --load-setup, so that YAML
    // outranks the setup file. Returns false after printing why.
    bool applyConfigRuntime (SonobusAudioProcessor * proc, AudioDeviceManager * dm)
    {
        if (!haveConfig || proc == nullptr) return true;
        const auto & cfg = effectiveConfig;

        // --- codec / bitrate ---
        if (cfg.codec || cfg.bitrate) {
            const bool wantPcm = cfg.codec && *cfg.codec == "pcm";
            int found = -1;
            String bitrates;
            for (int i = 0; i < proc->getNumberAudioCodecFormats(); ++i) {
                SonobusAudioProcessor::AudioCodecFormatInfo info;
                if (!proc->getAudioCodeFormatInfo(i, info)) continue;
                if (wantPcm) {
                    if (info.codec == SonobusAudioProcessor::CodecPCM && info.bitdepth == 2) { found = i; break; } // 16 bit
                } else if (info.codec == SonobusAudioProcessor::CodecOpus) {
                    bitrates << (bitrates.isEmpty() ? "" : ", ") << info.bitrate;
                    if (cfg.bitrate && info.bitrate == *cfg.bitrate) found = i;
                }
            }
            if (!wantPcm && !cfg.bitrate) {
                // codec: opus without a bitrate keeps an Opus default as it is
                SonobusAudioProcessor::AudioCodecFormatInfo cur;
                if (proc->getAudioCodeFormatInfo(proc->getDefaultAudioCodecFormat(), cur)
                    && cur.codec == SonobusAudioProcessor::CodecOpus) {
                    found = proc->getDefaultAudioCodecFormat();
                }
            }
            if (found < 0 && !wantPcm && !cfg.bitrate) {
                for (int i = 0; i < proc->getNumberAudioCodecFormats(); ++i) {
                    SonobusAudioProcessor::AudioCodecFormatInfo info;
                    if (proc->getAudioCodeFormatInfo(i, info) && info.codec == SonobusAudioProcessor::CodecOpus
                        && info.bitrate == 96000) { found = i; break; }
                }
            }
            if (found < 0) {
                std::cerr << "Error in --config: bitrate " << (cfg.bitrate ? *cfg.bitrate : 0)
                          << " is not a supported Opus bitrate (bits/s per channel). Supported: "
                          << bitrates << std::endl;
                return false;
            }
            proc->setDefaultAudioCodecFormat(found);
            std::cerr << "Config: default send format = " << proc->getAudioCodeFormatName(found) << std::endl;
        }

        // --- audio devices / sample rate / buffer ---
        if (dm != nullptr && (cfg.inputDevice || cfg.outputDevice || cfg.sampleRate || cfg.buffer)) {
            // P2.11: on Linux a PipeWire node name was already validated and turned
            // into the generated crosspoint_in / crosspoint_out PCM while --config
            // was read (loadConfigFile); select those PCMs by their JUCE names.
            // Names that are not PipeWire nodes are matched as before.
            String err;
            if (!reopenAudioDevices(*dm, err)) {
                std::cerr << "Error in --config: " << err << std::endl;
                return false;
            }
        }
        return true;
    }

    // P2.4: (re)opens the configured audio devices; also what the device
    // watcher calls to recover a lost device. Same path as the startup apply.
    bool reopenAudioDevices (AudioDeviceManager & dm, String & err)
    {
        crosspoint::Config audioCfg = effectiveConfig;
       #if JUCE_LINUX
        if (!pipewirePins.inputJuceName.empty())  audioCfg.inputDevice = pipewirePins.inputJuceName;
        if (!pipewirePins.outputJuceName.empty()) audioCfg.outputDevice = pipewirePins.outputJuceName;
       #endif
        if (!audioCfg.inputDevice && !audioCfg.outputDevice && !audioCfg.sampleRate && !audioCfg.buffer) {
            dm.restartLastAudioDevice();   // nothing configured: reopen whatever was last open
            if (dm.getCurrentAudioDevice() == nullptr) { err = "no audio device could be opened"; return false; }
            return true;
        }
        return applyAudioConfig(dm, audioCfg, err);
    }

    // Selects devices/rate/buffer on the device manager by JUCE device name.
    static bool applyAudioConfig (AudioDeviceManager & dm, const crosspoint::Config & cfg, String & err)
    {
        // Find a device type (CoreAudio, WASAPI, ALSA, ...) that offers the
        // requested devices, preferring the current one.
        const String wantIn = cfg.inputDevice ? String(*cfg.inputDevice) : String();
        const String wantOut = cfg.outputDevice ? String(*cfg.outputDevice) : String();

        auto & types = dm.getAvailableDeviceTypes();
        String listing;
        AudioIODeviceType * chosen = nullptr;
        auto * current = dm.getCurrentDeviceTypeObject();

        std::vector<AudioIODeviceType*> order;
        if (current != nullptr) order.push_back(current);
        for (auto * t : types) if (t != current) order.push_back(t);

        for (auto * t : order) {
            t->scanForDevices();
            const auto ins = t->getDeviceNames(true);
            const auto outs = t->getDeviceNames(false);
            listing << "  [" << t->getTypeName() << "]\n";
            for (auto & n : ins)  listing << "    input:  " << n << "\n";
            for (auto & n : outs) listing << "    output: " << n << "\n";
            if (chosen == nullptr
                && (wantIn.isEmpty() || ins.contains(wantIn))
                && (wantOut.isEmpty() || outs.contains(wantOut))) {
                chosen = t;
            }
        }

        if (chosen == nullptr) {
            String what;
            if (wantIn.isNotEmpty())  what << "audio.input_device '" << wantIn << "'";
            if (wantIn.isNotEmpty() && wantOut.isNotEmpty()) what << " and ";
            if (wantOut.isNotEmpty()) what << "audio.output_device '" << wantOut << "'";
            err = what + " not found (names must match exactly, in one audio system). Available devices:\n" + listing.trimEnd();
            return false;
        }

        if (chosen != current) dm.setCurrentAudioDeviceType(chosen->getTypeName(), true);

        auto setup = dm.getAudioDeviceSetup();
        if (wantIn.isNotEmpty())  setup.inputDeviceName = wantIn;
        if (wantOut.isNotEmpty()) setup.outputDeviceName = wantOut;
        if (cfg.sampleRate) setup.sampleRate = *cfg.sampleRate;
        if (cfg.buffer)     setup.bufferSize = *cfg.buffer;

        const String openErr = dm.setAudioDeviceSetup(setup, true);
        if (openErr.isNotEmpty()) {
            err = "could not open the audio device(s): " + openErr;
            return false;
        }

        // JUCE silently picks the closest rate/buffer; a config is an explicit
        // request, so report a mismatch instead of running on something else.
        if (auto * dev = dm.getCurrentAudioDevice()) {
            if (cfg.sampleRate && (int) std::lround(dev->getCurrentSampleRate()) != *cfg.sampleRate) {
                String rates;
                for (auto r : dev->getAvailableSampleRates()) rates << (rates.isEmpty() ? "" : ", ") << (int) r;
                err = "audio.sample_rate " + String(*cfg.sampleRate) + " is not supported by the device; it supports: " + rates;
                return false;
            }
            if (cfg.buffer && dev->getCurrentBufferSizeSamples() != *cfg.buffer) {
                String sizes;
                for (auto b : dev->getAvailableBufferSizes()) sizes << (sizes.isEmpty() ? "" : ", ") << b;
                err = "audio.buffer " + String(*cfg.buffer) + " is not supported by the device; it supports: " + sizes;
                return false;
            }
            std::cerr << "Config: audio input='" << setup.inputDeviceName << "' output='" << setup.outputDeviceName
                      << "' rate=" << dev->getCurrentSampleRate() << " buffer=" << dev->getCurrentBufferSizeSamples() << std::endl;
        }
        return true;
    }
#else
    bool applyConfigRuntime (SonobusAudioProcessor *, AudioDeviceManager *) { return true; }
    bool reopenAudioDevices (AudioDeviceManager & dm, String & err)
    {
        dm.restartLastAudioDevice();
        if (dm.getCurrentAudioDevice() == nullptr) { err = "no audio device could be opened"; return false; }
        return true;
    }
#endif

    // A config problem found after startup began: report the exit code and quit.
    void failStartup()
    {
        setApplicationReturnValue(1);
        quit();
    }

    void handleCommandLine()
    {
        ConsoleApplication app;

        const String versionSpec("-v|--version");
        const String helpSpec("-h|--help");

        const String serverSpec("-c|--connectionserver");
        const String serverSpecDesc("-c|--connectionserver <address[:port]>");

        const String groupSpec("-g|--group");
        const String groupSpecDesc("-g|--group <groupname>");

        const String groupPassSpec("-p|--group-password");
        const String groupPassSpecDesc("-p|--group-password <password>");

        const String userNameSpec("-n|--username");
        const String userNameSpecDesc("-n|--username <username>");

        const String headlessSpec("-q|--headless");
        const String headlessSpecDesc("-q|--headless");

        const String loadSetupSpec("-l|--load-setup");
        const String loadSetupSpecDesc("-l|--load-setup <setup-filename>");

        const String roleSpec("--role");
        const String roleSpecDesc("--role <vdi|console>");

        const String dumpPeersSpec("--dump-peers");
        const String dumpPeersSpecDesc("--dump-peers <filename>");

        const String configSpec("--config");
        const String configSpecDesc("--config <file.yaml>");

        const String testControlSpec("--test-control");
        const String testControlSpecDesc("--test-control <filename>");

        

        app.addCommand ({ helpSpec, helpSpec, TRANS("Prints the list of commands"), {}, nullptr });
        app.addCommand ({ versionSpec, versionSpec, TRANS("Prints the current version number only"), {}, nullptr });


        auto params = getCommandLineParameterArray();
        ArgumentList arglist(getApplicationName(), params);

        app.addCommand ({ groupSpec, groupSpecDesc,
            TRANS("Specify the group name to immediately connect to upon launch"), {},
            nullptr
        });

        app.addCommand ({ userNameSpec, userNameSpecDesc,
            TRANS("Specify the displayed username for yourself when connecting to a group"), {},
            nullptr
        });

        app.addCommand ({ groupPassSpec, groupPassSpecDesc,
            TRANS("Specify the password to use for the group name to connect to (optional)"), {},
            nullptr
        });

        app.addCommand ({ serverSpec, serverSpecDesc,
            TRANS("Specify connection server to use when connecting to a group (optional)"), {},
            nullptr
        });

        app.addCommand ({ loadSetupSpec, loadSetupSpecDesc,
            TRANS("Specify the filename of a setup file to load."),
            TRANS("The setup file can be created using the Save Setup feature from the full application, and includes any device selection, input mixer setup, and all other options. If you don't specify a full or relative pathname it will look for a preset file by that name in the last used setup folder."),
            nullptr
        });

        app.addCommand ({ headlessSpec, headlessSpecDesc,
            TRANS("If specified, no GUI will be used and the application will be run headless."),
            TRANS("You'll need to use other command-line options to connect to a group... eventually there will be an OSC remote control interface."),
            nullptr
        });

        app.addCommand ({ roleSpec, roleSpecDesc,
            TRANS("Specify the role for this peer: 'vdi' sends system audio and receives the Console mic, 'console' listens to VDIs and talks back (default: console)."),
            {},
            nullptr
        });

        app.addCommand ({ configSpec, configSpecDesc,
            TRANS("Read settings from a YAML file: server, group, password, username, role, audio.input_device, audio.output_device, audio.sample_rate, audio.buffer, codec, bitrate."),
            TRANS("Precedence, highest first: command-line options, the YAML file, the --load-setup file, the saved application state. Unknown keys and wrong types are errors. See vdi.example.yaml."),
            nullptr
        });

        app.addCommand ({ dumpPeersSpec, dumpPeersSpecDesc,
            TRANS("Write a JSON snapshot of the remote peer table to the given file, refreshed about once per second. Used by the local test harness."),
            {},
            nullptr
        });

        // P4.1: control API options (see applyApiOptions)
        app.addCommand ({ "--api-port", "--api-port <n>",
            TRANS("Port for the control API and web UI (HTTP + WebSocket). Default 7070 for a console, 7071 for a VDI; 0 disables it."), {}, nullptr });
        app.addCommand ({ "--api-bind", "--api-bind <address>",
            TRANS("Address the control API listens on (default 127.0.0.1). A non-loopback address requires --api-token."), {}, nullptr });
        app.addCommand ({ "--api-token", "--api-token <secret>",
            TRANS("Shared secret clients must send in their first WebSocket message. Can also be given in the CROSSPOINT_API_TOKEN environment variable."), {}, nullptr });
        app.addCommand ({ "--api-allow-origin", "--api-allow-origin <origin>",
            TRANS("Extra browser origin allowed to open the WebSocket, e.g. https://crosspoint.example.com. Repeatable."), {}, nullptr });
        app.addCommand ({ "--ui-dir", "--ui-dir <path>",
            TRANS("Folder of web UI files served at / (for example the repo's console-ui folder)."), {}, nullptr });

        app.addCommand ({ testControlSpec, testControlSpecDesc,
            TRANS("Test only: poll a JSON file about once per second and apply its mic/talk/solo/mute settings. Used by the local test harness."),
            {},
            nullptr
        });



        if (arglist.removeOptionIfFound(versionSpec)) {
            std::cout << getApplicationName() << TRANS(" version ") << getApplicationVersion() << std::endl;
            doImmediateQuit = true;
        }

        if (arglist.containsOption("-h|--help")) {
            std::cout << TRANS("Usage: ") << getApplicationName() << " [options...]  [connect_URL]" << std::endl;

            printCommandList(app, arglist);
            doImmediateQuit = true;
        }

        if (doImmediateQuit) {
            return;
        }

        setupDefaultConnInfo();

        auto connserv = arglist.removeValueForOption(serverSpec);
#if CROSSPOINT_HAS_CONFIG
        crosspoint::Config cliValues; // what the command line itself gave, for the merge below
        if (connserv.isNotEmpty()) cliValues.server = connserv.toStdString();
#endif
        if (connserv.isNotEmpty()) {
            cmdlineConnInfo.serverHost =  connserv.upToFirstOccurrenceOf(":", false, true);
            String portpart = connserv.fromFirstOccurrenceOf(":", false, false);
            int port = portpart.getIntValue();
            if (port > 0) {
                cmdlineConnInfo.serverPort = port;
            } else {
                cmdlineConnInfo.serverPort = DEFAULT_SERVER_PORT;
            }
            copyInfo = true;
        }

        auto groupname = arglist.removeValueForOption(groupSpec);
#if CROSSPOINT_HAS_CONFIG
        if (groupname.isNotEmpty()) cliValues.group = groupname.trim().toStdString();
#endif
        if (groupname.isNotEmpty()) {
            cmdlineConnInfo.groupName = groupname.trim();
            doInitialConnect = true;
            copyInfo = true;
        }

        auto grouppass = arglist.removeValueForOption(groupPassSpec);
#if CROSSPOINT_HAS_CONFIG
        if (grouppass.isNotEmpty()) cliValues.password = grouppass.toStdString();
#endif
        if (grouppass.isNotEmpty()) {
            cmdlineConnInfo.groupPassword = grouppass;
            copyInfo = true;
        }

        auto username = arglist.removeValueForOption(userNameSpec);
#if CROSSPOINT_HAS_CONFIG
        if (username.isNotEmpty()) cliValues.username = username.trim().toStdString();
#endif
        if (username.isNotEmpty()) {
            cmdlineConnInfo.userName = username.trim();
            copyInfo = true;
        }

        // P2.1: "--load-setup <file>" (space form) was silently ignored by JUCE's
        // removeValueForOption, which only reads "--load-setup=<file>" (and left
        // the file name behind as a stray argument). Handle the space form first.
        auto setupfile = removeLongOptionValue(arglist, "--load-setup");
        if (setupfile.isEmpty())
            setupfile = arglist.removeValueForOption(loadSetupSpec);
        if (setupfile.isNotEmpty()) {
            loadSetupFilename = setupfile;
        }

        auto role = removeLongOptionValue(arglist, roleSpec, &roleWasGiven);
        if (roleWasGiven && role.isEmpty()) {
            std::cerr << "Error: --role requires a value ('vdi' or 'console')" << std::endl;
            doImmediateQuit = true;
        }
        else if (role.isNotEmpty()) {
            auto r = role.trim().toLowerCase();
            if (r == "vdi" || r == "console") {
                cmdlineRole = r;
#if CROSSPOINT_HAS_CONFIG
                cliValues.role = r.toStdString();
#endif
            } else {
                std::cerr << "Error: --role must be 'vdi' or 'console', got '"
                          << role << "'" << std::endl;
                doImmediateQuit = true;
            }
        }

        auto dumppeers = removeLongOptionValue(arglist, dumpPeersSpec, &dumppeersWasGiven);
        if (dumppeersWasGiven && dumppeers.isEmpty()) {
            std::cerr << "Error: --dump-peers requires a filename" << std::endl;
            doImmediateQuit = true;
        }
        else if (dumppeers.isNotEmpty()) {
            dumpPeersFilename = dumppeers;
        }

        // P2.1: --config. After every other option so the CLI values are known;
        // before the --headless check so a YAML group satisfies it.
        auto configPath = removeLongOptionValue(arglist, configSpec, &configWasGiven);
        if (configWasGiven && configPath.isEmpty()) {
            std::cerr << "Error: --config requires a filename" << std::endl;
            doImmediateQuit = true;
            setApplicationReturnValue(1);
        }
        else if (configPath.isNotEmpty()) {
#if CROSSPOINT_HAS_CONFIG
            if (!doImmediateQuit && !loadConfigFile(configPath, cliValues)) {
                doImmediateQuit = true;
                setApplicationReturnValue(1);
            }
#else
            std::cerr << "Error: --config is not supported on this platform" << std::endl;
            doImmediateQuit = true;
            setApplicationReturnValue(1);
#endif
        }

        applyApiOptions (arglist);   // P4.1 (after --config, so YAML api:/role/username feed its defaults)

        auto testcontrol = removeLongOptionValue(arglist, testControlSpec, &testcontrolWasGiven);
        if (testcontrolWasGiven && testcontrol.isEmpty()) {
            std::cerr << "Error: --test-control requires a filename" << std::endl;
            doImmediateQuit = true;
        }
        else if (testcontrol.isNotEmpty()) {
            testControlFilename = testcontrol;
        }


        if (arglist.removeOptionIfFound(headlessSpec)) {

            doHeadless = true;

            // P4.1: a headless app with the control API on can be driven over it,
            // so it does not need a group on the command line. P2.1: don't repeat
            // an error that was already reported.
            if (!doInitialConnect && !doImmediateQuit && !(apiPortWasGiven && apiConfig.port > 0)) {
                std::cout << TRANS("Error: you need to specify a group to connect to for headless operation right now... eventually there will be an OSC interface.") << std::endl;
                doImmediateQuit = true;
                setApplicationReturnValue(1);
            }
        }

        // what args remain? assume it's a URL
        if (arglist.arguments.size() > 0) {
            cmdlineArgUrl = arglist.arguments.getLast().text;
        }

    }

    //==============================================================================
    void initialise (const String&) override
    {

        handleCommandLine();

        if (doImmediateQuit) {
            if (apiOptionError) setApplicationReturnValue (2);   // P4.1
            quit();
            return;
        };

       #if ! JUCE_WINDOWS && ! JUCE_ANDROID
        if (doHeadless) {
            // P4.1: let SIGTERM/SIGINT take the normal quit path (see QuitSignalWatcher).
            // Installed before the API starts listening so a signal can never beat it.
            auto onQuitSignal = [] (int) { quitSignalFlag().store (true); };
            std::signal (SIGTERM, onQuitSignal);
            std::signal (SIGINT, onQuitSignal);
            quitSignalWatcher.startTimer (200);
        }
       #endif

        if (!startApiServer()) {   // P4.1
            setApplicationReturnValue (2);
            quit();
            return;
        }


        if (!doHeadless) {
            mainWindow.reset (createWindow());

            // P1.1: set the role before anything connects, so the very first
            // peer-info we advertise carries the real role rather than the
            // default. Peers use it to decide routing (P1.4), so a wrong first
            // advertisement could briefly open or close the wrong path.
            applyCommandLineRole(mainWindow->pluginHolder->processor.get());

#if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
#endif

            mainWindow->setVisible (true);

            Desktop::getInstance().setScreenSaverEnabled(false);


            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get())) {
                if (sonoproc->hasEditor()) {
                    if (auto * sonoeditor = dynamic_cast<SonobusAudioProcessorEditor*>(sonoproc->createEditorIfNeeded())) {
                        sonoeditor->saveSettingsIfNeeded = [this]() {
                            mainWindow->pluginHolder->savePluginState();
                            mainWindow->pluginHolder->saveAudioDeviceState();
                            appProperties.saveIfNeeded();
                        };

                        // apply command line connection stuff

                        if (doInitialConnect) {
                            DBG("CONNECTING INITIAL");
                            sonoeditor->connectWithInfo(cmdlineConnInfo, false, false);
                        } else if (copyInfo) {
                            // only copy info
                            DBG("COPYING INITIAL");
                            sonoeditor->connectWithInfo(cmdlineConnInfo, false, true);
                        }
                        else if (cmdlineArgUrl.isNotEmpty()) {
#if JUCE_LINUX
                            // Linux only, as this is handled through other means on other platforms
                            // handle the last arg as a connect URL
                            sonoeditor->handleURL(cmdlineArgUrl);
#endif
                        }

                        if (loadSetupFilename.isNotEmpty()) {
                            File setupfile = File::getCurrentWorkingDirectory().getChildFile(loadSetupFilename);
                            if (!setupfile.exists()) {
                                // try the default location
                                String recentsfolder = mainWindow->pluginHolder->getLastRecentsFolder();
                                if (recentsfolder.isNotEmpty()) {
                                    setupfile = File(recentsfolder).getChildFile(setupfile.getFileName());
                                }
                            }
                            if (setupfile.exists()) {
                                Thread::sleep(200); // just in case
                                sonoeditor->loadSettingsFromFile(setupfile);
                            }
                            else {
                                std::cerr << "Settings file does not exist: " << loadSetupFilename << std::endl;
                            }
                        }
                    }
                }

                // P2.1: YAML codec/audio keys go on top of the setup file just loaded.
                if (!applyConfigRuntime(sonoproc, &mainWindow->getDeviceManager())) {
                    failStartup();
                    return;
                }
            }

#if JUCE_ANDROID && JUCE_OPENGL
            attachGL();
#endif


#if JUCE_ANDROID
            startTimer(500);
#endif
        }
        else {
            // headless mode

            pluginHolder.reset (createHeadlessPlugin());


            // P1.1: role first, before connecting/joining, so our initial
            // peer-info advertises the real role (see the windowed path above).
            applyCommandLineRole(pluginHolder->processor.get());

            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(pluginHolder->processor.get())) {

                // apply command line connection stuff

                if (loadSetupFilename.isNotEmpty()) {
                    File setupfile = File::getCurrentWorkingDirectory().getChildFile(loadSetupFilename);
                    if (!setupfile.exists()) {
                        // try the default location
                        String recentsfolder = pluginHolder->getLastRecentsFolder();
                        if (recentsfolder.isNotEmpty()) {
                            setupfile = File(recentsfolder).getChildFile(setupfile.getFileName());
                        }
                    }
                    if (setupfile.exists()) {
                        Thread::sleep(200); // just in case

                        if (loadSettingsFromFile(setupfile)) {
                            std::cerr << "Loaded Settings file: " << setupfile.getFullPathName() << std::endl;
                        }
                        else {
                            std::cerr << "Error loading settings file: " << setupfile.getFullPathName() << std::endl;
                        }
                    }
                    else {
                        std::cerr << "Settings file does not exist: " << loadSetupFilename << std::endl;
                    }
                }

                // P2.1: YAML codec/audio keys go on top of the setup file just loaded
                // (CLI > YAML > setup file > saved state); role is already pinned.
                if (!applyConfigRuntime(sonoproc, &pluginHolder->deviceManager)) {
                    failStartup();
                    return;
                }

                // P2.4: connect now and keep the session up (backoff, bad-password
                // handling). Replaces the old one-shot connect + 500 ms sleep + join.
                if (doInitialConnect) {
                    cmdlineConnInfo.timestamp = Time::getCurrentTime().toMilliseconds();
                    agentConnector = std::make_unique<crosspoint::AgentConnector>(*sonoproc, cmdlineConnInfo);
                }

                // P2.4: audio device loss -> report missing, retry the open with backoff.
                agentDeviceWatcher = std::make_unique<crosspoint::AgentDeviceWatcher>(
                    *sonoproc, pluginHolder->deviceManager,
                    [this]() -> String {
                        String err;
                        if (pluginHolder == nullptr) return "shutting down";
                        return reopenAudioDevices(pluginHolder->deviceManager, err) ? String() : (err.isEmpty() ? String("could not open the audio device") : err);
                    });
            }

        }


        startEngineState();   // P4.2

        // F2: start the peer-table dump timer when --dump-peers was given. The
        // timer runs on the message thread; the dump itself takes the core lock.
        if (dumpPeersFilename.isNotEmpty() || testControlFilename.isNotEmpty()) {
            startTimer(1000);
        }

#if JUCE_MAC
        disableAppNap();
#endif

    }


    bool loadSettingsFromFile(const File & file)
    {
        SonobusAudioProcessor * processor = nullptr;
        AudioDeviceManager * deviceManager = nullptr;
        StandalonePluginHolder * plugHolder = nullptr;

        if (mainWindow != nullptr && mainWindow->pluginHolder != nullptr) {
            processor = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get());
            deviceManager = &mainWindow->getDeviceManager();
            plugHolder = mainWindow->pluginHolder.get();
        }
        else if (pluginHolder != nullptr) {
            processor = dynamic_cast<SonobusAudioProcessor*>(pluginHolder->processor.get());
            deviceManager = &pluginHolder->deviceManager;
            plugHolder = pluginHolder.get();
        }

        if (processor == nullptr || deviceManager == nullptr) return false;

        bool retval = true;

        PropertiesFile::Options opts;
        PropertiesFile propfile = PropertiesFile(file, opts);

        if (!propfile.isValidFile()) {
            std::cerr << "Error while loading setup, could not read from the specified file!" << std::endl;

            return false;
        }

        MemoryBlock data;

        if (propfile.containsKey("filterStateXML")) {
            String filtxml = propfile.getValue ("filterStateXML");
            data.replaceWith(filtxml.toUTF8(), filtxml.getNumBytesAsUTF8());
            if (data.getSize() > 0) {
                processor->setStateInformationWithOptions (data.getData(), (int) data.getSize(), false, true, true);
            }
            else {
                DBG("Empty XML filterstate");
                retval = false;
            }
        }
        else {
            if (data.fromBase64Encoding (propfile.getValue ("filterState")) && data.getSize() > 0) {
                processor->setStateInformationWithOptions (data.getData(), (int) data.getSize(), false, true);
            } else {
                retval = false;
            }
        }

        auto savedAudioState = propfile.getXmlValue ("audioSetup");

        if (savedAudioState.get()) {
            std::unique_ptr<AudioDeviceManager::AudioDeviceSetup> prefSetupOptions;
            String preferredDefaultDeviceName;

            // now remove samplerate from saved state if necessary
            // as well as preferredSetupOptions

            if (!((bool)plugHolder->getShouldOverrideSampleRateValue().getValue())) {
                DBG("NOT OVERRIDING SAMPLERATE");
                if (savedAudioState && savedAudioState->hasAttribute("audioDeviceRate")) {
                    savedAudioState->removeAttribute("audioDeviceRate");
                }
                if (prefSetupOptions) {
                    prefSetupOptions->sampleRate = 0;
                }
            }



            auto totalInChannels  = processor->getMainBusNumInputChannels();
            auto totalOutChannels = processor->getMainBusNumOutputChannels();

            deviceManager->initialise (totalInChannels,
                                       totalOutChannels,
                                       savedAudioState.get(),
                                       true,
                                       preferredDefaultDeviceName,
                                       prefSetupOptions.get());
        }

        if (!retval) {
            std::cerr << "Error while loading setup, invalid setup!" << std::endl;
        }

        return retval;
    }


    void timerCallback() override
    {
#if JUCE_ANDROID
        DBG("setting foreground service active");
        setAndroidForegroundServiceActive(true);
        stopTimer();
        return;
#endif

        SonobusAudioProcessor * dumpProc = nullptr;
        if (mainWindow.get() != nullptr && mainWindow->pluginHolder != nullptr) {
            dumpProc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get());
        } else if (pluginHolder != nullptr) {
            dumpProc = dynamic_cast<SonobusAudioProcessor*>(pluginHolder->processor.get());
        }

        // P1.5 TEST ONLY: apply the control file first, so the dump written
        // right after reflects it. Re-applied every tick (the setters are
        // idempotent) so peers that join later pick it up too.
        if (testControlFilename.isNotEmpty() && dumpProc != nullptr) {
            dumpProc->applyTestControlFile(File::getCurrentWorkingDirectory()
                                               .getChildFile(testControlFilename));
        }

        // P2.4 TEST ONLY: {"audioLoss": true} in the control file closes the audio
        // device and makes every reopen fail until it is cleared.
        if (testControlFilename.isNotEmpty() && agentDeviceWatcher != nullptr) {
            auto ctl = JSON::parse(File::getCurrentWorkingDirectory().getChildFile(testControlFilename));
            agentDeviceWatcher->setTestDeviceGone(ctl.getProperty("audioLoss", false));
        }

        // F2: periodic peer-table dump for the test harness.
        if (dumpPeersFilename.isNotEmpty() && dumpProc != nullptr) {
            dumpProc->dumpPeersToFile(File::getCurrentWorkingDirectory()
                                          .getChildFile(dumpPeersFilename));
        }
    }

    void shutdown() override
    {
        //DBG("shutdown");
        quitSignalWatcher.stopTimer();
        apiCommands.reset(); // P4.3/P4.4: before the state and the server
        agentConnector.reset();        // P2.4: before the processor goes away
        agentDeviceWatcher.reset();
        engineState.reset(); // P4.2: before the server it publishes to
        apiServer.reset();   // P4.1: closes sockets and joins the API threads first
        if (mainWindow.get() != nullptr) {
            mainWindow->pluginHolder->savePluginState();
            mainWindow->pluginHolder->saveAudioDeviceState();
        }
        else if (pluginHolder != nullptr) {
            // Headless mode also needs to persist plugin state, otherwise
            // settings set from the command line (e.g. --role) are lost on
            // exit, which the VDI agent relies on.
            pluginHolder->savePluginState();
            pluginHolder->saveAudioDeviceState();
        }

#if JUCE_ANDROID
        setAndroidForegroundServiceActive(false);
  #if JUCE_OPENGL
        detachGL();
  #endif
#endif

        mainWindow = nullptr;

        pluginHolder = nullptr;

        appProperties.saveIfNeeded();

    }
    
    void urlOpened(const URL & url) override {
        DBG("Url opened: " << url.toString(true));
        
        if (mainWindow.get() != nullptr) {
            
            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get())) {
                if (sonoproc->hasEditor()) {
                    if (auto * sonoeditor = dynamic_cast<SonobusAudioProcessorEditor*>(sonoproc->createEditorIfNeeded())) {
                        sonoeditor->handleURL(url.toString(true));
                        mainWindow->toFront(true);
                    }
                }
            }
        }        
    }
    
    void anotherInstanceStarted (const String& url) override    {
        
        DBG("Url handled from another instance: " << url);
        
        if (mainWindow.get() != nullptr) {
            
            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get())) {
                if (sonoproc->hasEditor()) {
                    if (auto * sonoeditor = dynamic_cast<SonobusAudioProcessorEditor*>(sonoproc->createEditorIfNeeded())) {
                        sonoeditor->handleURL(url);
                        mainWindow->toFront(true);
                    }
                }
            }
        }
    }
        
    void suspended() override
    {
        DBG("suspended");
        if (mainWindow.get() != nullptr) {
            mainWindow->pluginHolder->savePluginState();
            mainWindow->pluginHolder->saveAudioDeviceState();

            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get())) {
                if (sonoproc->getNumberRemotePeers() == 0 && !mainWindow->pluginHolder->isInterAppAudioConnected()) {
                    // shutdown audio engine
                    DBG("no connections shutting down audio");
                    mainWindow->getDeviceManager().closeAudioDevice();
                    sonoproc->setPlayHead (nullptr);
#if JUCE_ANDROID
                    setAndroidForegroundServiceActive(false);
#endif
                }
                else {
#if JUCE_ANDROID
                    setAndroidForegroundServiceActive(true);
#endif
                }
            }

        }

        appProperties.saveIfNeeded();

        auto props = appProperties.getUserSettings()->getAllProperties();
        for (auto & prop : props.getAllKeys()) {
            DBG("save state key: " << prop );
        }
        
        Desktop::getInstance().setScreenSaverEnabled(true);        
    }

    void setAndroidForegroundServiceActive(bool flag)
    {
    #if JUCE_ANDROID
        LocalRef<jobject> activity (getMainActivity());

        if (activity != nullptr) {
            getEnv()->CallVoidMethod(activity.get(), SonoBusActivity.setForegroundServiceActive, flag);
        }
    #endif
    }


    void resumed() override
    {
        Desktop::getInstance().setScreenSaverEnabled(false);
        
#if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
        Desktop::getInstance().setKioskModeComponent (nullptr, false);
        Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
#endif

        if (auto * dev = mainWindow->getDeviceManager().getCurrentAudioDevice()) {
            if (!dev->isPlaying()) {
                DBG("dev not playing, restarting");
                mainWindow->getDeviceManager().restartLastAudioDevice();
            }
        }
        else {
            DBG("was not active: restarting");
            mainWindow->getDeviceManager().restartLastAudioDevice();
        }

#if JUCE_ANDROID
        setAndroidForegroundServiceActive(true);
#endif

    }
    
    //==============================================================================
    void systemRequestedQuit() override
    {
        DBG("Requested quit");
        if (mainWindow.get() != nullptr) {
            mainWindow->pluginHolder->savePluginState();
            mainWindow->pluginHolder->saveAudioDeviceState();
        }

        appProperties.saveIfNeeded();

        
        if (mainWindow.get() != nullptr) {
            if (auto * editor = mainWindow->getEditor()) {
                if (auto * sonoeditor = dynamic_cast<SonobusAudioProcessorEditor*>(editor)) {
                    if (!sonoeditor->requestedQuit()) {
                        // they'll handle it
                        return;
                    }
                }
            }
        }
        
        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, []()
            {
                if (auto app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

    void memoryWarningReceived()  override
    {
        DBG("Memory warning");
    }

    void attachGL()
    {
        if (mainWindow) {
#if JUCE_OPENGL  && JUCE_ANDROID
            DBG("activating GL context");
            openGLContext.makeActive();
            DBG("OPEN GL attaching to " << (long) mainWindow.get());
            openGLContext.attachTo (*mainWindow);
#endif
        }
    }

    void detachGL()
    {

    #if JUCE_OPENGL && JUCE_ANDROID
        DBG("About to detach opengl context");
        if (openGLContext.isAttached()) {
            openGLContext.detach();
            DBG("About to deactivate opengl context");
            openGLContext.deactivateCurrentContext();
            DBG("done detach opengl context");
        }
    #endif
    }

protected:

#if JUCE_OPENGL
    OpenGLContext openGLContext;
#endif

    ApplicationProperties appProperties;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;

    // used only in headless mode
    std::unique_ptr<StandalonePluginHolder> pluginHolder;

};

} // namespace juce

#if JucePlugin_Build_Standalone && JUCE_IOS

using namespace juce;

bool JUCE_CALLTYPE juce_isInterAppAudioConnected()
{
    if (auto holder = StandalonePluginHolder::getInstance())
        return holder->isInterAppAudioConnected();

    return false;
}

void JUCE_CALLTYPE juce_switchToHostApplication()
{
    if (auto holder = StandalonePluginHolder::getInstance())
        holder->switchToHostApplication();
}

#if JUCE_MODULE_AVAILABLE_juce_gui_basics
Image JUCE_CALLTYPE juce_getIAAHostIcon (int size)
{
    if (auto holder = StandalonePluginHolder::getInstance())
        return holder->getIAAHostIcon (size);

    return Image();
}
#endif
#endif

START_JUCE_APPLICATION (SonobusStandaloneFilterApp);

//#endif
