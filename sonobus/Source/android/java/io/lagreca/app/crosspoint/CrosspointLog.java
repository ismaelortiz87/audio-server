package io.lagreca.app.crosspoint;

import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Process;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.Date;
import java.util.Locale;

/**
 * On-phone diagnostics: a capped log file, a crash dump per uncaught exception, and
 * session markers that say whether the previous run ended on purpose.
 *
 * <p><b>Why.</b> The hardware-only defects (B17-B21) are reported from the phone, where
 * {@code adb logcat} is not always at hand. Everything logged through here also goes to
 * logcat exactly as before, so nothing that reads logcat breaks; the difference is that
 * it lands in a file too:
 *
 * <ul>
 *   <li>{@code <external files>/logs/crosspoint.log} — the Java side (route transitions,
 *       media buttons, service lifecycle). Pull it with
 *       {@code adb pull /sdcard/Android/data/io.lagreca.app.crosspoint/files/logs/}.</li>
 *   <li>{@code crosspoint-native.log} in the same folder — the engine's lines, written by
 *       the native side (it is handed this folder once at start).</li>
 *   <li>{@code crash-java-<time>.txt} — an uncaught Java exception, with the stack trace
 *       and the last {@link #RING} log lines, written before the process dies.</li>
 *   <li>{@code crash-native-<time>.txt} — a fatal signal (SIGSEGV/SIGABRT/...), written by
 *       the native handler, which then re-raises so the tombstone still happens: keep
 *       {@code adb logcat -b crash} as the authority for native crashes, and read this file
 *       for the last thing the engine was doing.</li>
 * </ul>
 *
 * <p>The session markers are the cheap catch-all: {@code session start} when the service
 * comes up, {@code session clean exit} when it is destroyed. A run whose file has no clean
 * exit after its start ended abruptly — a crash, a force-stop, or the system killing the
 * process — and the lines above it say how far it got. That is deliberately worded as
 * "ended abruptly" rather than "crashed", because a system kill looks the same and a false
 * crash report is worse than none.
 *
 * <p>Nothing here may ever disturb the app: every step is guarded, and a failure to log
 * disables logging rather than throwing.
 */
public final class CrosspointLog {
    private static final String TAG = "Crosspoint";
    private static final String DIR_NAME = "logs";
    private static final String FILE_NAME = "crosspoint.log";
    private static final long MAX_BYTES = 2L * 1024 * 1024;   // rotate at 2 MB
    private static final int RING = 300;                       // lines kept for a crash dump
    private static final SimpleDateFormat STAMP =
        new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US);

    private static final Object sLock = new Object();
    private static final ArrayDeque<String> sRing = new ArrayDeque<>(RING);
    private static File sDir = null;
    private static File sFile = null;
    private static FileOutputStream sOut = null;
    private static long sBytes = 0;
    private static boolean sCrashHandlerInstalled = false;
    private static String sVersion = "";

    private CrosspointLog() {}

    /** The folder the on-phone logs live in, or null before {@link #start}. */
    public static File directory() {
        synchronized (sLock) { return sDir; }
    }

    /** The folder's absolute path, or null before {@link #start}. */
    public static String path() {
        synchronized (sLock) { return sDir != null ? sDir.getAbsolutePath() : null; }
    }

    /**
     * Call once, from the service's {@code onCreate}, before anything else logs. Safe to
     * call again: only the first call does work.
     */
    public static void start(Context ctx) {
        synchronized (sLock) {
            if (sFile != null) return;
            try {
                final File base = ctx.getExternalFilesDir(null);
                sDir = new File(base != null ? base : ctx.getFilesDir(), DIR_NAME);
                if (!sDir.exists() && !sDir.mkdirs()) { sDir = null; return; }
                sFile = new File(sDir, FILE_NAME);

                // What the previous run left behind, read before this one writes anything.
                final boolean existed = sFile.exists() && sFile.length() > 0;
                final String tail = existed ? readTail(sFile, 8192) : "";

                if (existed && sFile.length() > MAX_BYTES) rotate();
                sBytes = sFile.exists() ? sFile.length() : 0;
                sOut = new FileOutputStream(sFile, true);

                // Written through sOut, so it lands in the file, not just the ring.
                if (existed && !tail.contains("session clean exit")) {
                    writeRaw("=== previous session ended abruptly (" +
                        "crash, force-stop, or a system kill - see crash-*.txt and " +
                        "crosspoint-native.log) ===");
                }

                sVersion = describe(ctx);
                writeRaw("=== session start " + STAMP.format(new Date()) + " | " + sVersion + " ===");
                writeRaw("log file: " + sFile.getAbsolutePath());
            } catch (Throwable t) {
                sDir = null; sFile = null; sOut = null;
                return;      // logging is off; the app is unaffected
            }

            installCrashHandler();
        }
    }

    /** The service was destroyed: this run ended on purpose. */
    public static void cleanExit() {
        writeRaw("=== session clean exit " + STAMP.format(new Date()) + " ===");
    }

    // ------------------------------------------------------------- logcat + file

    public static void d(String tag, String msg) { Log.d(tag, msg); write(tag, "D", msg, null); }
    public static void i(String tag, String msg) { Log.i(tag, msg); write(tag, "I", msg, null); }
    public static void w(String tag, String msg) { Log.w(tag, msg); write(tag, "W", msg, null); }
    public static void e(String tag, String msg) { Log.e(tag, msg); write(tag, "E", msg, null); }

    public static void w(String tag, String msg, Throwable t) {
        Log.w(tag, msg, t);
        write(tag, "W", msg, t);
    }

    public static void e(String tag, String msg, Throwable t) {
        Log.e(tag, msg, t);
        write(tag, "E", msg, t);
    }

    /**
     * A line that is already a complete message with its own tag, e.g. from the native
     * side's breadcrumb mirror. Kept separate so callers cannot accidentally double-tag.
     */
    public static void note(String tag, String msg) { write(tag, "I", msg, null); }

    private static void write(String tag, String level, String msg, Throwable t) {
        final StringBuilder sb = new StringBuilder(msg == null ? "" : msg);
        if (t != null) {
            sb.append(" | ").append(t.getClass().getSimpleName());
            if (t.getMessage() != null) sb.append(": ").append(t.getMessage());
        }
        writeRaw(STAMP.format(new Date()) + " " + level + "/" + tag + ": " + sb);
        if (t != null) {
            final StringWriter sw = new StringWriter();
            t.printStackTrace(new PrintWriter(sw));
            for (String line : sw.toString().split("\n")) writeRaw("    " + line);
        }
    }

    /** One line to the file and into the ring. Never throws. */
    private static void writeRaw(String line) {
        synchronized (sLock) {
            sRing.addLast(line);
            while (sRing.size() > RING) sRing.removeFirst();
            if (sOut == null) return;
            try {
                sOut.write((line + "\n").getBytes("UTF-8"));
                sOut.flush();
                sBytes += line.length() + 1;
                if (sBytes > MAX_BYTES) {
                    sOut.close();
                    sOut = null;
                    rotate();
                    sBytes = sFile.exists() ? sFile.length() : 0;
                    sOut = new FileOutputStream(sFile, true);
                }
            } catch (Throwable t) {
                try { sOut.close(); } catch (Throwable ignored) {}
                sOut = null;      // stop trying; the ring still serves a crash dump
            }
        }
    }

    private static void rotate() {
        try {
            final File prev = new File(sDir, FILE_NAME + ".1");
            if (prev.exists() && !prev.delete()) return;
            if (!sFile.renameTo(prev)) return;
        } catch (Throwable ignored) { }
    }

    private static String readTail(File f, int max) {
        try (java.io.RandomAccessFile raf = new java.io.RandomAccessFile(f, "r")) {
            final long len = raf.length();
            final int n = (int) Math.min(len, max);
            final byte[] buf = new byte[n];
            raf.seek(len - n);
            raf.readFully(buf);
            return new String(buf, "UTF-8");
        } catch (Throwable t) {
            return "";
        }
    }

    private static String describe(Context ctx) {
        String version = "?";
        try {
            final android.content.pm.PackageInfo pi =
                ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0);
            version = pi.versionName + " (" + pi.versionCode + ")";
        } catch (PackageManager.NameNotFoundException ignored) { }
        return "app " + version
            + " | " + Build.MANUFACTURER + " " + Build.MODEL
            + " | Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")";
    }

    // ------------------------------------------------------------------- crashes

    private static void installCrashHandler() {
        if (sCrashHandlerInstalled) return;
        sCrashHandlerInstalled = true;
        final Thread.UncaughtExceptionHandler previous =
            Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override public void uncaughtException(Thread thread, Throwable error) {
                try { dumpJavaCrash(thread, error); } catch (Throwable ignored) { }
                if (previous != null) {
                    previous.uncaughtException(thread, error);     // normal death path
                } else {
                    Process.killProcess(Process.myPid());
                }
            }
        });
    }

    /** Writes crash-java-<time>.txt: the trace first, then how the app got there. */
    private static void dumpJavaCrash(Thread thread, Throwable error) {
        final File dir;
        final String version;
        final String ring;
        synchronized (sLock) {
            dir = sDir;
            version = sVersion;
            final StringBuilder sb = new StringBuilder();
            for (String l : sRing) sb.append(l).append('\n');
            ring = sb.toString();
        }
        if (dir == null) return;
        writeRaw("!!! uncaught " + error.getClass().getName() + " on thread '" +
            thread.getName() + "': " + error.getMessage());
        final String name = "crash-java-" + fileStamp() + ".txt";
        final File f = new File(dir, name);
        try (FileOutputStream out = new FileOutputStream(f, false)) {
            final StringWriter sw = new StringWriter();
            final PrintWriter pw = new PrintWriter(sw);
            pw.println("Crosspoint Java crash");
            pw.println("time    : " + STAMP.format(new Date()));
            pw.println("thread  : " + thread.getName() + " (id " + thread.getId() + ")");
            pw.println("build   : " + version);
            pw.println("log     : crosspoint.log and crosspoint-native.log, same folder");
            pw.println("note    : logcat -b crash has the tombstone only for a *native*");
            pw.println("          crash; this file is the Java one.");
            pw.println();
            pw.println("---- exception ----");
            error.printStackTrace(pw);
            pw.println();
            pw.println("---- last " + RING + " log lines ----");
            pw.print(ring);
            pw.flush();
            out.write(sw.toString().getBytes("UTF-8"));
            out.flush();
        } catch (Throwable ignored) { }
        writeRaw("crash dump written: " + f.getAbsolutePath());
    }

    private static String fileStamp() {
        return new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
    }
}
