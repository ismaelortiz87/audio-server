
#pragma once

#include "JuceHeader.h"

#if JUCE_ANDROID


#include "AppConfig.h"

#include "juce_core/native/juce_BasicNativeHeaders.h"

#include "juce_core/juce_core.h"
#include "juce_core/native/juce_JNIHelpers_android.h"

#include "AndroidPlatform.h"

class SonoBusActivity
{
public:
    SonoBusActivity(jobject javaObject)
    {
        auto* env = getEnv();
        
        javaCounterpartInstance = env->NewWeakGlobalRef(javaObject);
        env->SetLongField (javaObject, SonoBusActivityJavaClass.cppCounterpartInstance,
                           reinterpret_cast<jlong> (this));
        
        // initialise the JUCE message manager!
        MessageManager::getInstance();
        
        DBG("SonoBusActivity C++ constructed");
    }

    ~SonoBusActivity()
    {
        auto* env = getEnv();
        
        {
            LocalRef<jobject> javaThis (env->NewLocalRef (javaCounterpartInstance));
            
            if (javaThis != nullptr)
                env->SetLongField (javaThis.get(), SonoBusActivityJavaClass.cppCounterpartInstance, 0);
        }
        
        env->DeleteWeakGlobalRef(javaCounterpartInstance);
    }
    
    static jobject gAssetManager;
    static String gPendingImportFile;
    
private:

 //==============================================================================
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    FIELD    (cppCounterpartInstance,    "cppCounterpartInstance", "J") \
    CALLBACK (constructNativeClassJni,   "constructNativeClass",   "()V") \
    CALLBACK (destroyNativeClassJni,     "destroyNativeClass",     "()V") \
    CALLBACK (nativePlatformSignalsJni,  "nativePlatformSignals",  "(ZZ)V") \
    CALLBACK (nativePhoneCallJni,        "nativePhoneCall",        "(Z)V") \
    CALLBACK (nativeNotificationActionJni, "nativeNotificationAction", "(I)V") \
    CALLBACK (nativePlayerButtonJni,     "nativePlayerButton",     "(I)V") \
    CALLBACK (nativeSelectStationJni,    "nativeSelectStation",    "(Ljava/lang/String;)V") \
    CALLBACK (nativeShutdownJni,         "nativeShutdown",         "()V") \
    CALLBACK (nativeBtRouteJni,          "nativeBtRoute",          "(Ljava/lang/String;Z)V") \
    CALLBACK (nativeBtDeviceJni,         "nativeBtDevice",         "(Ljava/lang/String;Z)V") \
    CALLBACK (nativeBtMicSettingJni,     "nativeBtMicSetting",     "()Ljava/lang/String;") \
    
    DECLARE_JNI_CLASS (SonoBusActivityJavaClass, JUCE_ANDROID_ACTIVITY_OVERRIDE_CLASSPATH)
#undef JNI_CLASS_MEMBERS


 static SonoBusActivity* getCppInstance (JNIEnv* env, jobject javaInstance)
    {
        // always call JUCE::initialiseJUCEThread in java callbacks
        return reinterpret_cast<SonoBusActivity*> (env->GetLongField (javaInstance,
                                                                                SonoBusActivityJavaClass.cppCounterpartInstance));
    }
    
    static void JNIEXPORT constructNativeClassJni (JNIEnv* env, jobject javaInstance)
    {
        initialiseJuce_GUI();
        new SonoBusActivity (javaInstance);
    }
    
    static void JNIEXPORT destroyNativeClassJni (JNIEnv* env, jobject javaInstance)
    {
        if (auto* myself = getCppInstance (env, javaInstance))
            delete myself;
    }

    // A10 / A14: static natives (PlatformMonitor, the service's notification actions). They
    // only record what happened; the AndroidServiceController acts on it on the message thread.
    static void JNIEXPORT nativePlatformSignalsJni (JNIEnv*, jclass, jboolean notifications, jboolean restricted)
    {
        auto& sig = crosspoint::android::Signals::get();
        sig.notificationsEnabled.store (notifications != JNI_FALSE);
        sig.backgroundRestricted.store (restricted != JNI_FALSE);
    }

    static void JNIEXPORT nativePhoneCallJni (JNIEnv*, jclass, jboolean inCall)
    {
        crosspoint::android::Signals::get().phoneCall.store (inCall != JNI_FALSE);
    }

    static void JNIEXPORT nativeNotificationActionJni (JNIEnv*, jclass, jint action)
    {
        crosspoint::android::Signals::get().pendingAction.store ((int) action);
    }

    /** A17: a lock-screen player button. Recorded here; the controller runs it on
        the message thread through the same command paths the control API uses. */
    static void JNIEXPORT nativePlayerButtonJni (JNIEnv*, jclass, jint button)
    {
        crosspoint::android::Signals::get().pendingPlayerButton.store ((int) button);
    }

    /** A20 (section 13.1): the car's browse list selected a station. Like
        nativeShutdown, this runs on the Android main thread (= JUCE's message
        thread), so it calls straight through instead of being polled. */
    static void JNIEXPORT nativeSelectStationJni (JNIEnv* env, jclass, jstring station)
    {
        auto& sig = crosspoint::android::Signals::get();
        if (sig.onSelectStation && station != nullptr)
            sig.onSelectStation (juceString (env, station));
    }

    // Swipe-away: runs on the Android main thread, which is JUCE's message thread.
    static void JNIEXPORT nativeShutdownJni (JNIEnv*, jclass)
    {
        auto& sig = crosspoint::android::Signals::get();
        sig.shutdownRequested.store (true);
        if (sig.onShutdown) sig.onShutdown();
    }

    /** A22 (android.md 14): the Bluetooth route changed. `name` is the connected
        headset/car (null when none); `stereo` is true for LE Audio (stereo + mic
        together, placement stays on) and false for the call/SCO route (both ears
        get the same mix). Recorded here; EngineState turns it into
        platform.bluetoothRoute. */
    static void JNIEXPORT nativeBtRouteJni (JNIEnv* env, jclass, jstring name, jboolean stereo)
    {
        auto& sig = crosspoint::android::Signals::get();
        std::lock_guard<std::mutex> lock (sig.btRouteNameLock);
        if (name == nullptr)
        {
            sig.btRoute.store (false);
            sig.btRouteStereo.store (false);
            sig.btRouteName.clear();
        }
        else
        {
            sig.btRoute.store (true);
            sig.btRouteStereo.store (stereo != JNI_FALSE);
            sig.btRouteName = juceString (env, name).toStdString();
        }
    }

    /** A22 (android.md 14): a Bluetooth headset/car is connected (or was
        disconnected). Independent of the "Bluetooth mic" setting: this is what
        makes the Settings row available, while nativeBtRoute above reports only
        the route actually taken. `le` is true for LE Audio. */
    static void JNIEXPORT nativeBtDeviceJni (JNIEnv* env, jclass, jstring name, jboolean le)
    {
        auto& sig = crosspoint::android::Signals::get();
        std::lock_guard<std::mutex> lock (sig.btDeviceNameLock);
        if (name == nullptr)
        {
            sig.btDevice.store (false);
            sig.btDeviceLe.store (false);
            sig.btDeviceName.clear();
        }
        else
        {
            sig.btDevice.store (true);
            sig.btDeviceLe.store (le != JNI_FALSE);
            sig.btDeviceName = juceString (env, name).toStdString();
        }
    }

    /** A22 (android.md 14): the current "Bluetooth mic" setting, read by the Java
        route controller on its main-looper tick. The controller's own tick mirrors
        the processor value into the Signals global on the message thread, so this
        needs no engine handle and is safe before the engine exists (returns the
        default "phone" until the first tick). */
    static jstring JNIEXPORT nativeBtMicSettingJni (JNIEnv* env, jclass)
    {
        const bool headset = crosspoint::android::Signals::get().btMic.load() == 1;
        // A new local jstring; the Java caller owns it (do not wrap in LocalRef,
        // whose destructor would delete the very reference we return).
        return env->NewStringUTF (headset ? "headset" : "phone");
    }

 
    //==============================================================================
    jweak javaCounterpartInstance = nullptr;
    
};


namespace juce {

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
    METHOD (updateConnectionNotification,   "updateConnectionNotification", "(ZLjava/lang/String;Ljava/lang/String;ZI)V") \
    METHOD (updatePlayerState,              "updatePlayerState", "(Ljava/lang/String;)V") \
    METHOD (requestNotificationPermission,  "requestNotificationPermission", "()V") \
    METHOD (micPermissionState,             "micPermissionState", "()I") \
    METHOD (deviceName,                     "deviceName", "()Ljava/lang/String;") \
    METHOD (markMicAsked,                   "markMicAsked", "()V") \
    METHOD (openSettingsPage,               "openSettingsPage", "(Ljava/lang/String;)Z") \
    METHOD (allowBackground,                "allowBackground", "()Z")

    DECLARE_JNI_CLASS (SonoBusActivity, JUCE_ANDROID_ACTIVITY_OVERRIDE_CLASSPATH);
#undef JNI_CLASS_MEMBERS

// Calls into the activity (A10). Message thread. Defined here, inside juce::, where
// `SonoBusActivity` is unambiguously the JNI class above.
inline void androidUpdateConnectionNotification (bool active, const String& title, const String& text, bool live, int micAction)
{
    LocalRef<jobject> activity (getMainActivity());
    if (activity == nullptr) return;
    auto* env = getEnv();
    LocalRef<jstring> jTitle (javaString (title)), jText (javaString (text));
    env->CallVoidMethod (activity.get(), SonoBusActivity.updateConnectionNotification,
                         (jboolean) active, jTitle.get(), jText.get(), (jboolean) live, (jint) micAction);
}

/** A17/A18: push the player's state to Java as one JSON object (section 12.4): the
    selected station's short name, up to 4 stations, the connection, listening.
    Called only when it changes. */
inline void androidUpdatePlayerState (const String& json)
{
    LocalRef<jobject> activity (getMainActivity());
    if (activity == nullptr) return;
    auto* env = getEnv();
    LocalRef<jstring> jJson (javaString (json));
    env->CallVoidMethod (activity.get(), SonoBusActivity.updatePlayerState, jJson.get());
}

/** android.md section 3: notifications are asked right after the mic answer (Android 13+). */
inline void androidRequestNotificationPermission()
{
    LocalRef<jobject> activity (getMainActivity());
    if (activity != nullptr) getEnv()->CallVoidMethod (activity.get(), SonoBusActivity.requestNotificationPermission);
}

/** The phone's name for "Your name" (Settings > About phone > Device name, else manufacturer and model). */
inline String androidDeviceName()
{
    LocalRef<jobject> activity (getMainActivity());
    if (activity == nullptr) return {};
    LocalRef<jstring> s ((jstring) getEnv()->CallObjectMethod (activity.get(), SonoBusActivity.deviceName));
    return s != nullptr ? juceString (s.get()) : String();
}

/** A9: 0 granted, 1 prompt (never asked), 3 prompt (asked, refused once), 2 denied (only Android Settings can change it). */
inline int androidMicPermissionState()
{
    LocalRef<jobject> activity (getMainActivity());
    if (activity == nullptr) return 1;
    return (int) getEnv()->CallIntMethod (activity.get(), SonoBusActivity.micPermissionState);
}

/** A9: remember that the system dialog was shown, so a later refusal reads as "denied" (rationale semantics). */
inline void androidMarkMicAsked()
{
    LocalRef<jobject> activity (getMainActivity());
    if (activity != nullptr) getEnv()->CallVoidMethod (activity.get(), SonoBusActivity.markMicAsked);
}

inline bool androidOpenSettingsPage (const String& page)
{
    LocalRef<jobject> activity (getMainActivity());
    if (activity == nullptr) return false;
    LocalRef<jstring> jPage (javaString (page));
    return getEnv()->CallBooleanMethod (activity.get(), SonoBusActivity.openSettingsPage, jPage.get()) != JNI_FALSE;
}

inline bool androidAllowBackground()
{
    LocalRef<jobject> activity (getMainActivity());
    return activity != nullptr && getEnv()->CallBooleanMethod (activity.get(), SonoBusActivity.allowBackground) != JNI_FALSE;
}


}


#endif
