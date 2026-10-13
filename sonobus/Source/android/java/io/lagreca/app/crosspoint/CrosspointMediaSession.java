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
import android.graphics.Path;
import android.graphics.drawable.Icon;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;

import java.util.ArrayList;
import java.util.List;

/**
 * A18 (docs/design/android.md section 12): Crosspoint as an Android media
 * session, so the phone shows it in its lock-screen player (Samsung's Now Bar)
 * with buttons that work without unlocking.
 *
 * Built on the *platform* android.media.session APIs, not androidx.media:
 * everything needed (MediaSession, PlaybackState custom actions,
 * Notification.MediaStyle) is in android.jar at minSdk 24, so the app gains no
 * new dependency.
 *
 * LAYOUT (section 12.2). The spike (docs/TASKS.md A18, Result) found that the
 * Android 13/14 system player always draws the standard play/pause in the centre
 * slot and never lets a custom action take it, so this is **Layout B**:
 *
 *   [Listening play/pause] [Next station] [Mic] [Mute] [Solo]
 *
 * The session owns no engine state. Native pushes one JSON document
 * (SonoBusService.applyPlayerState) and the "who hears you" / connection text;
 * button presses become nativePlayerButton calls.
 */
final class CrosspointMediaSession {

    // Native button codes. Must match crosspoint::android::PlayerButton in AndroidPlatform.h.
    static final int NATIVE_MIC = 1;
    static final int NATIVE_PREV = 2;
    static final int NATIVE_NEXT = 3;
    static final int NATIVE_MUTE = 4;
    static final int NATIVE_SOLO = 5;
    static final int NATIVE_LISTEN = 7;      // Layout B centre: Listening pause / resume
    /** A20 section 13.3: the car's "Connect to {group}" browse item. 6 is the
        spare slot in the native enum (its `disconnect` is never sent today). */
    static final int NATIVE_CONNECT = 6;
    // Not a native code: "Allow mic" opens the app instead.

    static final String ACTION_MIC = "io.lagreca.app.crosspoint.MIC";
    static final String ACTION_NEXT = "io.lagreca.app.crosspoint.NEXT";
    static final String ACTION_MUTE = "io.lagreca.app.crosspoint.MUTE";
    static final String ACTION_SOLO = "io.lagreca.app.crosspoint.SOLO";

    /** Mic button states, pushed by native (AndroidServiceController::playerMicState). */
    static final int MIC_HIDDEN = 0, MIC_ON = 1, MIC_OFF = 2, MIC_ALLOW = 3;

    /** Section 12.4: station identity colours (console-ui tokens --s1..--s4). */
    private static final int[] STATION_COLORS = { 0xFF9085E9, 0xFF199E70, 0xFFC98500, 0xFFBED590 };
    private static final int PANEL = 0xFF12161A, INK4 = 0xFF4A4E4B, LIVE = 0xFFFF4F7B;

    private final Context ctx;
    private MediaSession session;

    // ---- pushed from native (main thread only)
    private String subtitle = "Mic off · nobody hears you";   // section 4 title copy: who hears you
    private String connText = "";                                    // section 4 text: connection
    private boolean live = false;
    private int micState = MIC_HIDDEN;
    private boolean connected = false;
    private boolean listening = true;                                // Layout B centre
    private String group = "";                                       // A20 section 13.3
    private boolean hasSaved = false;                                // A20 section 13.3
    private boolean callActive = false, pttMode = false;             // A20 section 13.2
    private int soloDimDb = -18;
    private final List<Station> stations = new ArrayList<>();
    private String selected = "";
    private boolean active = false;                                  // A20: shown in the system UI only while connected

    // ---- what was last given to the system, so nothing is re-posted when it did not change
    private String lastMetaKey = "";
    private String lastStateKey = "";

    /** One station as the player draws it (section 12.4). */
    static final class Station {
        String name = "";
        int colorIndex = 0;
        String presence = "online", health = "clear";
        double latencyMs = -1, lossPct = 0;
        boolean mute, solo;
        boolean hearsYou;                                            // A20 section 13.1
    }

    CrosspointMediaSession(Context ctx) {
        this.ctx = ctx.getApplicationContext();
    }

    // ---- one session for the whole process (section 13.4) --------------------

    /**
     * The single Crosspoint session, shared by the phone's lock screen and the
     * Android Auto browser. It outlives the foreground service on purpose: after
     * Disconnect the service stops, but Auto must still be able to browse and
     * offer "Connect to {group}" (section 13.3), and a MediaBrowserService with
     * no session token cannot be bound at all.
     *
     * It is only *active* while the service runs, so the lock-screen player still
     * disappears on Disconnect (section 12.1); the token simply stays valid.
     */
    private static CrosspointMediaSession sShared = null;

    /** `wantActive`: true from the foreground service (connected), false when Auto
        is browsing a Crosspoint that is not connected. */
    static synchronized CrosspointMediaSession shared(Context ctx, boolean wantActive) {
        if (sShared == null) {
            sShared = new CrosspointMediaSession(ctx);
            sShared.start();
        }
        sShared.setActive(wantActive);
        return sShared;
    }

    static synchronized CrosspointMediaSession peek() { return sShared; }

    /** Shown in the system's media UI only while connected (section 12.1). */
    void setActive(boolean active) {
        this.active = active;
        if (session != null) session.setActive(active);
    }

    // ------------------------------------------------------------ session

    void start() {
        if (session != null) return;
        session = new MediaSession(ctx, "Crosspoint");
        session.setCallback(new MediaSession.Callback() {
            // The on-screen play/pause is Listening (Layout B); the headset button
            // is the Mic (section 12.2), and arrives through onMediaButtonEvent.
            @Override public void onPlay() { nativeAction(NATIVE_LISTEN); }
            @Override public void onPause() { nativeAction(NATIVE_LISTEN); }
            @Override public boolean onMediaButtonEvent(Intent intent) {
                KeyEvent ev = intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT);
                if (ev == null) return false;
                switch (ev.getKeyCode()) {
                    case KeyEvent.KEYCODE_HEADSETHOOK:
                    case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE:
                    case KeyEvent.KEYCODE_MEDIA_PLAY:
                    case KeyEvent.KEYCODE_MEDIA_PAUSE:
                        if (ev.getAction() == KeyEvent.ACTION_DOWN && ev.getRepeatCount() == 0) nativeAction(NATIVE_MIC);
                        return true;
                    case KeyEvent.KEYCODE_MEDIA_NEXT:
                        if (ev.getAction() == KeyEvent.ACTION_DOWN) nativeAction(NATIVE_NEXT);
                        return true;
                    case KeyEvent.KEYCODE_MEDIA_PREVIOUS:
                        if (ev.getAction() == KeyEvent.ACTION_DOWN) nativeAction(NATIVE_PREV);
                        return true;
                    default:
                        return super.onMediaButtonEvent(intent);
                }
            }
            @Override public void onCustomAction(String action, Bundle extras) {
                if (ACTION_MUTE.equals(action)) nativeAction(NATIVE_MUTE);
                else if (ACTION_SOLO.equals(action)) nativeAction(NATIVE_SOLO);
                else if (ACTION_NEXT.equals(action)) nativeAction(NATIVE_NEXT);
                else if (ACTION_MIC.equals(action)) micPressed();
            }
            /**
             * A20 (section 13.1): the Android Auto browse list. Auto delivers the
             * tap here, not to the browse service (the platform
             * MediaBrowserService has no onPlayFromMediaId). A tap only selects
             * the station; mute, solo and the mic are untouched.
             */
            @Override public void onPlayFromMediaId(String mediaId, Bundle extras) {
                if (mediaId == null) return;
                if (mediaId.startsWith(CrosspointBrowserService.STATION_PREFIX))
                    SonoBusActivity.nativeSelectStation(
                            mediaId.substring(CrosspointBrowserService.STATION_PREFIX.length()));
                else if (CrosspointBrowserService.CONNECT_ID.equals(mediaId))
                    nativeAction(NATIVE_CONNECT);          // section 13.3
            }
            @Override public void onStop() { /* the service lifetime owns this */ }
        });
        session.setSessionActivity(openAppIntent());
        // Not necessarily active: the service activates it while connected, and a
        // cold Android Auto launch creates it inactive so no phantom player appears
        // on the lock screen (section 12.1).
        session.setActive(active);
        refresh();
    }

    void stop() {
        if (session == null) return;
        session.setActive(false);
        session.release();
        session = null;
        lastMetaKey = "";
        lastStateKey = "";
    }

    boolean isRunning() { return session != null; }

    // ------------------------------------------------------------ pushed state

    /** Who hears you (section 4 title), connection text (section 4 text), the live flag and the Mic button state. */
    void updateText(String subtitle, String connText, boolean live, int micState) {
        this.subtitle = subtitle != null ? subtitle : "";
        this.connText = connText != null ? connText : "";
        this.live = live;
        this.micState = micState;
        refresh();
    }

    /** The station list and selection, as JSON from native (AndroidServiceController::updatePlayerStations). */
    void updateState(String json) {
        stations.clear();
        try {
            org.json.JSONObject o = new org.json.JSONObject(json != null && !json.isEmpty() ? json : "{}");
            selected = o.optString("selected", "");
            connected = o.optBoolean("connected", false);
            listening = o.optBoolean("listening", true);
            group = o.optString("group", "");
            hasSaved = o.optBoolean("hasSaved", false);
            callActive = o.optBoolean("phoneCall", false);
            pttMode = o.optBoolean("pttMode", false);
            soloDimDb = o.optInt("soloDimDb", -18);
            org.json.JSONArray arr = o.optJSONArray("stations");
            for (int i = 0; arr != null && i < arr.length(); i++) {
                org.json.JSONObject s = arr.optJSONObject(i);
                if (s == null) continue;
                Station st = new Station();
                st.name = s.optString("name", "");
                st.colorIndex = Math.abs(s.optInt("colorIndex", 0));
                st.presence = s.optString("presence", "online");
                st.health = s.optString("health", "clear");
                st.latencyMs = s.isNull("latencyMs") ? -1 : s.optDouble("latencyMs", -1);
                st.lossPct = s.isNull("lossPct") ? 0 : s.optDouble("lossPct", 0);
                st.mute = s.optBoolean("mute", false);
                st.solo = s.optBoolean("solo", false);
                st.hearsYou = s.optBoolean("hearsYou", false);
                stations.add(st);
            }
        } catch (org.json.JSONException e) {
            stations.clear();
        }
        refresh();
    }

    private Station selectedStation() {
        for (Station s : stations) if (s.name.equals(selected)) return s;
        return null;
    }

    // ---- A20 (section 13.1): what the Android Auto browse list reads ----------

    /** The session token Auto attaches its player to. */
    MediaSession.Token getToken() { return session != null ? session.getSessionToken() : null; }

    /** One entry per station, in stationOrder, up to 8 (section 13.1). */
    List<Station> getStations() { return stations; }

    String getSelectedName() { return selected; }
    boolean isConnected() { return connected; }
    String getGroup() { return group; }
    boolean hasSavedConnection() { return hasSaved; }
    /** Section 13.2: the car-short now-playing subtitle. */
    String getCarSubtitle() { return carSubtitle(); }

    /** Section 13.1: the browse item's icon, 128 px, in the station colour, blended
        toward live while that station hears you. Offline: ink4. */
    Bitmap stationIcon(int index) {
        Station s = index >= 0 && index < stations.size() ? stations.get(index) : null;
        final int size = 128;
        Bitmap bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(bmp);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(tileColor(s, s != null && s.hearsYou));      // same rule as section 12.4
        c.drawRoundRect(0, 0, size, size, 28, 28, p);
        return bmp;
    }

    /** Section 13.1: the browse item's subtitle, e.g. "Hears you · Clear". */
    String stationState(Station s) {
        if (s == null) return "";
        List<String> p = new ArrayList<>();
        if (s.hearsYou) p.add("Hears you");
        if (s.solo) p.add("Soloed");
        if (s.mute) p.add("Muted");
        String presence = s.presence == null ? "online" : s.presence;
        if ("lost".equals(presence)) p.add("Lost");
        else if ("offline".equals(presence)) p.add("Offline");
        else if ("unstable".equals(s.health)) p.add("Unstable");
        else if (!s.hearsYou && !s.solo) p.add("Clear");
        StringBuilder sb = new StringBuilder();
        for (String part : p) { if (sb.length() > 0) sb.append(" · "); sb.append(part); }
        return sb.length() == 0 ? "Clear" : sb.toString();
    }

    /**
     * Section 13.2: the car-short now-playing subtitle, used only on Android Auto.
     * Car screens truncate around 30 characters and the driver only glances, so
     * these are shortened forms of the section 4/12 lines: no numbers except the
     * "Clear/Unstable" words, no meters.
     */
    private String carSubtitle() {
        if (!listening) return "Paused · mic off";
        if (micState == MIC_ALLOW) return "Mic blocked · allow on phone";
        if (callActive) return "Mic off · on a call";
        if (pttMode) return "Push to talk · hold on phone";
        if (!connected) return connText.isEmpty() ? "Not connected" : connText;
        if (micState != MIC_ON) return "Mic off";
        // Mic is on: say who hears you.
        List<String> hear = new ArrayList<>();
        for (Station s : stations) if (s.hearsYou) hear.add(s.name);
        boolean anySolo = false;
        for (Station s : stations) anySolo |= s.solo;
        if (hear.isEmpty()) return "Mic open · nobody hears you";
        if (anySolo && hear.size() == 1) return "Live · solo " + hear.get(0);
        if (hear.size() == 1) return "Live · " + hear.get(0) + " hears you";
        if (hear.size() == stations.size()) return "Live · all " + hear.size() + " hear you";
        if (hear.size() == 2) return "Live · " + hear.get(0) + " and " + hear.get(1) + " hear you";
        return "Live · " + hear.size() + " of " + stations.size() + " hear you";
    }

    // ------------------------------------------------------------ text (section 12.4)

    private String titleText() {
        Station s = selectedStation();
        return s != null ? s.name : "Crosspoint";
    }

    /** Second line: the selected station's state, " · " joined; not connected: the section 4 connection text. */
    private String albumText() {
        Station s = selectedStation();
        if (!connected || s == null) return connText;
        List<String> p = new ArrayList<>();
        // The system's centre button keeps showing "pause" (the session stays PLAYING, section 12.1),
        // so the paused state is told here.
        if (!listening) p.add("Listening paused");
        boolean anySolo = false;
        for (Station o : stations) anySolo |= o.solo;
        if (s.mute) p.add("Muted");
        if (s.solo) p.add("Soloed");
        else if (anySolo && !s.mute) p.add("Dimmed −" + Math.abs(soloDimDb));
        if ("lost".equals(s.presence)) p.add("Lost · reconnecting");
        else if ("offline".equals(s.presence)) p.add("Offline");
        else if ("unstable".equals(s.health)) p.add(s.lossPct >= 0.5 ? "Unstable · " + Math.round(s.lossPct) + "% loss" : "Unstable");
        else p.add(s.latencyMs >= 0 ? "Clear · " + Math.round(s.latencyMs) + " ms" : "Clear");
        StringBuilder sb = new StringBuilder();
        for (String part : p) { if (sb.length() > 0) sb.append(" · "); sb.append(part); }
        return sb.toString();
    }

    // ------------------------------------------------------------ session content

    /** Push state and metadata, only for what actually changed (section 4: no flicker, no sound). */
    private void refresh() {
        if (session == null) return;
        final Station sel = selectedStation();
        final String stateKey = micState + "|" + listening + "|" + stations.size()
            + "|" + (sel != null ? sel.name + sel.mute + sel.solo : "");
        if (!stateKey.equals(lastStateKey)) {
            lastStateKey = stateKey;
            session.setPlaybackState(playbackState());
        }
        final String title = titleText(), album = albumText();
        final int art = artworkKey(sel);
        final String metaKey = title + "\n" + subtitle + "\n" + album + "\n" + art + "\n" + live;
        if (!metaKey.equals(lastMetaKey)) {
            lastMetaKey = metaKey;
            session.setMetadata(new MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, title)
                .putString(MediaMetadata.METADATA_KEY_ARTIST, subtitle)
                .putString(MediaMetadata.METADATA_KEY_ALBUM, album)
                .putBitmap(MediaMetadata.METADATA_KEY_ART, artwork(sel, live))
                .putLong(MediaMetadata.METADATA_KEY_DURATION, -1L)
                .build());
        }
    }

    /** The actions the player offers right now (Layout B). */
    private PlaybackState playbackState() {
        final Station sel = selectedStation();
        final boolean hasStation = sel != null;
        // Section 12.1: "playing" from connect until Disconnect, whatever the mic
        // does, so the system never drops the player. (Pausing Listening also stays
        // PLAYING for the same reason: see the A18 spike in docs/TASKS.md.)
        PlaybackState.Builder b = new PlaybackState.Builder()
            .setState(PlaybackState.STATE_PLAYING, PlaybackState.PLAYBACK_POSITION_UNKNOWN, 1.0f,
                      android.os.SystemClock.elapsedRealtime());
        long actions = 0;
        // The centre slot is the system's play/pause: Listening. With no stations
        // there is nothing to listen to, so only Mic shows.
        if (hasStation) actions |= PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE;
        b.setActions(actions);

        // Custom actions fill, in order: the previous slot, the next slot, custom 1, custom 2.
        if (hasStation) {
            b.addCustomAction(custom(ACTION_NEXT, "Next station", R.drawable.ic_player_next_station));
        }
        switch (micState) {
            case MIC_ON:
                b.addCustomAction(custom(ACTION_MIC, "Mic on. Turn your mic off", R.drawable.ic_player_mic));
                break;
            case MIC_OFF:
                b.addCustomAction(custom(ACTION_MIC, "Mic off. Turn your mic on", R.drawable.ic_player_mic_off));
                break;
            case MIC_ALLOW:
                b.addCustomAction(custom(ACTION_MIC, "Allow mic", R.drawable.ic_player_mic_off));
                break;
            default: break;
        }
        if (hasStation) {
            b.addCustomAction(custom(ACTION_MUTE, sel.mute ? "Unmute " + sel.name : "Mute " + sel.name,
                                     sel.mute ? R.drawable.ic_player_speaker_off : R.drawable.ic_player_speaker));
            b.addCustomAction(custom(ACTION_SOLO,
                sel.solo ? "Release solo " + sel.name
                         : "Solo " + sel.name + ": only " + sel.name + " hears you, others dimmed",
                sel.solo ? R.drawable.ic_player_solo_on : R.drawable.ic_player_solo));
        }
        return b.build();
    }

    private static PlaybackState.CustomAction custom(String id, String name, int icon) {
        return new PlaybackState.CustomAction.Builder(id, name, icon).build();
    }

    /** Mic button: "Allow mic" opens the app (unlock required); otherwise toggles the mic. */
    private void micPressed() {
        if (micState == MIC_ALLOW) {
            try {
                openAppIntent().send();
            } catch (PendingIntent.CanceledException e) {
                android.util.Log.w("Crosspoint", "could not open the app", e);
            }
            return;
        }
        nativeAction(NATIVE_MIC);
    }

    // ------------------------------------------------------------ artwork (section 12.4)

    /** A key that changes exactly when the artwork would. */
    private int artworkKey(Station s) {
        return tileColor(s, live);
    }

    /**
     * Section 12.4 tile colour. Muted: the colour at 35% over panel. Lost: ink4. No
     * station: panel. Your voice reaches a station: the whole tile blends toward
     * live (55% station, 45% live), because Android 13+ scales the artwork to the
     * whole player background, where an inset border shows only as edge strips.
     */
    private static int tileColor(Station s, boolean voice) {
        if (s == null) return voice ? blend(LIVE, PANEL, 0.55f) : PANEL;
        return tileColorFor(s.colorIndex, s.presence, s.mute, voice || s.hearsYou);
    }

    /**
     * The same rule, by value, so the Android Auto browse list (which reads the
     * pushed JSON rather than a Station) draws identical icons (section 13.1:
     * offline is ink4, muted is the colour at 35% over panel, live blends 55/45).
     */
    static int tileColorFor(int colorIndex, String presence, boolean mute, boolean voice) {
        int c;
        if ("lost".equals(presence) || "offline".equals(presence)) c = INK4;
        else {
            c = STATION_COLORS[Math.abs(colorIndex) % STATION_COLORS.length];
            if (mute) c = blend(PANEL, c, 0.35f);
        }
        return voice ? blend(LIVE, c, 0.55f) : c;
    }

    /**
     * 512x512 rounded tile with a small white Crosspoint mark top-left, about 18% of the
     * tile (design/icon/crosspoint-notification.svg: the route in from the left and out of
     * the bottom, and the lit node). Small and off-centre because Android 13+ scales and
     * crops the artwork into the whole player background.
     */
    private Bitmap artwork(Station s, boolean voice) {
        final int size = 512;
        Bitmap bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(bmp);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(tileColor(s, voice));
        c.drawRoundRect(0, 0, size, size, 96, 96, p);

        // The mark on the 24-unit grid, scaled to 92 px, in the top-left quadrant.
        final float k = 92f / 24f, ox = 40f, oy = 40f;
        p.setColor(Color.WHITE);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2.4f * k);
        p.setStrokeCap(Paint.Cap.ROUND);
        c.drawLine(ox + 3 * k, oy + 12 * k, ox + 6.4f * k, oy + 12 * k, p);
        c.drawLine(ox + 12 * k, oy + 17.6f * k, ox + 12 * k, oy + 21 * k, p);
        p.setStyle(Paint.Style.FILL);
        Path ring = new Path();
        ring.setFillType(Path.FillType.EVEN_ODD);
        ring.addCircle(ox + 12 * k, oy + 12 * k, 4.4f * k, Path.Direction.CW);
        ring.addCircle(ox + 12 * k, oy + 12 * k, 1.7f * k, Path.Direction.CW);
        c.drawPath(ring, p);
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

    /**
     * The MediaStyle notification that replaces the section 4 one (section 12.1).
     * Android 13+ draws the player from the session; Android 12 and older draw
     * these actions, so they mirror the session's.
     */
    Notification buildNotification(String channelId, PendingIntent contentIntent) {
        Notification.Builder b = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            ? new Notification.Builder(ctx, channelId)
            : new Notification.Builder(ctx);

        final Station sel = selectedStation();
        final List<Integer> compact = new ArrayList<>();
        int n = 0;
        if (sel != null) {
            b.addAction(action(listening ? R.drawable.ic_player_pause : R.drawable.ic_player_play,
                               listening ? "Pause listening" : "Resume listening", NATIVE_LISTEN));
            n++;
            b.addAction(action(R.drawable.ic_player_next_station, "Next station", NATIVE_NEXT));
            compact.add(n++);
        }
        if (micState != MIC_HIDDEN) {
            final boolean on = micState == MIC_ON;
            b.addAction(micState == MIC_ALLOW
                ? new Notification.Action.Builder(Icon.createWithResource(ctx, R.drawable.ic_player_mic_off), "Allow mic", openAppIntent()).build()
                : action(on ? R.drawable.ic_player_mic : R.drawable.ic_player_mic_off,
                         on ? "Mic on. Turn your mic off" : "Mic off. Turn your mic on", NATIVE_MIC));
            compact.add(0, n++);
        }
        if (sel != null) {
            b.addAction(action(sel.mute ? R.drawable.ic_player_speaker_off : R.drawable.ic_player_speaker,
                               sel.mute ? "Unmute " + sel.name : "Mute " + sel.name, NATIVE_MUTE));
            compact.add(n++);
            b.addAction(action(sel.solo ? R.drawable.ic_player_solo_on : R.drawable.ic_player_solo,
                               sel.solo ? "Release solo " + sel.name
                                        : "Solo " + sel.name + ": only " + sel.name + " hears you, others dimmed", NATIVE_SOLO));
            n++;
        }

        Notification.MediaStyle style = new Notification.MediaStyle();
        if (session != null) style.setMediaSession(session.getSessionToken());
        int[] show = new int[Math.min(3, compact.size())];
        for (int i = 0; i < show.length; i++) show[i] = compact.get(i);
        if (show.length > 0) style.setShowActionsInCompactView(show);

        b.setStyle(style)
         .setSmallIcon(R.drawable.ic_stat_crosspoint)
         .setContentTitle(titleText())
         .setContentText(subtitle)
         .setSubText(albumText())
         .setColor(live ? LIVE : 0xFF2E6B66)
         .setOngoing(true)
         .setOnlyAlertOnce(true)
         .setShowWhen(false)
         .setVisibility(Notification.VISIBILITY_PUBLIC)
         .setContentIntent(contentIntent);
        return b.build();
    }

    private Notification.Action action(int icon, String title, int code) {
        return new Notification.Action.Builder(Icon.createWithResource(ctx, icon), title, serviceIntent(code)).build();
    }

    private void nativeAction(int code) {
        SonoBusActivity.nativePlayerButton(code);
    }

    private PendingIntent openAppIntent() {
        Intent open = new Intent(ctx, SonoBusActivity.class);
        open.setAction(Intent.ACTION_MAIN);
        open.addCategory(Intent.CATEGORY_LAUNCHER);
        open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) flags |= PendingIntent.FLAG_IMMUTABLE;
        return PendingIntent.getActivity(ctx, 0, open, flags);
    }

    /** A notification button: a service start. getForegroundService exists from API 26; minSdk is 24. */
    private PendingIntent serviceIntent(int code) {
        Intent i = new Intent(ctx, SonoBusService.class);
        i.setAction(SonoBusService.ACTION_MEDIA);
        i.putExtra("code", code);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) flags |= PendingIntent.FLAG_IMMUTABLE;
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
            ? PendingIntent.getForegroundService(ctx, 9000 + code, i, flags)
            : PendingIntent.getService(ctx, 9000 + code, i, flags);
    }
}
