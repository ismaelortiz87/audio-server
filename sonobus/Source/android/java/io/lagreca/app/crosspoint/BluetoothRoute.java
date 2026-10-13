// SPDX-License-Identifier: GPLv3-or-later

package io.lagreca.app.crosspoint;

import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
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
 * connected Bluetooth device, and engages or releases the route. A change is
 * pushed to native (nativeBtRoute), which surfaces it as
 * platform.bluetoothRoute. While it holds the communication route it sets
 * PlatformMonitor.appOwnsAudioMode so the call detector (A14) does not mistake
 * it for a VoIP call.
 *
 * The Bluetooth *type* constants of AudioDeviceInfo changed names across SDK
 * releases, so they are resolved here at runtime (see the type() helper)
 * instead of being referenced at compile time.
 */
final class BluetoothRoute {

    private static final long POLL_MS = 1000;
    private static final String TAG = "Crosspoint";

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
        sHeld = false;
    }

    // ---------------------------------------------------------------- tick

    private static void tick() {
        final AudioManager am = (AudioManager) sApp.getSystemService(Context.AUDIO_SERVICE);
        if (am == null) return;

        // The setting, from the engine ("phone" | "headset").
        final int desired;
        try {
            desired = "headset".equals(SonoBusActivity.nativeBtMicSetting()) ? 1 : 0;
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

        final boolean wantRoute = connected && desired == 1 && !inRealCall;

        if (wantRoute && !sHeld) engage(am, le, bt);
        else if (!wantRoute && sHeld) releaseRoute();

        // Report the route to native only when it changed (name or stereo-ness).
        final String name = (connected && desired == 1) ? nameOf(bt) : null;
        if (!same(name, le)) push(name, le);
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
    private static void engage(AudioManager am, boolean le, AudioDeviceInfo dev) {
        PlatformMonitor.appOwnsAudioMode = true;   // A14: do not count this as a call
        if (Build.VERSION.SDK_INT >= 31 && dev != null) {
            // setCommunicationDevice covers both classic and LE (it switches the
            // SCO channel to the device). It does not exist before API 31.
            try {
                if (am.setCommunicationDevice(dev)) {
                    sHeld = true;
                    return;
                }
            } catch (Throwable ignored) { }
        }
        classicSco(am);
    }

    /** The classic Bluetooth SCO call channel (API 24+; the API 31+ fallback). */
    private static void classicSco(AudioManager am) {
        try {
            am.setMode(AudioManager.MODE_IN_COMMUNICATION);
            am.setBluetoothScoOn(true);
            am.startBluetoothSco();
            sHeld = true;
        } catch (Throwable t) {
            Log.w(TAG, "could not start the Bluetooth call channel", t);
        }
    }

    /** Back to normal media routing and the phone mic (mic on/off state untouched). */
    static void releaseRoute() {
        if (sApp != null) {
            final AudioManager am = (AudioManager) sApp.getSystemService(Context.AUDIO_SERVICE);
            if (am != null) {
                if (Build.VERSION.SDK_INT >= 31) {
                    try { am.clearCommunicationDevice(); }
                    catch (Throwable t) { am.setMode(AudioManager.MODE_NORMAL); }
                }
                am.stopBluetoothSco();
                am.setBluetoothScoOn(false);
                am.setMode(AudioManager.MODE_NORMAL);
            }
        }
        PlatformMonitor.appOwnsAudioMode = false;
        sHeld = false;
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
