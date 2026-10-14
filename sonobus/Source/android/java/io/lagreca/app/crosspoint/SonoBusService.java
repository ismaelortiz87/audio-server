// SPDX-License-Identifier: GPLv3-or-later
// Copyright (C) 2020 Jesse Chappell

package io.lagreca.app.crosspoint;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import androidx.core.app.NotificationCompat;

/**
 * A10 (docs/design/android.md sections 1 and 4): the foreground service that
 * keeps the engine alive from connect until Disconnect or swipe-away.
 *
 * It owns no copy of its own: the native side computes title, text, tint and
 * the Mic action from the engine state (ConnectionNotification.h) and pushes
 * them through SonoBusActivity.updateConnectionNotification() -> apply().
 * The service only draws them, holds the Wi-Fi and wake locks while it runs,
 * and reports the two notification actions and a swipe-away back to native.
 */
public class SonoBusService extends Service {

    private final static String TAG = "Crosspoint";

    public static final String CHANNEL_ID = "connection";
    private static final String OLD_CHANNEL_ID = "foreground_audio_sonobus";
    public static final int NOTIFICATION_ID = 1555;

    static final String ACTION_MIC = "io.lagreca.app.crosspoint.action.MIC";
    static final String ACTION_DISCONNECT = "io.lagreca.app.crosspoint.action.DISCONNECT";
    /** A18: a lock-screen player button; the code travels in the "code" extra. */
    static final String ACTION_MEDIA = "io.lagreca.app.crosspoint.action.MEDIA";

    // android.md section 4: tint accent normally, live while the voice reaches a station.
    private static final int COLOR_ACCENT = 0xFF2E6B66;
    private static final int COLOR_LIVE = 0xFFFF4F7B;

    // The latest content from native (main thread only).
    private static String sTitle = "Crosspoint";
    private static String sText = "";
    private static boolean sLive = false;
    /** The Mic button: 0 hidden, 1 "Mic on" (offers off), 2 "Mic off" (offers on), 3 "Allow mic" (A18). */
    private static int sMicAction = 0;
    /** A17/A18: the player's state as JSON (selected station, stations, connection, listening). */
    private static String sPlayerJson = "{}";
    private static SonoBusService sInstance = null;

    private NotificationManager mNotificationManager;
    private PowerManager.WakeLock mWakeLock;
    private WifiManager.WifiLock mWifiLock, mWifiLockLowLatency;
    private int mForegroundTypes = -1;
    private boolean mStarted = false;
    /** A18 (section 12): the media session behind the lock-screen player. */
    private CrosspointMediaSession mMediaSession;

    // ---------------------------------------------------------------- native -> service

    /** Main thread. Start, update or stop the service from the native state. */
    static void apply(Context ctx, boolean active, String title, String text, boolean live, int micAction) {
        sTitle = title;
        sText = text;
        sLive = live;
        sMicAction = micAction;
        if (!active) {
            if (sInstance != null) sInstance.shutDown();
            return;
        }
        if (sInstance != null && sInstance.mStarted) {
            sInstance.refresh();
        } else {
            Intent intent = new Intent(ctx, SonoBusService.class);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) ctx.startForegroundService(intent);
            else ctx.startService(intent);
        }
    }

    /** A17/A18: the player's state as JSON (main thread). */
    static void applyPlayerState(Context ctx, String json) {
        sPlayerJson = json;
        // A20: the Android Auto browse list reads the same pushed state, so it
        // keeps working while disconnected (section 13.3), when the media session
        // and its notification are gone.
        CrosspointBrowserService.setStateJson(json);
        if (sInstance != null && sInstance.mStarted) sInstance.refresh();
    }

    // ---------------------------------------------------------------- lifecycle

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        // A26: on-phone logs first, so everything the app logs from here on is in the
        // file too. The engine is handed the same folder, so one pull has both halves.
        CrosspointLog.start(this);
        final String logDir = CrosspointLog.path();
        if (logDir != null) {
            try { SonoBusActivity.nativeSetLogDir(logDir); }
            catch (Throwable t) { CrosspointLog.w(TAG, "could not hand the log folder to the engine", t); }
        }
        if (BuildConfig.DEBUG) CrosspointLog.d(TAG, "service created");
        sInstance = this;
        mNotificationManager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        createChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : null;
        if (ACTION_MIC.equals(action)) {
            SonoBusActivity.nativeNotificationAction(1);
        } else if (ACTION_DISCONNECT.equals(action)) {
            SonoBusActivity.nativeNotificationAction(2);
        } else if (ACTION_MEDIA.equals(action) && intent != null) {
            // A18: a player notification action. Separate channel from the legacy
            // 1/2 notification actions, so the controller can tell them apart.
            SonoBusActivity.nativePlayerButton(intent.getIntExtra("code", 0));
        }
        if (!mStarted) {
            mStarted = true;
            acquireLocks();
            startMediaSession();
            refresh();
        }
        // The engine lives in this process: a restart by the system would only
        // bring up a notification for an engine that is gone.
        return START_NOT_STICKY;
    }

    /** A18: create the media session before the first notification, so the
        MediaStyle notification has a session token to attach to. */
    private void startMediaSession() {
        // A20 (section 13.4: one session): the process-shared session is handed to
        // the Android Auto browser, so the car draws this player and its buttons.
        // It is reused across connects so Auto can still browse after Disconnect
        // (section 13.3); only its *active* flag follows the service.
        mMediaSession = CrosspointMediaSession.shared(this, true);
        mMediaSession.setActive(true);
        CrosspointBrowserService.setSession(mMediaSession);
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        // stopWithTask="false" in the manifest, so we get here when the app is
        // swiped away from Recents: disconnect and quit (android.md section 1).
        SonoBusActivity.quitApp(this);
        super.onTaskRemoved(rootIntent);
    }

    @Override
    public void onDestroy() {
        CrosspointLog.cleanExit();     // A26: this run ended on purpose
        releaseLocks();
        // The session itself is kept (the Auto browser still needs the token and
        // the pushed state); only deactivate it, so the lock-screen player goes
        // away on Disconnect as section 12.1 says.
        if (mMediaSession != null) mMediaSession.setActive(false);
        mMediaSession = null;
        if (sInstance == this) sInstance = null;
        mStarted = false;
        if (BuildConfig.DEBUG) CrosspointLog.d(TAG, "service stopped");
        super.onDestroy();
    }

    /** Disconnect / swipe-away: remove the notification and stop. */
    void shutDown() {
        mStarted = false;
        mForegroundTypes = -1;
        stopForeground(true);
        stopSelf();
        mNotificationManager.cancel(NOTIFICATION_ID);
        releaseLocks();
    }

    // ---------------------------------------------------------------- notification

    private void createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        if (mNotificationManager.getNotificationChannel(OLD_CHANNEL_ID) != null)
            mNotificationManager.deleteNotificationChannel(OLD_CHANNEL_ID);
        if (mNotificationManager.getNotificationChannel(CHANNEL_ID) != null) return;
        NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "Connection", NotificationManager.IMPORTANCE_DEFAULT);   // not LOW: Android 12+ hides silent notifications on the lock screen (android.md section 4)
        channel.setDescription("Shows whether your microphone is live while Crosspoint is connected");
        channel.enableVibration(false);
        channel.setSound(null, null);
        channel.setShowBadge(false);
        channel.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
        mNotificationManager.createNotificationChannel(channel);
    }

    private PendingIntent actionIntent(String action, int requestCode) {
        Intent i = new Intent(this, SonoBusService.class).setAction(action);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) flags |= PendingIntent.FLAG_IMMUTABLE;
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
            ? PendingIntent.getForegroundService(this, requestCode, i, flags)
            : PendingIntent.getService(this, requestCode, i, flags);
    }

    private Notification build() {
        Intent open = new Intent(this, SonoBusActivity.class);
        open.setAction(Intent.ACTION_MAIN);
        open.addCategory(Intent.CATEGORY_LAUNCHER);
        open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) flags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent content = PendingIntent.getActivity(this, 0, open, flags);

        // A18 (android.md section 12.1): the MediaStyle notification replaces the
        // section 4 connection notification as the foreground notification (same id,
        // so there is never a second one), so the phone shows the lock-screen player
        // and the system keeps it while the session plays. The plain notification
        // below is only the fallback for the instant before the session exists.
        if (mMediaSession != null && mMediaSession.isRunning()) {
            mMediaSession.updateState(sPlayerJson);
            mMediaSession.updateText(sTitle, sText, sLive, sMicAction);
            return mMediaSession.buildNotification(CHANNEL_ID, content);
        }

        NotificationCompat.Builder b = new NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(sTitle)
            .setContentText(sText)
            .setStyle(new NotificationCompat.BigTextStyle().bigText(sText))
            .setSmallIcon(R.drawable.ic_stat_crosspoint)          // A4: design/icon/crosspoint-notification.svg
            .setColor(sLive ? COLOR_LIVE : COLOR_ACCENT)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .setPriority(NotificationCompat.PRIORITY_DEFAULT)   // pre-Oreo; the channel decides from Android 8
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setContentIntent(content);
        // At most two actions, in this order (android.md section 4).
        if (sMicAction == 1) b.addAction(0, "Mic off", actionIntent(ACTION_MIC, 1));
        else if (sMicAction == 2) b.addAction(0, "Mic on", actionIntent(ACTION_MIC, 1));
        b.addAction(0, "Disconnect", actionIntent(ACTION_DISCONNECT, 2));
        return b.build();
    }

    private int foregroundTypes() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return 0;
        int types = ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK;
        // The microphone type needs RECORD_AUDIO (enforced from Android 14).
        if (checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED)
            types |= ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE;
        return types;
    }

    private void refresh() {
        Notification n = build();
        int types = foregroundTypes();
        if (mForegroundTypes != types) {
            // First call, or the mic permission changed: (re)declare the types.
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) startForeground(NOTIFICATION_ID, n, types);
                else startForeground(NOTIFICATION_ID, n);
                mForegroundTypes = types;
            } catch (RuntimeException e) {
                CrosspointLog.e(TAG, "startForeground failed", e);
                startForeground(NOTIFICATION_ID, n);
                mForegroundTypes = types;
            }
        } else {
            mNotificationManager.notify(NOTIFICATION_ID, n);
        }
    }

    // ---------------------------------------------------------------- locks

    /** Held only while the service runs, i.e. while connected (or trying to). */
    private void acquireLocks() {
        try {
            PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
            mWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "crosspoint:connection");
            mWakeLock.setReferenceCounted(false);
            mWakeLock.acquire();

            WifiManager wm = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            if (wm != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    mWifiLockLowLatency = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "crosspoint:lowlatency");
                    mWifiLockLowLatency.setReferenceCounted(false);
                    mWifiLockLowLatency.acquire();
                }
                if (Build.VERSION.SDK_INT < 34) {
                    // The low-latency mode only applies with the screen on and the app in front; this one
                    // keeps Wi-Fi out of power save while the screen is off.
                    mWifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "crosspoint:highperf");
                    mWifiLock.setReferenceCounted(false);
                    mWifiLock.acquire();
                }
            }
        } catch (RuntimeException e) {
            CrosspointLog.e(TAG, "could not take the wake/Wi-Fi locks", e);
        }
    }

    private void releaseLocks() {
        try {
            if (mWakeLock != null && mWakeLock.isHeld()) mWakeLock.release();
            if (mWifiLock != null && mWifiLock.isHeld()) mWifiLock.release();
            if (mWifiLockLowLatency != null && mWifiLockLowLatency.isHeld()) mWifiLockLowLatency.release();
        } catch (RuntimeException e) {
            CrosspointLog.e(TAG, "could not release the locks", e);
        }
        mWakeLock = null; mWifiLock = null; mWifiLockLowLatency = null;
    }
}
