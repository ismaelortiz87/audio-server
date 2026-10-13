// SPDX-License-Identifier: GPLv3-or-later

package io.lagreca.app.crosspoint;

import android.app.ActivityManager;
import android.app.usage.UsageStatsManager;
import android.content.Context;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;

/**
 * A10 / A14: watches what the engine needs to know about the phone and reports
 * changes (and only changes) to native:
 *  - a phone or VoIP call (A14, android.md section 6),
 *  - whether notifications are blocked and whether Android restricts the app in
 *    the background (A10, android.md section 7).
 * Polling on the main looper: cheap calls, and the process is awake anyway
 * while connected (wake lock). No permission is needed for any of them.
 */
final class PlatformMonitor {

    private static final int CALL_POLL_MS = 500;
    private static final int SETTINGS_POLL_EVERY = 4;     // every 2 s

    /**
     * A8 hint: set true while the app itself puts AudioManager into
     * MODE_IN_COMMUNICATION (e.g. for Bluetooth SCO), so that is not mistaken for
     * a VoIP call.
     */
    static volatile boolean appOwnsAudioMode = false;

    private static Context sApp;
    private static Handler sHandler;
    private static int sTick = 0;
    private static int sCall = -1, sNotif = -1, sBg = -1;     // last values sent (-1 = never)

    static synchronized void start(Context ctx) {
        if (sApp != null) return;
        sApp = ctx.getApplicationContext();
        sHandler = new Handler(Looper.getMainLooper());
        sHandler.post(new Runnable() {
            @Override public void run() {
                tick();
                sHandler.postDelayed(this, CALL_POLL_MS);
            }
        });
    }

    /** Re-read the settings-related state now (the app came back from Android Settings). */
    static void refreshNow() {
        if (sApp == null) return;
        checkSettings();
    }

    private static void tick() {
        checkCall();
        if (sTick++ % SETTINGS_POLL_EVERY == 0) checkSettings();
    }

    // ---------------------------------------------------------------- call

    static boolean inCall(Context ctx) {
        AudioManager am = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
        if (am == null) return false;
        int mode = am.getMode();
        if (mode == AudioManager.MODE_IN_CALL) return true;                    // cellular
        if (Build.VERSION.SDK_INT >= 30 && mode == AudioManager.MODE_CALL_SCREENING) return true;
        // VoIP: another app holds the communication mode (ringing is MODE_RINGTONE and is ignored).
        return mode == AudioManager.MODE_IN_COMMUNICATION && !appOwnsAudioMode;
    }

    private static void checkCall() {
        int now = inCall(sApp) ? 1 : 0;
        if (now != sCall) {
            sCall = now;
            SonoBusActivity.nativePhoneCall(now == 1);
        }
    }

    // ---------------------------------------------------------------- settings

    static boolean notificationsEnabled(Context ctx) {
        try {
            return androidx.core.app.NotificationManagerCompat.from(ctx).areNotificationsEnabled();
        } catch (RuntimeException e) {
            return true;
        }
    }

    /**
     * "Android may stop Crosspoint in the background" (android.md sections 4, 7):
     * the user set the app to Restricted battery use, or the system put it in
     * the restricted standby bucket. Plain "Optimized" is not reported, because
     * a foreground service with its notification is not stopped by it (checked
     * on the emulator, docs/TASKS.md A10).
     */
    static boolean backgroundRestricted(Context ctx) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) return false;
        try {
            ActivityManager am = (ActivityManager) ctx.getSystemService(Context.ACTIVITY_SERVICE);
            if (am != null && am.isBackgroundRestricted()) return true;
            UsageStatsManager usm = (UsageStatsManager) ctx.getSystemService(Context.USAGE_STATS_SERVICE);
            if (usm != null && usm.getAppStandbyBucket() >= UsageStatsManager.STANDBY_BUCKET_RESTRICTED) return true;
        } catch (RuntimeException e) {
            // not reportable: stay calm
        }
        return false;
    }

    private static void checkSettings() {
        int n = notificationsEnabled(sApp) ? 1 : 0;
        int b = backgroundRestricted(sApp) ? 1 : 0;
        if (n != sNotif || b != sBg) {
            sNotif = n;
            sBg = b;
            SonoBusActivity.nativePlatformSignals(n == 1, b == 1);
        }
    }
}
