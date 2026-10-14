/*
    A26 diagnostics: the engine's log file on the phone, and a native crash dump.

    The Java side creates <external files>/logs and hands the folder here once, from the
    app's start (SonoBusActivity.nativeSetLogDir, also called by the service). Everything
    the engine logs -- the JUCE Logger messages, including the "A22 route ...: reopened
    audio" line -- is appended to crosspoint-native.log in that folder, and mirrored to
    logcat unchanged, so `adb logcat -s JUCE` keeps working exactly as before.

    A fatal signal writes the last lines plus the signal number to
    crash-native-<time>.txt, then puts back the handler that was installed before ours
    and re-raises. Restoring the old handler is the whole point: bionic's handlers are
    what feed Android's tombstone pipeline, so re-raising with SIG_DFL instead kills the
    process silently and leaves `adb logcat -b crash` empty -- measured on API 34, which
    is why this is written the way it is. (JUCE's own
    SystemStats::setApplicationCrashHandler is no use either: it follows the user handler
    with kill(getpid(), SIGKILL), which produces no tombstone.)

    The signal handler is written for the environment it runs in: only open/write/close,
    snprintf into stack buffers, sigaction, and the re-raise. No juce::String, no
    allocation -- the crash path must not depend on a heap that may be the reason we are
    crashing.

    Everything here is best-effort and must never disturb the engine: if the folder is
    not writable the logger falls back to logcat only.
*/

#pragma once

#include <cstdio>
#include <cstring>
#include <ctime>
#include <csignal>

#if JUCE_ANDROID
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace crosspoint
{

class EngineLog : public juce::Logger
{
public:
    static EngineLog& instance()
    {
        static EngineLog log;
        return log;
    }

    /** Called once with the folder the Java side created. Empty = logcat only. */
    void initialise (const juce::String& folder)
    {
        if (folder.isEmpty() || file != nullptr)
            return;

        dir = folder;
        nativePath = folder + "/crosspoint-native.log";
        file = std::fopen (nativePath.toRawUTF8(), "a");

        // The crash path builds its file name without touching the heap, so the prefix
        // is prepared here, once, as plain bytes.
        std::snprintf (crashPrefix, sizeof (crashPrefix), "%s/crash-native-", folder.toRawUTF8());

        ringIndex = 0;
        ringUsed = 0;
        ringWrapped = false;
        std::memset (ring, 0, sizeof (ring));

        if (file != nullptr)
        {
            char when[64] = { 0 };
            time_t now = time (nullptr);
            struct tm tmv;
            localtime_r (&now, &tmv);
            std::strftime (when, sizeof (when), "%Y-%m-%d %H:%M:%S", &tmv);

            append ("=== session start (engine) " + juce::String (when)
                    + " | " + juce::SystemStats::getOperatingSystemName()
                    + " | JUCE " + juce::SystemStats::getJUCEVersion() + " ===");
            append ("log file: " + nativePath);
        }

        Logger::setCurrentLogger (this);      // from here on, everything goes to both
        installSignalHandlers();
    }

    /** logMessage is called on whatever thread logged; the critical section is cheap. */
    void logMessage (const juce::String& message) override
    {
        Logger::outputDebugString (message);   // logcat, as before
        append (message);
    }

    /** One line into the ring and the file. Safe to call before initialise(). */
    void append (const juce::String& line)
    {
        const juce::ScopedLock sl (lock);

        const char* raw = line.toRawUTF8();
        const size_t len = (size_t) std::strlen (raw);

        // The ring keeps the tail for a crash dump: 16 KB of recent output, written
        // linearly and wrapping when it fills. ringUsed says how much of it was ever
        // written, so a dump does not carry a screen of NULs with it.
        if (len + 1 >= sizeof (ring))
        {
            std::memcpy (ring, raw + (len + 1 - sizeof (ring)), sizeof (ring));
            ringIndex = 0;
            ringUsed = sizeof (ring);
            ringWrapped = true;
        }
        else
        {
            if (ringIndex + len + 1 > sizeof (ring))
            {
                ringIndex = 0;
                ringUsed = sizeof (ring);
                ringWrapped = true;
            }

            std::memcpy (ring + ringIndex, raw, len);
            ring[ringIndex + len] = '\n';
            ringIndex += len + 1;

            if (ringIndex > ringUsed)
                ringUsed = ringIndex;
        }

        if (file != nullptr)
        {
            std::fputs (raw, file);
            std::fputc ('\n', file);
            std::fflush (file);          // a crash must not lose the lines above it
        }
    }

private:
    EngineLog() = default;

    void installSignalHandlers()
    {
       #if JUCE_ANDROID
        struct sigaction sa;
        std::memset (&sa, 0, sizeof (sa));
        sa.sa_handler = &EngineLog::onFatalSignal;
        sigemptyset (&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;      // one shot; the handler puts the old one back

        for (int i = 0; i < numFatalSignals; ++i)
        {
            std::memset (&oldActions[i], 0, sizeof (oldActions[i]));
            sigaction (fatalSignals[i], &sa, &oldActions[i]);    // keep what was there
        }
       #endif
    }

    /** Only what is safe in a signal handler: snprintf, open, write, close, sigaction,
        raise. No allocation, no locks, no juce types. */
    static void onFatalSignal (int signum)
    {
       #if JUCE_ANDROID
        auto& self = EngineLog::instance();

        if (self.crashPrefix[0] != '\0')
        {
            char when[32] = { 0 };
            time_t now = time (nullptr);
            struct tm tmv;
            localtime_r (&now, &tmv);
            std::strftime (when, sizeof (when), "%Y%m%d-%H%M%S", &tmv);

            char path[600];
            std::snprintf (path, sizeof (path), "%s%s.txt", self.crashPrefix, when);

            const int fd = ::open (path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

            if (fd >= 0)
            {
                char head[320];
                const int headLen = std::snprintf (head, sizeof (head),
                    "Crosspoint native crash\nsignal  : %d\nwhen    : %s\n"
                    "log     : crosspoint-native.log holds the full engine log\n"
                    "note    : the previous signal handler is put back before the "
                    "re-raise, so the tombstone in `adb logcat -b crash` still happens\n\n"
                    "---- last engine output ----\n",
                    signum, when);
                if (headLen > 0)
                    (void) ::write (fd, head, (size_t) headLen);

                // The engine's own file, already flushed, has the full record; this is
                // the newest ring content only, never the untouched tail of the buffer.
                if (self.ringWrapped)
                {
                    (void) ::write (fd, self.ring + self.ringIndex,
                                    sizeof (self.ring) - (size_t) self.ringIndex);
                    (void) ::write (fd, self.ring, (size_t) self.ringIndex);
                }
                else
                {
                    (void) ::write (fd, self.ring, (size_t) self.ringUsed);
                }
                (void) ::close (fd);
            }
        }

        // Hand the signal back to whoever had it -- bionic's handler is what feeds the
        // tombstone. SIG_DFL here would kill the process without one.
        for (int i = 0; i < numFatalSignals; ++i)
        {
            if (fatalSignals[i] == signum)
            {
                (void) ::sigaction (signum, &self.oldActions[i], nullptr);
                ::raise (signum);
                return;
            }
        }

        ::signal (signum, SIG_DFL);
        ::raise (signum);
       #endif
    }

    static const int numFatalSignals = 6;
    static const int fatalSignals[numFatalSignals];

    juce::CriticalSection lock;
    juce::String dir, nativePath;
    FILE* file = nullptr;
    char crashPrefix[512] = { 0 };
    char ring[16 * 1024] = { 0 };
    size_t ringIndex = 0;
    size_t ringUsed = 0;
    bool ringWrapped = false;
    struct sigaction oldActions[numFatalSignals];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineLog)
};

/*  Out-of-line so the class stays complete for the handler that reads it.  */
inline const int EngineLog::fatalSignals[numFatalSignals] =
    { SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGSYS };

} // namespace crosspoint
