// SPDX-License-Identifier: GPLv3-or-later

package io.lagreca.app.crosspoint;

import android.content.Context;
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
    // ...and wait at most this long for the platform to really leave the call
    // route before telling native. Bounded, so a platform that never reports a
    // clean state cannot leave the UI showing a route we already left.
    private static final long RELEASE_SETTLE_MS = 2500;

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
        sReleaseAt = 0;
        sReleasePending = false;
        sReleaseDeadline = 0;
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

        // A real call owns the route: never override it (android.md 6, A14).
        final int mode = am.getMode();
        final boolean inRealCall = mode == AudioManager.MODE_IN_CALL
            || (Build.VERSION.SDK_INT >= 30 && mode == AudioManager.MODE_CALL_SCREENING);

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

        if (wantRoute && !sHeld) {
            engage(am, le);
            sReleaseAt = 0;
            sReleasePending = false;    // we are back on the call route: report it
            sReleaseDeadline = 0;
        } else if (!wantRoute && sHeld) {
            // Nothing wanted: hold for a few seconds first, so a normal pause in a
            // conversation does not cost an SCO teardown and setup.
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
        // moved, and that report is what makes the engine reopen its audio device.
        // On release that means waiting for the communication/SCO link to go down.
        if (sReleasePending && (routeIsClear(am) || now >= sReleaseDeadline)) {
            sReleasePending = false;
            Log.i(TAG, "Bluetooth route released (platform clear=" + routeIsClear(am)
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
        if (!sReleasePending) {
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
        PlatformMonitor.appOwnsAudioMode = true;   // A14: do not count this as a call
        if (le) {
            // LE Audio (android.md 14) carries the stereo media and the mic
            // together, so there is nothing to switch: the route stays where it is.
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
        classicSco(am);
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

    /** The classic Bluetooth SCO call channel (API 24+; the API 31+ fallback). */
    private static void classicSco(AudioManager am) {
        try {
            am.setMode(AudioManager.MODE_IN_COMMUNICATION);
            am.startBluetoothSco();
            am.setBluetoothScoOn(true);
            sHeld = true;
            Log.i(TAG, "Bluetooth route engaged via SCO: scoOn=" + am.isBluetoothScoOn()
                + " mode=" + am.getMode());
        } catch (Throwable t) {
            Log.w(TAG, "could not start the Bluetooth call channel", t);
        }
    }

    /** Back to normal media routing and the phone mic (mic on/off state untouched). */
    static void releaseRoute() {
        if (sApp != null) {
            final AudioManager am = (AudioManager) sApp.getSystemService(Context.AUDIO_SERVICE);
            if (am != null) {
                final int mode = am.getMode();
                final boolean ours = mode != AudioManager.MODE_IN_CALL
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
        sHeld = false;
    }

    /**
     * A22: true once the platform is really back on the phone route — no
     * communication device selected and the SCO link down. API 31+ reports the
     * selection; older builds only have the SCO flag. Native is told about the
     * release only after this, so the engine's device reopen cannot land on a
     * tearing-down SCO channel (which it would read as noise).
     */
    private static boolean routeIsClear(AudioManager am) {
        try {
            if (Build.VERSION.SDK_INT >= 31 && am.getCommunicationDevice() != null)
                return false;
            return !am.isBluetoothScoOn();
        } catch (Throwable t) {
            return true;     // cannot ask: do not stall the release
        }
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
