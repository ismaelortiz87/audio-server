// SPDX-License-Identifier: GPLv3-or-later

package io.lagreca.app.crosspoint;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

/**
 * A22 (docs/design/android.md section 14): the "Bluetooth mic" setting.
 *
 * With classic Bluetooth, Android plays media over the stereo A2DP channel but
 * captures from the phone's mic. The headset's mic is only reachable through the
 * Bluetooth call channel (SCO), which carries audio both ways in mono at call
 * quality. LE Audio devices can do stereo and mic together and keep stereo.
 *
 * The engine owns the mic on/off state (it never changes here, android.md 6);
 * this controller only switches the audio *route*: when the setting is
 * "headset" and a Bluetooth headset or car is connected it moves audio to the
 * communication route (SCO on API <31, setCommunicationDevice on API 31+), and
 * back to normal media routing when it is "phone" or the device leaves. A real
 * call always owns the route, so we never touch it.
 *
 * It runs on the main looper (the PlatformMonitor pattern): once a second it
 * reads the desired setting (from the engine via nativeBtMicSetting), checks the
 * connected Bluetooth device, and engages or releases the route. Two things go
 * to native: the *connected device* (nativeBtDevice →
 * platform.bluetoothDevice), which is what offers the Settings row at all, and
 * the *route actually taken* (nativeBtRoute → platform.bluetoothRoute). The row
 * must not depend on the route: that is only published once the setting is
 * already "headset", so gating on it would hide the control that sets it.
 *
 * LE Audio (android.md 14) keeps the stereo media route untouched: it can carry
 * media and the mic together, so there is nothing to switch. Only the classic
 * A2DP/SCO case (stereo media, but the mic only on the call channel) actually
 * moves audio to the communication route. While we hold either, we set
 * PlatformMonitor.appOwnsAudioMode so the call detector (A14) does not mistake
 * it for a VoIP call.
 *
 * A22 (on-demand): holding the communication route forces the narrowband mono
 * call path whether or not anything is captured, so the route is held only while
 * capture is actually wanted. The engine reports (one bitmask per tick) whether it
 * is transmitting, whether any station is soloed, and two policies the user sets:
 * whether a solo engages the route at all, and whether a mute releases it. When
 * nothing is wanted, the route is released after a few seconds of silence and
 * playback returns to the stereo A2DP channel — the mono/stereo question is a
 * property of the route, not of capturing, which is why it is decided here.
 * The release is reported to native only once the platform has really left the
 * call route: native reopens the engine's audio device from that signal, and
 * doing it while the SCO link tears down rebinds capture to a dying channel.
 *
 * The Bluetooth *type* constants of AudioDeviceInfo changed names across SDK
 * releases, so they are resolved here at runtime (see the type() helper)
 * instead of being referenced at compile time.
 */
final class BluetoothRoute {

    private static final long POLL_MS = 1000;
    private static final String TAG = "Crosspoint";

    // A22 (on-demand route): hold the route this long after capture stops, so an
    // ordinary pause in a conversation does not pay an SCO teardown + setup (each
    // ~0.2-0.5 s, plus a device reopen in the engine).
    private static final long RELEASE_HOLD_MS = 3000;
    // Selecting the communication device is not the same as the link being up: the
    // SCO takes a moment to come up, and the engine's reopen must not land before
    // it does (that is what left the buds mic silent on a later unmute). Bound, so
    // a platform that never reports the link live still gets the route reported.
    private static final long ENGAGE_SETTLE_MS = 3000;
    // ...and wait at most this long for the platform to really leave the call
    // route before telling native. Bounded, so a platform that never reports a
    // clean state cannot leave the UI showing a route we already left. Generous:
    // the HFP teardown plus the A2DP restart is the slow part of the switch.
    private static final long RELEASE_SETTLE_MS = 4000;
    // A failed engage is retried with a back-off instead of on every tick: each
    // attempt sets the voice mode and undoes it again, and flipping the mode once a
    // second re-routes the phone's audio each time (audible glitches). 2 s, 4 s, ...
    // capped at 30 s; reset on success and whenever the route stops being wanted.
    private static final long ENGAGE_RETRY_MIN_MS = 2000;
    private static final long ENGAGE_RETRY_MAX_MS = 30000;

    // The engine's answer to "is capture wanted, and under which policies"
    // (one int per tick): see Signals::BtRouteFlag in AndroidPlatform.h.
    private static final int F_MIC_TX          = 1 << 0;   // transmitting (mic on / PTT held)
    private static final int F_ANY_SOLO        = 1 << 1;   // any station soloed
    private static final int F_SOLO_ENGAGES    = 1 << 2;   // policy: solo may engage the route
    private static final int F_RELEASE_ON_MUTE = 1 << 3;   // policy: a mute releases the route
    private static final int F_PTT_MODE        = 1 << 4;   // "not transmitting" = released key
    private static final int F_OFF_BY_CALL     = 1 << 5;   // a call turned the mic off (A14)
    private static final int F_MIC_BLOCKED     = 1 << 6;   // not granted (A9)

    // AudioDeviceInfo type values, resolved at runtime so this file compiles
    // against any SDK release and runs on API 24+.
    private static final int T_BT_SCO      = type("TYPE_BLUETOOTH_SCO", "OUTPUT_DEVICE_BLUETOOTH_SCO");
    private static final int T_BT_A2DP     = type("TYPE_BLUETOOTH_A2DP", "OUTPUT_DEVICE_BLUETOOTH_A2DP");
    private static final int T_BT_HEADSET  = type("TYPE_BLUETOOTH_HEADSET", "OUTPUT_DEVICE_BLUETOOTH_HEADSET");
    private static final int T_BT_HEADPH   = type("TYPE_BLUETOOTH_A2DP_HEADPHONES", "OUTPUT_DEVICE_BLUETOOTH_A2DP_HEADPHONES");
    private static final int T_BLE_HEADSET = type("TYPE_BLE_HEADSET", "OUTPUT_DEVICE_BLE_HEADSET");
    private static final int T_BLE_A2DP    = type("TYPE_BLE_A2DP", "OUTPUT_DEVICE_BLE_A2DP");
    private static final int T_BLE_HEADPH  = type("TYPE_BLE_A2DP_HEADPHONES", "OUTPUT_DEVICE_BLE_A2DP_HEADPHONES");

    private static Context sApp = null;
    private static Handler sHandler = null;

    private static String sLastRoute = null;   // last route name pushed to native (null = none)
    private static boolean sLastStereo = false;
    private static boolean sHeld = false;      // we currently hold the communication route
    // T1: did *we* change the audio mode (and select the communication device)? Only
    // then may releaseRoute() undo them. The LE path holds the route without touching
    // either, and a release that still called setMode(MODE_NORMAL) would reset the mode
    // underneath another app's live VoIP call.
    private static boolean sOwnsMode = false;
    private static String sLastDevice = null;  // last *connected* device pushed (null = none)
    private static boolean sLastDeviceLe = false;

    // A22 (on-demand route): when the held route may be released (0 = not
    // scheduled), and whether we released it but have not told native yet because
    // the platform is still on the call route. Native is told only once it really
    // left: that signal makes the engine reopen its audio device, and reopening
    // while the SCO link is tearing down lands on a dying channel (audible noise).
    private static long sReleaseAt = 0;
    private static boolean sReleasePending = false;
    private static long sReleaseDeadline = 0;
    // A22: the route is selected but the platform is not on it yet (the SCO link is
    // still coming up). Native learns the route only when this clears, so the
    // engine's reopen lands on a live call channel.
    private static boolean sEngagePending = false;
    private static long sEngageDeadline = 0;
    private static int sEngageFailures = 0;    // consecutive failed engages
    private static long sEngageRetryAt = 0;    // no engage attempt before this (uptime ms)

    static synchronized void start(Context ctx) {
        if (sApp != null) return;
        sApp = ctx.getApplicationContext();
        sHandler = new Handler(Looper.getMainLooper());
        sHandler.post(new Runnable() {
            @Override public void run() {
                tick();
                sHandler.postDelayed(this, POLL_MS);
            }
        });
    }

    static synchronized void stop() {
        if (sApp == null) return;
        releaseRoute();
        if (sHandler != null) sHandler.removeCallbacksAndMessages(null);
        sHandler = null;
        sApp = null;
        sLastRoute = null;
        sLastStereo = false;
        sLastDevice = null;
        sLastDeviceLe = false;
        sHeld = false;
        sOwnsMode = false;
        sReleaseAt = 0;
        sReleasePending = false;
        sReleaseDeadline = 0;
        sEngagePending = false;
        sEngageDeadline = 0;
        sEngageFailures = 0;
        sEngageRetryAt = 0;
    }

    // ---------------------------------------------------------------- tick

    private static void tick() {
        final AudioManager am = (AudioManager) sApp.getSystemService(Context.AUDIO_SERVICE);
        if (am == null) return;

        // The setting, from the engine ("phone" | "headset"), and the on-demand
        // route state (one bitmask: transmitting / soloed / the two policies).
        final int desired;
        final int flags;
        try {
            desired = "headset".equals(SonoBusActivity.nativeBtMicSetting()) ? 1 : 0;
            final int fl = SonoBusActivity.nativeBtRouteFlags();
            // A zero bitmask means the engine's controller has not mirrored yet (or
            // there is no engine): treat the two policies as their defaults (on),
            // instead of reading "release on mute off" out of an empty int.
            flags = fl == 0 ? (F_SOLO_ENGAGES | F_RELEASE_ON_MUTE) : fl;
        } catch (Throwable t) {
            return;      // native not up yet
        }

        final AudioDeviceInfo bt = findBt(am);
        final boolean le = bt != null && isLeType(bt.getType());
        final boolean connected = bt != null;

        // A real call owns the route: never override it (android.md 6, A14). That
        // includes another app's VoIP call: it holds MODE_IN_COMMUNICATION, and
        // engaging on top of it would take the mic and the speakers out from under
        // their call. `sHeld` separates their hold from ours — our own engage is the
        // only IN_COMMUNICATION we may override. A ringing call counts too: the
        // user may answer it in a second.
        final int mode = am.getMode();
        final boolean inRealCall = mode == AudioManager.MODE_IN_CALL
            || mode == AudioManager.MODE_RINGTONE
            || (Build.VERSION.SDK_INT >= 30 && mode == AudioManager.MODE_CALL_SCREENING)
            || (mode == AudioManager.MODE_IN_COMMUNICATION && !sHeld);

        // Is capture wanted right now? Solo only counts when the policy allows it:
        // a solo is a persistent state, and while the route is held playback is
        // mono, so the user decides whether soloing is worth that.
        final boolean micTx = (flags & F_MIC_TX) != 0;
        final boolean micLive = micTx
            || ((flags & F_SOLO_ENGAGES) != 0 && (flags & F_ANY_SOLO) != 0);

        // An explicit mute (open mode, mic off, nothing else responsible) may hold
        // the route: with "release on mute" off, unmuting is instant — at the price
        // of staying on the mono call route while muted. In PTT mode "not
        // transmitting" is a released key, not a mute, so the mute rule cannot
        // apply; a call or a denied permission never holds it (release is right).
        final boolean muted = !micTx && (flags & F_PTT_MODE) == 0
            && (flags & (F_OFF_BY_CALL | F_MIC_BLOCKED)) == 0;
        final boolean holdThroughMute = muted && (flags & F_RELEASE_ON_MUTE) == 0;

        final boolean wantRoute = connected && desired == 1 && !inRealCall
                                  && (micLive || holdThroughMute);

        final long now = SystemClock.uptimeMillis();

        if (!wantRoute) {
            // A new request gets a prompt first attempt.
            sEngageFailures = 0;
            sEngageRetryAt = 0;
        }

        if (wantRoute && !sHeld && now >= sEngageRetryAt) {
            engage(am, le);
            if (sHeld) {
                sEngageFailures = 0;
                sEngageRetryAt = 0;
            } else {
                sEngageFailures++;
                final long wait = Math.min(ENGAGE_RETRY_MAX_MS,
                    ENGAGE_RETRY_MIN_MS << Math.min(sEngageFailures - 1, 4));
                sEngageRetryAt = now + wait;
                Log.w(TAG, "Bluetooth route: engage failed (" + sEngageFailures
                    + " in a row), next attempt in " + (wait / 1000) + " s");
            }
            sReleaseAt = 0;
            sReleasePending = false;    // we are back on the call route: report it
            sReleaseDeadline = 0;
            // Selecting the device is not the link being up: the SCO takes a moment.
            // Native is told the route only when the platform is really on it, so the
            // engine's reopen binds the buds mic instead of the phone mic. LE Audio
            // switches nothing, so it is live at once.
            sEngagePending = sHeld && !le;
            sEngageDeadline = now + ENGAGE_SETTLE_MS;
            if (sEngagePending)
                Log.i(TAG, "Bluetooth route: selected, waiting for the call link to come up");
        } else if (!wantRoute && sHeld) {
            // Nothing wanted: hold for a few seconds first, so a normal pause in a
            // conversation does not cost an SCO teardown and setup.
            sEngagePending = false;
            if (sReleaseAt == 0) {
                sReleaseAt = now + RELEASE_HOLD_MS;
                Log.i(TAG, "Bluetooth route: nothing wanted, holding "
                    + (RELEASE_HOLD_MS / 1000) + " s before releasing");
            } else if (now >= sReleaseAt) {
                releaseRoute();             // platform side: back to media (A2DP)
                sReleaseAt = 0;
                sReleasePending = true;     // native is told once the link is down
                sReleaseDeadline = now + RELEASE_SETTLE_MS;
            }
        }

        // A22: the route is reported to native only once the platform has really
        // moved, and that report is what makes the engine reopen its audio device —
        // in both directions, so the streams follow the route instead of staying
        // bound to the device they were created against.
        if (sEngagePending && (routeIsLive(am) || now >= sEngageDeadline)) {
            sEngagePending = false;
            Log.i(TAG, "Bluetooth route live (platform live=" + routeIsLive(am)
                + ", mediaOut=" + currentMediaOutputType(am)
                + ", timedOut=" + (now >= sEngageDeadline) + ")");
        }
        if (sReleasePending && (routeIsClear(am) || now >= sReleaseDeadline)) {
            sReleasePending = false;
            Log.i(TAG, "Bluetooth route released (platform clear=" + routeIsClear(am)
                + ", mediaOut=" + currentMediaOutputType(am)
                + ", timedOut=" + (now >= sReleaseDeadline) + ")");
        }

        // Report the connected device separately from the route below. With the
        // setting on "phone" there is no route to report, yet the Settings row
        // that offers "Headset" has to be there — gating that row on the route
        // would hide the only control that sets it (android.md 14). `le` is the
        // LE Audio flag, which the route reports as `stereo`.
        final String dev = connected ? nameOf(bt) : null;
        if (!sameDevice(dev, le)) pushDevice(dev, le);

        // Report the route actually taken. While a release is still settling, the
        // previous value stands: pushing null early is what told the engine to
        // reopen onto a channel that was already going away.
        if (!sReleasePending && !sEngagePending) {
            // Report a route only while we really hold it: a failed engage must not
            // claim one (the engine reopens its device on this signal, and the UI's
            // mono note keys off it).
            final boolean held = wantRoute && sHeld;
            final String name = held ? nameOf(bt) : null;
            if (!same(name, held && le)) push(name, held && le);
        }
    }

    private static boolean sameDevice(String name, boolean le) {
        return (name == null ? sLastDevice == null : name.equals(sLastDevice)) && le == sLastDeviceLe;
    }

    private static void pushDevice(String name, boolean le) {
        sLastDevice = name;
        sLastDeviceLe = le;
        try {
            SonoBusActivity.nativeBtDevice(name, le);
        } catch (Throwable t) {
            // ignore: the engine is tearing down
        }
    }

    private static boolean same(String name, boolean stereo) {
        return (name == null ? sLastRoute == null : name.equals(sLastRoute)) && stereo == sLastStereo;
    }

    private static void push(String name, boolean stereo) {
        sLastRoute = name;
        sLastStereo = stereo;
        try {
            SonoBusActivity.nativeBtRoute(name, stereo);
        } catch (Throwable t) {
            // ignore: the engine is tearing down
        }
    }

    // ---------------------------------------------------------------- route

    /** Move audio to the communication route on the given Bluetooth device. */
    private static void engage(AudioManager am, boolean le) {
        if (le) {
            // LE Audio (android.md 14) carries the stereo media and the mic
            // together, so there is nothing to switch: the route stays where it is.
            // Note what that means for the release: sOwnsMode stays false, so
            // releaseRoute() only drops the hold and touches no AudioManager setter.
            sHeld = true;
            return;
        }
        if (Build.VERSION.SDK_INT >= 31) {
            // Classic A2DP: media plays on the stereo music channel, but the mic
            // only exists on the call channel. The device to select is NOT the
            // A2DP output findBt() found (setCommunicationDevice rejects it and it
            // has no mic) but the one the platform offers for communication —
            // SCO, or a BLE headset. Selecting it also makes the framework bring
            // the SCO link up (startBluetoothSco() is deprecated from API 31).
            final AudioDeviceInfo comm = findCommunicationDevice(am);
            if (comm != null) {
                try {
                    am.setMode(AudioManager.MODE_IN_COMMUNICATION);
                    // A14: the mode is ours, not a call. Set only where we really call
                    // setMode (the LE path above switches nothing and must not claim
                    // it, or another app's VoIP call would go undetected). T1: it is
                    // also what releaseRoute() needs to know before undoing the mode.
                    PlatformMonitor.appOwnsAudioMode = true;
                    sOwnsMode = true;
                    if (am.setCommunicationDevice(comm)) {
                        sHeld = true;
                        Log.i(TAG, "Bluetooth route engaged: type=" + comm.getType()
                            + " commDevice=" + am.getCommunicationDevice()
                            + " scoOn=" + am.isBluetoothScoOn());
                        return;
                    }
                    Log.w(TAG, "setCommunicationDevice(" + comm.getType() + ") was refused");
                } catch (Throwable t) {
                    Log.w(TAG, "engaging the communication route failed", t);
                }
            } else {
                final StringBuilder sb = new StringBuilder();
                try {
                    for (AudioDeviceInfo d : am.getAvailableCommunicationDevices())
                        sb.append(d.getType()).append(' ');
                } catch (Throwable t) {
                    sb.append("listing failed: ").append(t);
                }
                Log.w(TAG, "no Bluetooth communication device offered for a call route; offered: " + sb);
            }
        }
        if (!classicSco(am)) {
            undoFailedEngage(am);
        }
    }

    /**
     * T2: an engage that fails *after* the app called setMode() would leave the phone in
     * voice mode with the mode owned by us and no hold. The next tick's call guard reads
     * `MODE_IN_COMMUNICATION && !sHeld` as another app's VoIP call, so the route would
     * never be retried, never released (releaseRoute() needs sHeld, stop() goes through
     * it too), and A14 would stop detecting real calls for the rest of the session.
     * Undo exactly what the failed attempt changed, and only that.
     */
    private static void undoFailedEngage(AudioManager am) {
        if (sOwnsMode) {
            try {
                // Only if it is still *our* voice mode: another app may have taken it
                // over in the meantime, and its call must not be reset.
                if (am.getMode() == AudioManager.MODE_IN_COMMUNICATION)
                    am.setMode(AudioManager.MODE_NORMAL);
            } catch (Throwable t) {
                Log.w(TAG, "could not restore the audio mode after a failed engage", t);
            }
        }
        PlatformMonitor.appOwnsAudioMode = false;
        sOwnsMode = false;
        sHeld = false;
    }

    /**
     * The Bluetooth device the platform offers for a call, or null. On API 31+
     * this is what setCommunicationDevice accepts; the A2DP output used for media
     * has no microphone and is not a communication device.
     */
    private static AudioDeviceInfo findCommunicationDevice(AudioManager am) {
        if (Build.VERSION.SDK_INT < 31) return null;
        try {
            for (AudioDeviceInfo d : am.getAvailableCommunicationDevices()) {
                final int t = d.getType();
                if (t == T_BT_SCO || t == T_BLE_HEADSET) return d;
            }
        } catch (Throwable t) {
            Log.w(TAG, "could not list the communication devices", t);
        }
        return null;
    }

    /**
     * The classic Bluetooth SCO call channel (API 24+; the API 31+ fallback).
     * Returns whether the app now holds the route (T2: the caller undoes its own mode
     * change when this fails).
     */
    private static boolean classicSco(AudioManager am) {
        try {
            am.setMode(AudioManager.MODE_IN_COMMUNICATION);
            PlatformMonitor.appOwnsAudioMode = true;   // A14: this mode is ours
            sOwnsMode = true;                          // T1: and releaseRoute() may undo it
            am.startBluetoothSco();
            am.setBluetoothScoOn(true);
            sHeld = true;
            Log.i(TAG, "Bluetooth route engaged via SCO: scoOn=" + am.isBluetoothScoOn()
                + " mode=" + am.getMode());
            return true;
        } catch (Throwable t) {
            Log.w(TAG, "could not start the Bluetooth call channel", t);
            return false;
        }
    }

    /** Back to normal media routing and the phone mic (mic on/off state untouched). */
    static void releaseRoute() {
        // Nothing of ours to give back: leave the platform completely alone. Two cases
        // land here and neither may touch an AudioManager setter. stop() calls this
        // unconditionally on the way out, and a teardown from there would take down
        // whoever *does* hold the route — another app's VoIP call
        // (MODE_IN_COMMUNICATION), a ringing call, or a live call's SCO link. And the LE
        // path holds the route without having changed the mode or the communication
        // device at all (T1), so undoing them would reset platform state the app never
        // set — the same clobber, one hop away.
        if (sOwnsMode && sApp != null) {
            final AudioManager am = (AudioManager) sApp.getSystemService(Context.AUDIO_SERVICE);
            if (am != null) {
                final int mode = am.getMode();
                final boolean ours = mode != AudioManager.MODE_IN_CALL
                    && mode != AudioManager.MODE_RINGTONE
                    && (Build.VERSION.SDK_INT < 30 || mode != AudioManager.MODE_CALL_SCREENING);
                if (Build.VERSION.SDK_INT >= 31 && ours) {
                    try { am.clearCommunicationDevice(); }
                    catch (Throwable t) { }
                }
                am.stopBluetoothSco();
                am.setBluetoothScoOn(false);
                if (ours) am.setMode(AudioManager.MODE_NORMAL);
            }
        }
        PlatformMonitor.appOwnsAudioMode = false;
        sOwnsMode = false;
        sHeld = false;
    }

    /**
     * A22: has the platform really left the call route? No communication device
     * selected, the SCO link down, and — API 33+ — media output no longer on the
     * call-channel device, i.e. the media route has actually re-established. The
     * engine reopens its streams from this report, and doing that in the middle of
     * the profile switch leaves them bound to the call channel, which is heard as
     * a frozen input until something reopens them again.
     */
    private static boolean routeIsClear(AudioManager am) {
        try {
            if (Build.VERSION.SDK_INT >= 31 && am.getCommunicationDevice() != null)
                return false;
            if (am.isBluetoothScoOn())
                return false;
            if (Build.VERSION.SDK_INT >= 33 && currentMediaOutputType(am) == T_BT_SCO)
                return false;
            return true;
        } catch (Throwable t) {
            return true;     // cannot ask: do not stall the release
        }
    }

    /**
     * A22: is the platform really on the call route now? The mirror of
     * routeIsClear — a communication device selected (API 31+) or the classic SCO
     * link up. True when it cannot be asked, so an engage is never stalled.
     */
    private static boolean routeIsLive(AudioManager am) {
        try {
            if (Build.VERSION.SDK_INT >= 31 && am.getCommunicationDevice() != null)
                return true;
            return am.isBluetoothScoOn();
        } catch (Throwable t) {
            return true;     // cannot ask: report the route rather than stall it
        }
    }

    /** The type Android currently routes media output to — the system's own answer,
        which is the only way to see the A2DP route come back after a call route.
        0 when it cannot be told (API < 33 has no such call: the SCO flag is all). */
    private static int currentMediaOutputType(AudioManager am) {
        if (Build.VERSION.SDK_INT < 33) return 0;
        try {
            final AudioAttributes attrs = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA).build();
            final java.util.List<AudioDeviceInfo> list = am.getAudioDevicesForAttributes(attrs);
            if (list != null && !list.isEmpty()) return list.get(0).getType();
        } catch (Throwable t) {
            // unknown: treat as "cannot tell"
        }
        return 0;
    }

    // ---------------------------------------------------------------- detection

    /** The connected Bluetooth output device, or null when none is connected. */
    private static AudioDeviceInfo findBt(AudioManager am) {
        final AudioDeviceInfo[] outs = am.getDevices(1 /*AudioDeviceInfo.OUTPUT_DEVICES (API 21+);
            the value has been 1 since the constant was added, on every SDK release */);
        for (AudioDeviceInfo d : outs) {
            final int t = d.getType();
            if (isClassicType(t) || (Build.VERSION.SDK_INT >= 31 && isLeType(t)))
                return d;
        }
        return null;
    }

    private static boolean isClassicType(int t) {
        return t == T_BT_SCO || t == T_BT_A2DP || t == T_BT_HEADSET || t == T_BT_HEADPH;
    }

    /** LE Audio (BLE) can carry stereo media and a mic together: placement stays. */
    private static boolean isLeType(int t) {
        return t == T_BLE_HEADSET || t == T_BLE_A2DP || t == T_BLE_HEADPH;
    }

    /** The product name for platform.bluetoothRoute.name. */
    private static String nameOf(AudioDeviceInfo d) {
        final CharSequence n = d.getProductName();
        return (n != null && n.length() > 0) ? n.toString() : "Bluetooth";
    }

    /**
     * An AudioDeviceInfo type constant by name. The SDK jar that compiles this
     * may use the old names (OUTPUT_DEVICE_*) or the new ones (TYPE_*): try both
     * and keep whichever exists. Returns -1 when neither is there (never: both
     * names have been public since API 21).
     */
    private static int type(String a, String b) {
        final int v = constOr(-1, a);
        return v != -1 ? v : constOr(-1, b);
    }

    private static int constOr(int fallback, String name) {
        try {
            return AudioDeviceInfo.class.getField(name).getInt(null);
        } catch (Throwable t) {
            return fallback;
        }
    }
}
