// A7: JNI helpers for the Android web shell (declared in ConsoleShell.h). Include once, in
// SonoStandaloneFilterApp.cpp, after JUCE's JNI helpers are available.
#pragma once

#include "ConsoleShell.h"

#if JUCE_ANDROID
namespace crosspoint_android
{
    // JUCE 7.0.8's WebBrowserComponent hides its android.webkit.WebView, and has neither an
    // evaluateJavascript nor a JS bridge, so the few native things the Console shell needs
    // (android.md section 2) go through JNI on the view found in the activity's view tree.
    /** Paints the web view `bg` before any page does (never a white frame). True once found. */
    bool paintWebViewBackground (void* webView, uint32 argb)
    {
        auto* env = getEnv();
        // Only when a marker file exists in the app's data dir (created over adb with run-as,
        // which needs a debuggable build): lets chrome://inspect reach the web view for A7 checks.
        if (File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("webdebug").exists())
        {
            jclass wv = env->FindClass ("android/webkit/WebView");
            env->CallStaticVoidMethod (wv, env->GetStaticMethodID (wv, "setWebContentsDebuggingEnabled", "(Z)V"), JNI_TRUE);
            env->DeleteLocalRef (wv);
        }
        LocalRef<jobject> view (webView != nullptr ? env->NewLocalRef ((jobject) webView) : nullptr);
        if (view.get() == nullptr)
            return false;
        jclass cls = env->GetObjectClass (view.get());
        env->CallVoidMethod (view.get(), env->GetMethodID (cls, "setBackgroundColor", "(I)V"), (jint) argb);
        env->DeleteLocalRef (cls);
        return true;
    }

    /** android.md section 2: both system bars `rail`. The theme says so, but SonoBusActivity.onCreate
        sets the navigation bar black afterwards, so it is set again here, once the shell is up. */
    void setSystemBarColours (uint32 statusArgb, uint32 navigationArgb)
    {
        auto* env = getEnv();
        auto activity = getMainActivity();
        if (env == nullptr || activity.get() == nullptr)
            return;
        jclass activityClass = env->GetObjectClass (activity.get());
        LocalRef<jobject> window (env->CallObjectMethod (activity.get(), env->GetMethodID (activityClass, "getWindow", "()Landroid/view/Window;")));
        env->DeleteLocalRef (activityClass);
        if (window.get() == nullptr)
            return;
        jclass windowClass = env->GetObjectClass (window.get());
        env->CallVoidMethod (window.get(), env->GetMethodID (windowClass, "setStatusBarColor", "(I)V"), (jint) statusArgb);
        env->CallVoidMethod (window.get(), env->GetMethodID (windowClass, "setNavigationBarColor", "(I)V"), (jint) navigationArgb);
        env->DeleteLocalRef (windowClass);
    }

    /** android.md section 2: the page honours Android's font size. The activity handles
        fontScale itself (a restart would take the process down), and a running WebView does not
        follow it, so its text zoom is set from the configuration: at creation, and when it changes. */
    void applyFontScale (void* webView)
    {
        auto* env = getEnv();
        auto activity = getMainActivity();
        if (env == nullptr || webView == nullptr || activity.get() == nullptr)
            return;
        LocalRef<jobject> view (env->NewLocalRef ((jobject) webView));

        jclass activityClass = env->GetObjectClass (activity.get());
        LocalRef<jobject> resources (env->CallObjectMethod (activity.get(), env->GetMethodID (activityClass, "getResources", "()Landroid/content/res/Resources;")));
        env->DeleteLocalRef (activityClass);
        jclass resourcesClass = env->GetObjectClass (resources.get());
        LocalRef<jobject> config (env->CallObjectMethod (resources.get(), env->GetMethodID (resourcesClass, "getConfiguration", "()Landroid/content/res/Configuration;")));
        env->DeleteLocalRef (resourcesClass);
        jclass configClass = env->GetObjectClass (config.get());
        const float fontScale = env->GetFloatField (config.get(), env->GetFieldID (configClass, "fontScale", "F"));
        env->DeleteLocalRef (configClass);

        jclass viewClass = env->GetObjectClass (view.get());
        LocalRef<jobject> settings (env->CallObjectMethod (view.get(), env->GetMethodID (viewClass, "getSettings", "()Landroid/webkit/WebSettings;")));
        env->DeleteLocalRef (viewClass);
        jclass settingsClass = env->GetObjectClass (settings.get());
        jmethodID getZoom = env->GetMethodID (settingsClass, "getTextZoom", "()I");
        const int wanted = jlimit (50, 300, roundToInt (fontScale * 100.0f));
        if (env->CallIntMethod (settings.get(), getZoom) != wanted)
            env->CallVoidMethod (settings.get(), env->GetMethodID (settingsClass, "setTextZoom", "(I)V"), (jint) wanted);
        env->DeleteLocalRef (settingsClass);
    }

    /** View.performHapticFeedback: honours the system "touch feedback" setting, needs no permission. */
    void haptic (void* webView, bool starting)
    {
        auto* env = getEnv();
        LocalRef<jobject> view (webView != nullptr ? env->NewLocalRef ((jobject) webView) : nullptr);
        if (view.get() == nullptr)
            return;
        enum { KEYBOARD_TAP = 3, CLOCK_TICK = 4 };
        jclass cls = env->GetObjectClass (view.get());
        env->CallBooleanMethod (view.get(), env->GetMethodID (cls, "performHapticFeedback", "(I)Z"),
                                (jint) (starting ? KEYBOARD_TAP : CLOCK_TICK));
        env->DeleteLocalRef (cls);
    }

    /** Same as pressing Home; the foreground service keeps the audio. Never disconnects. */
    void moveTaskToBack()
    {
        auto* env = getEnv();
        auto activity = getCurrentActivity();
        if (env != nullptr && activity.get() != nullptr)
        {
            jclass cls = env->GetObjectClass (activity.get());
            env->CallBooleanMethod (activity.get(), env->GetMethodID (cls, "moveTaskToBack", "(Z)Z"), JNI_TRUE);
            env->DeleteLocalRef (cls);
        }
    }
}
#endif
