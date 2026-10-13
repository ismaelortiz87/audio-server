// A8: the phone's audio devices in plain words, and which way the output goes.
// Header-only on purpose (no .jucer entry). The naming half is plain C++ so it
// can be tested on the host (tests/android/a8-device-names.cpp); the JNI half
// (the system's current default route, the platform echo canceller) is Android only.
// Design: docs/design/android.md section 6.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#if JUCE_ANDROID
 #include <jni.h>
 #include "juce_core/native/juce_JNIHelpers_android.h"   // getEnv, LocalRef (not in the public module header)
#endif

namespace crosspoint { namespace androidaudio {

// android.media.AudioDeviceInfo.TYPE_* (the same numbers JUCE's Oboe device list
// turns into the suffix of each device name: "<product name> <type text>").
enum DeviceType
{
    kUnknown = 0, kEarpiece = 1, kSpeaker = 2, kWiredHeadset = 3, kWiredHeadphones = 4,
    kLineAnalog = 5, kLineDigital = 6, kBluetoothSco = 7, kBluetoothA2dp = 8,
    kHdmi = 9, kHdmiArc = 10, kUsbDevice = 11, kUsbAccessory = 12, kDock = 13,
    kBuiltinMic = 15, kTelephony = 18, kAuxLine = 19, kUsbHeadset = 22,
    kHearingAid = 23, kSpeakerSafe = 24, kRemoteSubmix = 25, kBleHeadset = 26,
    kBleSpeaker = 27, kBleBroadcast = 30
};

struct DeviceText { int type; const char* text; };

// Copy of JUCE 7.0.8 juce_Oboe_android.cpp OboeAudioIODeviceType::deviceTypeToString.
inline const std::vector<DeviceText>& juceTypeTexts()
{
    static const std::vector<DeviceText> t = {
        { 1, "built-in earphone speaker" }, { 2, "built-in speaker" }, { 3, "wired headset" },
        { 4, "wired headphones" }, { 5, "line analog" }, { 6, "line digital" },
        { 7, "Bluetooth device typically used for telephony" },
        { 8, "Bluetooth device supporting the A2DP profile" }, { 9, "HDMI" },
        { 10, "HDMI audio return channel" }, { 11, "USB device" }, { 12, "USB accessory" },
        { 13, "DOCK" }, { 14, "FM" }, { 15, "built-in microphone" }, { 16, "FM tuner" },
        { 17, "TV tuner" }, { 18, "telephony" }, { 19, "auxiliary line-level connectors" },
        { 20, "IP" }, { 21, "BUS" }, { 22, "USB headset" }, { 23, "hearing aid" },
        { 24, "built-in speaker safe" }, { 25, "remote submix" }, { 26, "BLE headset" },
        { 27, "BLE speaker" }, { 28, "echo reference" }, { 29, "HDMI eARC" }, { 30, "BLE broadcast" } };
    return t;
}

inline bool endsWith (const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare (s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/** JUCE's "System Default (Input)" / "System Default (Output)": no device id, so
    Android's own routing decides (and moves it when headphones come and go). */
inline bool isSystemDefault (const std::string& juceName)
{
    return juceName.rfind ("System Default", 0) == 0;
}

/** The AudioDeviceInfo type behind a JUCE device name, and the product name
    (the name minus the type text). 0 when the name carries no known type. */
inline int typeFromJuceName (const std::string& juceName, std::string* product = nullptr)
{
    int best = 0;
    size_t bestLen = 0;
    for (auto& t : juceTypeTexts())
    {
        const std::string suffix = std::string (" ") + t.text;
        if (suffix.size() > bestLen && endsWith (juceName, suffix))   // longest suffix wins ("USB headset" vs "headset")
        {
            best = t.type;
            bestLen = suffix.size();
        }
    }
    if (product != nullptr)
        *product = bestLen > 0 ? juceName.substr (0, juceName.size() - bestLen) : juceName;
    return best;
}

/** "speaker" | "earpiece" | "wired" | "bluetooth" | "usb" (control-api 5.5). */
inline const char* routeOf (int type)
{
    switch (type)
    {
        case kEarpiece: return "earpiece";
        case kWiredHeadset: case kWiredHeadphones: case kLineAnalog: case kLineDigital: case kAuxLine:
        case kHdmi: case kHdmiArc: case 29: return "wired";
        case kBluetoothSco: case kBluetoothA2dp: case kHearingAid: case kBleHeadset: case kBleSpeaker: case kBleBroadcast:
            return "bluetooth";
        case kUsbDevice: case kUsbAccessory: case kUsbHeadset: case kDock: return "usb";
        default: return "speaker";   // built-in speaker, and anything unknown: the safe assumption
    }
}

/** Devices the pickers should not offer: not something a person can plug in or
    talk into (remote submix, telephony, tuners, echo reference...), or an output
    the design has no word for (HDMI). */
inline bool isHidden (int type, bool input)
{
    if (type == 0) return true;
    if (input)
        return !(type == kBuiltinMic || type == kWiredHeadset || type == kLineAnalog || type == kLineDigital ||
                 type == kAuxLine || type == kBluetoothSco || type == kBleHeadset || type == kUsbDevice ||
                 type == kUsbAccessory || type == kUsbHeadset || type == kDock || type == kHearingAid);
    return !(type == kEarpiece || type == kSpeaker || type == kSpeakerSafe || type == kWiredHeadset ||
             type == kWiredHeadphones || type == kLineAnalog || type == kLineDigital || type == kAuxLine ||
             type == kBluetoothSco || type == kBluetoothA2dp || type == kBleHeadset || type == kBleSpeaker ||
             type == kUsbDevice || type == kUsbAccessory || type == kUsbHeadset || type == kDock || type == kHearingAid);
}

/** The label from android.md section 6. `product` is the device's own name. */
inline std::string labelFor (int type, const std::string& product, bool input)
{
    const std::string route = routeOf (type);
    const auto named = [&] (const char* prefix)
    {
        return product.empty() ? std::string (prefix) : std::string (prefix) + " \xC2\xB7 " + product;   // "Bluetooth · name"
    };
    if (route == "bluetooth") return named ("Bluetooth");
    if (route == "usb")       return named ("USB");
    if (input)                return type == kBuiltinMic ? "Phone mic" : "Headset mic";
    if (route == "earpiece")  return "Earpiece";
    if (route == "wired")     return "Wired headphones";
    return "Phone speaker";
}

/** Label of a JUCE device name; for "System Default" the caller passes the type
    of the device Android currently routes to (0 = unknown, then the phone's own). */
inline std::string labelForJuceName (const std::string& juceName, bool input, int defaultType = 0)
{
    std::string product;
    int type = isSystemDefault (juceName) ? defaultType : typeFromJuceName (juceName, &product);
    if (type == 0) type = input ? kBuiltinMic : kSpeaker;
    return labelFor (type, product, input);
}

/** Higher is better when two devices get the same label (a headset shows up
    twice as an output, A2DP for music and SCO for calls: A2DP is the one we want). */
inline int preference (int type)
{
    return type == kBluetoothA2dp || type == kSpeaker || type == kBuiltinMic ? 1 : 0;
}

/** The echo notice's condition (android.md section 10). `aecWorking`: the
    platform echo canceller is on the mic path. `anyHearsYou`: your voice reaches at
    least one station (android.md section 6), not just "the mic is open". */
inline bool echoRisk (const char* route, bool aecWorking, bool micTransmitting, bool anyHearsYou)
{
    return std::string (route) == "speaker" && ! aecWorking && micTransmitting && anyHearsYou;
}

}} // namespace crosspoint::androidaudio

//==============================================================================
#if JUCE_ANDROID

namespace crosspoint { namespace androidaudio {

namespace detail
{
    inline bool failed (JNIEnv* env)
    {
        if (env->ExceptionCheck()) { env->ExceptionClear(); return true; }
        return false;
    }

    inline juce::LocalRef<jobject> audioManager (JNIEnv* env)
    {
        using namespace juce;
        auto context = getAppContext();
        if (context.get() == nullptr) return {};
        auto contextClass = LocalRef<jclass> (env->FindClass ("android/content/Context"));
        auto getSystemService = env->GetMethodID (contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        LocalRef<jobject> am (env->CallObjectMethod (context.get(), getSystemService, javaString ("audio").get()));
        if (failed (env)) return {};
        return am;
    }

    /** How much Android prefers a connected device for the default route. */
    inline int rank (int type, bool input)
    {
        switch (type)
        {
            case kWiredHeadset: case kWiredHeadphones: case kLineAnalog: case kLineDigital:
            case kAuxLine: case kUsbHeadset: case kUsbDevice: case kUsbAccessory: return 4;
            case kBluetoothA2dp: case kBleHeadset: case kBleSpeaker: return input ? 0 : 3;   // a mic over Bluetooth needs SCO, never the default
            case kBuiltinMic: case kSpeaker: return 1;
            default: return 0;
        }
    }
}

/** The AudioDeviceInfo.TYPE_* of the device Android routes to when a stream
    names no device ("System Default"). API 33+: the system's own answer for
    media output (getAudioDevicesForAttributes). Otherwise, and for the mic, the
    usual priority: wired / USB headset, then (output only) Bluetooth, then the
    phone's own. 0 when it cannot be told. Message thread; a few JNI calls. */
inline int currentDefaultType (bool input)
{
    using namespace juce;
    auto* env = getEnv();
    if (env == nullptr) return 0;
    auto am = detail::audioManager (env);
    if (am.get() == nullptr) return 0;

    auto amClass = LocalRef<jclass> (env->FindClass ("android/media/AudioManager"));
    auto infoClass = LocalRef<jclass> (env->FindClass ("android/media/AudioDeviceInfo"));
    const auto getType = env->GetMethodID (infoClass, "getType", "()I");

    if (! input && getAndroidSDKVersion() >= 33)
    {
        auto builderClass = LocalRef<jclass> (env->FindClass ("android/media/AudioAttributes$Builder"));
        auto builder = LocalRef<jobject> (env->NewObject (builderClass, env->GetMethodID (builderClass, "<init>", "()V")));
        env->CallObjectMethod (builder, env->GetMethodID (builderClass, "setUsage", "(I)Landroid/media/AudioAttributes$Builder;"), 1);   // USAGE_MEDIA
        auto attrs = LocalRef<jobject> (env->CallObjectMethod (builder, env->GetMethodID (builderClass, "build", "()Landroid/media/AudioAttributes;")));
        if (! detail::failed (env) && attrs.get() != nullptr)
        {
            auto list = LocalRef<jobject> (env->CallObjectMethod (am, env->GetMethodID (amClass, "getAudioDevicesForAttributes",
                                           "(Landroid/media/AudioAttributes;)Ljava/util/List;"), attrs.get()));
            if (! detail::failed (env) && list.get() != nullptr)
            {
                auto listClass = LocalRef<jclass> (env->FindClass ("java/util/List"));
                const jint n = env->CallIntMethod (list, env->GetMethodID (listClass, "size", "()I"));
                if (n > 0)
                {
                    auto first = LocalRef<jobject> (env->CallObjectMethod (list, env->GetMethodID (listClass, "get", "(I)Ljava/lang/Object;"), 0));
                    const jint t = env->CallIntMethod (first, getType);
                    if (! detail::failed (env)) return (int) t;
                }
            }
        }
    }

    auto devices = LocalRef<jobjectArray> ((jobjectArray) env->CallObjectMethod (am, env->GetMethodID (amClass, "getDevices", "(I)[Landroid/media/AudioDeviceInfo;"),
                                                                                 input ? 1 : 2));   // GET_DEVICES_INPUTS / OUTPUTS
    if (detail::failed (env) || devices.get() == nullptr) return 0;
    int best = 0, bestRank = 0;
    const jsize n = env->GetArrayLength (devices);
    for (jsize i = 0; i < n; ++i)
    {
        auto d = LocalRef<jobject> (env->GetObjectArrayElement (devices, i));
        const int t = (int) env->CallIntMethod (d, getType);
        const int r = detail::rank (t, input);
        if (r > bestRank) { best = t; bestRank = r; }
    }
    return best;
}

/** True when the platform has an acoustic echo canceller for the mic path
    (AcousticEchoCanceler.isAvailable()). It says the effect exists, not that it
    works well; A13 checks that by ear. */
inline bool platformEchoCancellerAvailable()
{
    using namespace juce;
    auto* env = getEnv();
    if (env == nullptr) return false;
    auto cls = LocalRef<jclass> (env->FindClass ("android/media/audiofx/AcousticEchoCanceler"));
    if (detail::failed (env) || cls.get() == nullptr) return false;
    const bool ok = env->CallStaticBooleanMethod (cls, env->GetStaticMethodID (cls, "isAvailable", "()Z")) != JNI_FALSE;
    return ! detail::failed (env) && ok;
}

}} // namespace

#endif
