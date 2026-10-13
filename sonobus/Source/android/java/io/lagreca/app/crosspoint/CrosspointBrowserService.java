// SPDX-License-Identifier: GPLv3-or-later
// Copyright (C) 2020 Jesse Chappell

package io.lagreca.app.crosspoint;

import android.media.MediaDescription;
import android.media.MediaMetadata;
import android.media.browse.MediaBrowser;
import android.media.session.MediaSession;
import android.os.Bundle;
import android.service.media.MediaBrowserService;

import java.util.ArrayList;
import java.util.List;

/**
 * A20 (docs/design/android.md section 13): the Android Auto media app.
 *
 * Android Auto only lists apps declared as Auto media apps, and that means a
 * MediaBrowserService. This one returns the A18 session's token, so Auto draws
 * the *same* player as the phone's lock screen (section 13.4: one session), and
 * publishes the station browse list.
 *
 * Built on the platform android.service.media.MediaBrowserService, not
 * androidx.media: it is in android.jar at minSdk 24, so the app gains no new
 * dependency (the same choice A18 made for the session itself).
 *
 * The service owns no engine state. Tapping a station calls back into native,
 * which only moves the selection (section 13.1); mute, solo and the mic are
 * untouched by a tap.
 */
public class CrosspointBrowserService extends MediaBrowserService {

    /** Section 13.1: the browse root, i.e. Auto's "Stations" tab. */
    static final String ROOT_ID = "crosspoint_root";
    /** Prefix for a station item, so a tap can be recognised in onPlayFromMediaId. */
    static final String STATION_PREFIX = "station:";
    /** Section 13.3: the single item shown when nothing is connected but settings are saved. */
    static final String CONNECT_ID = "crosspoint_connect";

    private static CrosspointBrowserService sInstance = null;

    /** The live session, set by the service that owns it (SonoBusService). */
    private static CrosspointMediaSession sSession = null;

    /**
     * The state pushed from native (SonoBusService.applyPlayerState). The browse
     * list reads this rather than the session, because section 13.3 needs the list
     * to work while NOT connected: the playback session is released on Disconnect
     * (there is nothing to play), but Auto still has to offer "Connect to {group}"
     * or the fresh-install error.
     */
    private static String sStateJson = "{}";

    /** Called by SonoBusService on every player-state push. */
    static void setStateJson(String json) {
        if (json != null && !json.isEmpty()) sStateJson = json;
    }

    /**
     * A20 (section 13.4): the session Auto plays. Resolved from the shared one, so
     * it works even when the foreground service is not running: after Disconnect
     * the service stops, but Auto still has to browse and offer "Connect to
     * {group}" (section 13.3).
     */
    /** Set once per process: MediaBrowserService.setSessionToken throws if called twice. */
    private static boolean sTokenSet = false;

    static CrosspointMediaSession resolveSession(android.content.Context ctx) {
        CrosspointMediaSession s = sSession != null ? sSession : CrosspointMediaSession.peek();
        // Auto can open Crosspoint without the phone app ever having run (e.g. a
        // fresh boot). Create the session so there is at least a token to browse
        // with; section 13.3's "set up on your phone first" then comes from the
        // empty pushed state.
        if (s == null && ctx != null) s = CrosspointMediaSession.shared(ctx, false);
        applyToken(s);
        return s;
    }

    /** Called by SonoBusService when the media session starts or stops. */
    static void setSession(CrosspointMediaSession session) {
        sSession = session;
        applyToken(session);
    }

    /**
     * Hand the token to Auto, once. MediaBrowserService.setSessionToken throws
     * IllegalStateException if it is called twice, and the session is process-shared
     * now, so a reconnect (or a service restart) would otherwise crash the app
     * ("The session token has already been set").
     */
    private static void applyToken(CrosspointMediaSession session) {
        if (sTokenSet || sInstance == null || session == null) return;
        sInstance.setSessionToken(session.getToken());
        sTokenSet = true;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        sInstance = this;
        resolveSession(this);
    }

    @Override
    public void onDestroy() {
        if (sInstance == this) {
            sInstance = null;
            // A new instance gets a fresh MediaBrowserService, so its token may be
            // set again (the token itself is unchanged: one shared session).
            sTokenSet = false;
        }
        super.onDestroy();
    }

    /**
     * Section 13.4: Auto and the lock screen share one session, so the player is
     * the same and every action goes through the A17 command paths.
     * `null` rootId means "Auto is browsing this app from the launcher".
     */
    @Override
    public BrowserRoot onGetRoot(String clientPackageName, int clientUid, Bundle hints) {
        // Section 13.3: with nothing set up at all, Auto shows an error state and
        // no action, so the root still has to be returned for Auto to show it.
        return new BrowserRoot(ROOT_ID, null);
    }

    /** Section 13.1: one item per station, or the single "Connect to {group}" item. */
    @Override
    public void onLoadChildren(String parentId, Result<List<MediaBrowser.MediaItem>> result) {
        List<MediaBrowser.MediaItem> items = new ArrayList<>();

        if (!ROOT_ID.equals(parentId)) {
            result.sendResult(items);
            return;
        }

        // Everything is read from the pushed state, not from the live session:
        // section 13.3 needs a working list while disconnected, when there is no
        // playback session at all.
        org.json.JSONObject o;
        try {
            o = new org.json.JSONObject(sStateJson);
        } catch (org.json.JSONException e) {
            result.sendResult(items);
            return;
        }

        final boolean connected = o.optBoolean("connected", false);
        final String group = o.optString("group", "");
        final boolean hasSaved = o.optBoolean("hasSaved", false);

        // Section 13.3: not connected.
        if (!connected) {
            if (hasSaved && !group.isEmpty()) {
                // Saved settings: one playable item to connect with; tapping it
                // reconnects (handled in the session's onPlayFromMediaId).
                MediaDescription.Builder d = new MediaDescription.Builder()
                        .setMediaId(CONNECT_ID)
                        .setTitle("Connect to " + group)
                        .setSubtitle(carSubtitleFromState(o));
                if (o.has("connecting") || o.optString("connText", "").startsWith("Connecting")) {
                    // Section 13.3 connecting/reconnecting: the car is told so.
                    d.setTitle("Connecting to " + group + "…");
                }
                items.add(new MediaBrowser.MediaItem(d.build(),
                        MediaBrowser.MediaItem.FLAG_PLAYABLE));
            } else {
                // Section 13.3 fresh install: nothing saved at all. Auto shows an
                // error state with no action; the message is ours, not Auto's
                // generic empty list. It stays until the phone is set up and a
                // saved connection starts being pushed.
                MediaDescription.Builder d = new MediaDescription.Builder()
                        .setMediaId(CONNECT_ID)
                        .setTitle("Set up Crosspoint on your phone first.")
                        .setSubtitle("");
                items.add(new MediaBrowser.MediaItem(d.build(),
                        0 /* not playable: an error item, no action */));
            }
            result.sendResult(items);
            return;
        }

        // Section 13.1: up to 8 stations, in stationOrder, offline ones included
        // (so the car list matches the phone).
        org.json.JSONArray arr = o.optJSONArray("stations");
        for (int i = 0; arr != null && i < arr.length(); i++) {
            org.json.JSONObject s = arr.optJSONObject(i);
            if (s == null) continue;
            String name = s.optString("name", "");
            MediaDescription.Builder d = new MediaDescription.Builder()
                    .setMediaId(STATION_PREFIX + name)
                    .setTitle(name)
                    .setSubtitle(CrosspointMediaSession.stationStateFor(
                            s.optBoolean("hearsYou", false), s.optBoolean("solo", false),
                            s.optBoolean("mute", false), anySolo(o),
                            s.optString("presence", "online"), s.optString("health", "clear")))
                    .setIconBitmap(stationBitmap(s));
            // Tapping a station opens the player; it does not start audio by itself.
            items.add(new MediaBrowser.MediaItem(d.build(),
                    MediaBrowser.MediaItem.FLAG_PLAYABLE));
        }
        result.sendResult(items);
    }

    /** True when any station in the pushed state is soloed (the dim rules). */
    private static boolean anySolo(org.json.JSONObject o) {
        org.json.JSONArray arr = o.optJSONArray("stations");
        for (int i = 0; arr != null && i < arr.length(); i++) {
            if (arr.optJSONObject(i) != null && arr.optJSONObject(i).optBoolean("solo", false)) return true;
        }
        return false;
    }

    /** Section 13.1: 128 px station-colour icon, blended toward live while it hears you. */
    private static android.graphics.Bitmap stationBitmap(org.json.JSONObject s) {
        final int size = 128;
        android.graphics.Bitmap bmp = android.graphics.Bitmap.createBitmap(
                size, size, android.graphics.Bitmap.Config.ARGB_8888);
        android.graphics.Canvas c = new android.graphics.Canvas(bmp);
        android.graphics.Paint p = new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG);
        p.setColor(CrosspointMediaSession.tileColorFor(
                s.optInt("colorIndex", 0), s.optString("presence", "online"),
                s.optBoolean("mute", false), s.optBoolean("hearsYou", false)));
        c.drawRoundRect(0, 0, size, size, 28, 28, p);
        return bmp;
    }

    /** Section 13.2/13.3: the car-short line for the connect item. */
    private static String carSubtitleFromState(org.json.JSONObject o) {
        String conn = o.optString("connText", "");
        return conn.isEmpty() ? "Not connected" : conn;
    }

    /*
     * Tapping an item arrives at MediaSession.Callback.onPlayFromMediaId. The
     * platform MediaBrowserService has no onPlayFromMediaId or onSearch of its own
     * (those are androidx additions), so the tap is handled on the session that
     * Auto plays: see CrosspointMediaSession's callback.
     */
}
