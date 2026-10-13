// SPDX-License-Identifier: GPLv3-or-later
// Copyright (C) 2020 Jesse Chappell


package io.lagreca.app.crosspoint;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.view.Window;
import android.view.WindowManager;


//==============================================================================
public class SonoBusActivity   extends Activity
{
    //==============================================================================
    private native void appNewIntent (Intent intent);
    private native void appOnResume();

    // A10 / A14: static natives registered by SonoBusActivity.h. They only store
    // values or queue work for the engine, so they are safe before it is up.
    static native void nativePlatformSignals(boolean notificationsEnabled, boolean backgroundRestricted);
    static native void nativePhoneCall(boolean inCall);
    static native void nativeNotificationAction(int action);   // 1 = Mic off/on, 2 = Disconnect
    static native void nativeShutdown();                       // swipe-away: leave the group, save state

    private final static String TAG = "Crosspoint";

    //==============================================================================
    private native void constructNativeClass();
    private native void destroyNativeClass();

    private long cppCounterpartInstance;

    private static boolean sQuitting = false;

    @Override
    public final void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        // call the native C++ class constructor
        constructNativeClass();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            Window window = getWindow();
            window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
            window.setNavigationBarColor(Color.BLACK);
        }
    }

    @Override
    protected final void onStart()
    {
        super.onStart();
        PlatformMonitor.start(this);
    }

    @Override
    protected final void onResume()
    {
        super.onResume();
        PlatformMonitor.refreshNow();      // back from Android Settings: notifications / battery may have changed
    }

    @Override
    protected final void onDestroy()
    {
        if (BuildConfig.DEBUG) Log.d(TAG, "onDestroy, finishing=" + isFinishing());

        // call the native C++ class destructor
        destroyNativeClass();

        super.onDestroy();

        // Swiped away from Recents (or finished): disconnect and quit (android.md
        // section 1). Back and Home never get here: Back is turned into Home.
        if (isFinishing()) quitApp(this);
    }

    /**
     * Swipe-away: leave the group, remove the notification and end the process,
     * so a live mic can never keep running without any visible app.
     */
    static void quitApp(final Context ctx)
    {
        if (sQuitting) return;
        sQuitting = true;
        try { nativeShutdown(); } catch (Throwable t) { Log.w(TAG, "native shutdown failed", t); }
        SonoBusService.apply(ctx, false, "", "", false, 0);
        // Let the leave messages leave the socket before the process goes.
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override public void run() { android.os.Process.killProcess(android.os.Process.myPid()); }
        }, 500);
    }

    // ---- called from native (SonoBusActivity.h) -------------------------------------------

    /** Title, text, tint and the Mic action come from ConnectionNotification.h. micAction: 0 none, 1 "Mic off", 2 "Mic on". */
    public final void updateConnectionNotification(boolean active, String title, String text, boolean live, int micAction)
    {
        if (sQuitting) return;
        SonoBusService.apply(this, active, title, text, live, micAction);
    }

    /** android.md section 3: ask for notifications (Android 13+) right after the mic answer. Asked once. */
    public final void requestNotificationPermission()
    {
        if (Build.VERSION.SDK_INT < 33) return;
        if (checkSelfPermission("android.permission.POST_NOTIFICATIONS") == PackageManager.PERMISSION_GRANTED) return;
        SharedPreferences prefs = getSharedPreferences("crosspoint_platform", MODE_PRIVATE);
        if (prefs.getBoolean("asked_notifications", false)) return;
        prefs.edit().putBoolean("asked_notifications", true).apply();
        requestPermissions(new String[] { "android.permission.POST_NOTIFICATIONS" }, 7001);
    }

    /** Default "Your name" (android.md section 2): the device name the user set, else "Samsung SM-S911B". */
    public final String deviceName()
    {
        String n = null;
        try { n = Settings.Global.getString(getContentResolver(), "device_name"); } catch (RuntimeException e) { /* fall through */ }
        if (n == null || n.trim().isEmpty()) n = Settings.Secure.getString(getContentResolver(), "bluetooth_name");
        if (n == null || n.trim().isEmpty()) {
            String m = Build.MANUFACTURER == null ? "" : Build.MANUFACTURER, model = Build.MODEL == null ? "" : Build.MODEL;
            if (!m.isEmpty()) m = m.substring(0, 1).toUpperCase() + m.substring(1);
            n = model.toLowerCase().startsWith(m.toLowerCase()) ? model : (m + " " + model);
        }
        return n.trim();
    }

    private static final String MIC_PERMISSION = "android.permission.RECORD_AUDIO";

    /**
     * android.md section 3: 0 granted, 1 prompt (never asked), 3 prompt (asked and
     * refused once; the dialog can still be shown), 2 denied (only Android Settings can change it). "Denied" is the
     * rationale semantics: we asked once and the system will not ask again.
     */
    public final int micPermissionState()
    {
        if (Build.VERSION.SDK_INT < 23 || checkSelfPermission(MIC_PERMISSION) == PackageManager.PERMISSION_GRANTED) return 0;
        boolean asked = getSharedPreferences("crosspoint_platform", MODE_PRIVATE).getBoolean("asked_mic", false);
        if (!asked) return 1;
        return shouldShowRequestPermissionRationale(MIC_PERMISSION) ? 3 : 2;
    }

    /** The system dialog is about to be shown (asked once automatically; the user can still tap the mic button). */
    public final void markMicAsked()
    {
        getSharedPreferences("crosspoint_platform", MODE_PRIVATE).edit().putBoolean("asked_mic", true).apply();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults)
    {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == 7001) PlatformMonitor.refreshNow();
    }

    /** platform.openSettings { page: app | notifications | battery } */
    public final boolean openSettingsPage(String page)
    {
        try {
            Intent i;
            if ("notifications".equals(page) && Build.VERSION.SDK_INT >= 26) {
                i = new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
                        .putExtra(Settings.EXTRA_APP_PACKAGE, getPackageName());
            } else {
                // "battery" too: Restricted / Optimized is chosen on the app's own page.
                i = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + getPackageName()));
            }
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
            return true;
        } catch (RuntimeException e) {
            Log.w(TAG, "could not open settings page " + page, e);
            return false;
        }
    }

    /** platform.allowBackground: the system's "let it run in the background" dialog. */
    public final boolean allowBackground()
    {
        try {
            Intent i = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:" + getPackageName()));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
            return true;
        } catch (RuntimeException e) {
            return openSettingsPage("battery");
        }
    }

    @Override
    protected void onNewIntent (Intent intent)
    {
        super.onNewIntent(intent);
        setIntent(intent);

        appNewIntent (intent);
    }
}
