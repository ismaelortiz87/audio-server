// SPDX-License-Identifier: GPLv3-or-later
// Copyright (C) 2020 Jesse Chappell

package io.lagreca.app.crosspoint;

import android.app.Notification;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.drawable.Icon;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.Bundle;

/**
 * A18 (docs/design/android.md section 12): Crosspoint as an Android media
 * session, so the phone shows it in its lock-screen player (Samsung's Now Bar)
 * with buttons that work without unlocking.
 *
 * Deliberately built on the *platform* android.media.session APIs, not
 * androidx.media: everything needed (MediaSession, PlaybackState custom
 * actions, Notification.MediaStyle) is in android.jar at minSdk 24, so the app
 * gains no new dependency and no Gradle resolution risk.
 *
 * The session owns no engine state. Native pushes metadata and a small action
 * state through SonoBusService; button presses become nativeNotificationAction
 * calls, the same route the notification actions already use.
 */
final class CrosspointMediaSession {

    /** Custom action ids. Also the values passed to native. */
    static final String ACTION_MIC = "io.lagreca.app.crosspoint.MIC";
    static final String ACTION_PREV = "io.lagreca.app.crosspoint.PREV";
    static final String ACTION_NEXT = "io.lagreca.app.crosspoint.NEXT";
    static final String ACTION_MUTE = "io.lagreca.app.crosspoint.MUTE";
    static final String ACTION_SOLO = "io.lagreca.app.crosspoint.SOLO";

    // Native action codes (SonoBusActivity.nativeNotificationAction). 1/2 already
    // existed for the old notification's Mic and Disconnect buttons. The values
    // must match crosspoint::android::PlayerButton in AndroidPlatform.h.
    static final int NATIVE_MIC = 1;
    static final int NATIVE_PREV = 2;
    static final int NATIVE_NEXT = 3;
    static final int NATIVE_MUTE = 4;
    static final int NATIVE_SOLO = 5;

    private final Context ctx;
    private MediaSession session;

    // Pushed from native, main thread only.
    private String title = "Crosspoint";
    private String subtitle = "Mic off \u00B7 nobody hears you";
    private String album = "";
    private boolean micLive = false;
    private int artColor = 0xFF12161A;
    private boolean micAvailable = true;
    /** The selected station's short name, and the station list (section 12.3/12.4). */
    private String selected = "";
    private int selectedColor = 0xFF12161A;
    private boolean selectedMuted = false, selectedSoloed = false;
    private int stationCount = 0;

    CrosspointMediaSession(Context ctx) {
        this.ctx = ctx.getApplicationContext();
    }

    // ------------------------------------------------------------ session

    void start() {
        if (session != null) return;
        session = new MediaSession(ctx, "Crosspoint");
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { nativeAction(NATIVE_MIC); }
            @Override public void onPause() { nativeAction(NATIVE_MIC); }
            @Override public void onSkipToNext() { nativeAction(NATIVE_NEXT); }
            @Override public void onSkipToPrevious() { nativeAction(NATIVE_PREV); }
            @Override public void onCustomAction(String action, Bundle extras) {
                if (ACTION_MUTE.equals(action)) nativeAction(NATIVE_MUTE);
                else if (ACTION_SOLO.equals(action)) nativeAction(NATIVE_SOLO);
                else if (ACTION_MIC.equals(action)) nativeAction(NATIVE_MIC);
            }
            @Override public void onStop() { /* the service lifetime owns this */ }
        });
        session.setActive(true);
        // Section 12.1: "playing" from connect until Disconnect, whatever the mic
        // does, so the phone never times the player out of the Now Bar.
        session.setPlaybackState(playbackState());
        pushMetadata();
    }

    void stop() {
        if (session == null) return;
        session.setActive(false);
        session.release();
        session = null;
    }

    boolean isRunning() { return session != null; }

    /**
     * A17 (section 12.3/12.4): the station list and the selected station, as JSON
     * from native. Picks the selected station's colour and flags for the title,
     * artwork and the Mute / Solo button labels.
     */
    void updateStations(String stationsJson, String selectedName) {
        this.selected = selectedName != null ? selectedName : "";
        this.stationCount = 0;
        this.selectedMuted = false;
        this.selectedSoloed = false;
        try {
            org.json.JSONArray arr = new org.json.JSONArray(stationsJson != null ? stationsJson : "[]");
            stationCount = arr.length();
            for (int i = 0; i < arr.length(); i++) {
                org.json.JSONObject s = arr.optJSONObject(i);
                if (s == null) continue;
                if (s.optString("name", "").equals(this.selected)) {
                    selectedMuted = s.optBoolean("mute", false);
                    selectedSoloed = s.optBoolean("solo", false);
                    selectedColor = STATION_COLORS[Math.abs(s.optInt("colorIndex", 0)) % STATION_COLORS.length];
                }
            }
        } catch (org.json.JSONException e) {
            stationCount = 0;
        }
        if (session != null) {
            session.setPlaybackState(playbackState());
            pushMetadata();
        }
    }

    /** Section 12.4: the station identity colours (console-ui --s1..--s4). */
    private static final int[] STATION_COLORS = { 0xFF9085E9, 0xFF199E70, 0xFFC98500, 0xFFB0526F };

    // ------------------------------------------------------------ content

    /**
     * Main thread. Native calls this whenever the pushed content changes.
     * `micLive` tints the whole player (Android 13+ derives the player colour
     * from the artwork).
     */
    void update(String title, String subtitle, String album, int artColor, boolean micLive, boolean micAvailable) {
        this.title = title != null ? title : "";
        this.subtitle = subtitle != null ? subtitle : "";
        this.album = album != null ? album : "";
        this.artColor = artColor;
        this.micLive = micLive;
        this.micAvailable = micAvailable;
        if (session == null) return;
        session.setPlaybackState(playbackState());
        pushMetadata();
    }

    /** The actions the player offers right now, reflected in PlaybackState. */
    private long actions() {
        long a = PlaybackState.ACTION_PLAY_PAUSE | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY;
        // Section 12.2: with no stations only Mic shows.
        if (stationCount > 0)
            a |= PlaybackState.ACTION_SKIP_TO_NEXT | PlaybackState.ACTION_SKIP_TO_PREVIOUS;
        return a;
    }

    private PlaybackState playbackState() {
        PlaybackState.Builder b = new PlaybackState.Builder()
            .setActions(actions())
            // Always "playing" (section 12.1). The mic state is in the text.
            .setState(PlaybackState.STATE_PLAYING, 0L, 1.0f, android.os.SystemClock.elapsedRealtime());
        // Section 12.2: Mute and Solo act on the selected station, so they only
        // exist when there is one. A CustomAction must carry a real icon resource
        // id: passing 0 throws IllegalArgumentException (found on API 34).
        if (stationCount > 0) {
            b.addCustomAction(new PlaybackState.CustomAction.Builder(
                    ACTION_MUTE, selectedMuted ? "Unmute" : "Mute",
                    R.drawable.ic_stat_crosspoint).build());
            b.addCustomAction(new PlaybackState.CustomAction.Builder(
                    ACTION_SOLO, selectedSoloed ? "Unsolo" : "Solo",
                    R.drawable.ic_stat_crosspoint).build());
        }
        return b.build();
    }

    private void pushMetadata() {
        if (session == null) return;
        // Section 12.4: the title is the selected station's short name, or
        // "Crosspoint" with no stations; the subtitle is who hears you.
        final String t = stationCount > 0 && !selected.isEmpty() ? selected : "Crosspoint";
        MediaMetadata.Builder m = new MediaMetadata.Builder()
            .putString(MediaMetadata.METADATA_KEY_TITLE, t)
            .putString(MediaMetadata.METADATA_KEY_ARTIST, subtitle)
            .putString(MediaMetadata.METADATA_KEY_ALBUM, album)
            .putBitmap(MediaMetadata.METADATA_KEY_ART,
                       artwork(stationCount > 0 ? selectedColor : 0xFF12161A, micLive, selectedMuted))
            .putLong(MediaMetadata.METADATA_KEY_DURATION, -1L);
        session.setMetadata(m.build());
    }

    /**
     * A 512x512 tile in the station colour (section 12.4). Drawn in code: the app
     * has no per-station art assets, and Android 13+ tints the player from this
     * bitmap, so a solid colour is what carries the state.
     */
    private Bitmap artwork(int color, boolean live, boolean muted) {
        final int size = 512;
        Bitmap bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(bmp);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        // Section 12.4: muted is the colour at 35% over the panel colour.
        int base = muted ? blend(0xFF12161A, color, 0.35f) : color;
        p.setColor(base);
        c.drawRoundRect(0, 0, size, size, 96, 96, p);
        if (live) {
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(48);
            p.setColor(0xFFFF4F7B);                          // --live
            c.drawRoundRect(24, 24, size - 24, size - 24, 72, 72, p);
            p.setStyle(Paint.Style.FILL);
        }
        // The Crosspoint mark: a white route with a lit node.
        p.setColor(Color.WHITE);
        c.drawCircle(size / 2f, size / 2f, 54, p);
        return bmp;
    }

    /** `over` at `alpha` over `under`. */
    private static int blend(int under, int over, float alpha) {
        int r = (int) (Color.red(under) * (1 - alpha) + Color.red(over) * alpha);
        int g = (int) (Color.green(under) * (1 - alpha) + Color.green(over) * alpha);
        int bl = (int) (Color.blue(under) * (1 - alpha) + Color.blue(over) * alpha);
        return Color.argb(255, r, g, bl);
    }

    // ------------------------------------------------------------ notification

    /** The MediaStyle notification that replaces the section 4 one (section 12.1). */
    Notification buildNotification(String channelId, PendingIntent contentIntent) {
        Notification.Builder b = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            ? new Notification.Builder(ctx, channelId)
            : new Notification.Builder(ctx);

        // Build the action list first: the compact-view indices below have to
        // name real positions, and the list varies (the Mic slot disappears while
        // the mic can't be toggled, and Mute / Solo need a selected station).
        java.util.List<Integer> compact = new java.util.ArrayList<>();
        if (micAvailable) {
            compact.add(compact.size());
            b.addAction(new Notification.Action.Builder(
                    Icon.createWithResource(ctx, R.drawable.ic_stat_crosspoint), "Mic",
                    serviceIntent(NATIVE_MIC)).build());
        }
        if (stationCount > 0) {
            compact.add(compact.size());
            b.addAction(new Notification.Action.Builder(
                    Icon.createWithResource(ctx, R.drawable.ic_stat_crosspoint), "Previous",
                    serviceIntent(NATIVE_PREV)).build());
            compact.add(compact.size());
            b.addAction(new Notification.Action.Builder(
                    Icon.createWithResource(ctx, R.drawable.ic_stat_crosspoint), "Next",
                    serviceIntent(NATIVE_NEXT)).build());
            compact.add(compact.size());
            b.addAction(new Notification.Action.Builder(
                    Icon.createWithResource(ctx, R.drawable.ic_stat_crosspoint), selectedMuted ? "Unmute" : "Mute",
                    serviceIntent(NATIVE_MUTE)).build());
            b.addAction(new Notification.Action.Builder(
                    Icon.createWithResource(ctx, R.drawable.ic_stat_crosspoint), selectedSoloed ? "Unsolo" : "Solo",
                    serviceIntent(NATIVE_SOLO)).build());
        }

        Notification.MediaStyle style = new Notification.MediaStyle();
        if (session != null) style.setMediaSession(session.getSessionToken());
        // At most 3 in the collapsed view; show what exists (section 12.2).
        int[] show = new int[Math.min(3, compact.size())];
        for (int i = 0; i < show.length; i++) show[i] = compact.get(i);
        if (show.length > 0) style.setShowActionsInCompactView(show);

        b.setStyle(style)
         .setSmallIcon(R.drawable.ic_stat_crosspoint)
         .setContentTitle(title)
         .setContentText(subtitle)
         .setColor(micLive ? 0xFFFF4F7B : 0xFF2E6B66)
         .setOngoing(true)
         .setOnlyAlertOnce(true)
         .setShowWhen(false)
         .setVisibility(Notification.VISIBILITY_PUBLIC)
         .setContentIntent(contentIntent);
        return b.build();
    }

    /** A button press: back to native. Player buttons take their own channel
        (pendingPlayerButton), because the legacy 1/2 action slot is only for the
        old notification's Mic and Disconnect. */
    private void nativeAction(int code) {
        SonoBusActivity.nativePlayerButton(code);
    }

    private PendingIntent serviceIntent(int code) {
        Intent i = new Intent(ctx, SonoBusService.class);
        i.setAction(SonoBusService.ACTION_MEDIA);
        i.putExtra("code", code);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) flags |= PendingIntent.FLAG_IMMUTABLE;
        return PendingIntent.getForegroundService(ctx, 9000 + code, i, flags);
    }
}
