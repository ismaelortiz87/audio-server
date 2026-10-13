// A10 (docs/design/android.md section 4): the copy of the Connection
// notification, in one place. Pure C++ (no JUCE, no JNI) so the host test
// tests/android/a10-notification.cpp can check every row of the tables.
//
// The input is the same data the Console state carries for the live band
// (stations' hearsYou, mic mode/on/transmitting, connection), so the title
// answers the same question as the band: who hears you.

#pragma once

#include <string>
#include <vector>

namespace crosspoint::notification {

struct Input
{
    // connection (state.connection)
    std::string connState;              // connecting | connected | reconnecting | failed
    std::string group, reason;
    int stationsOnline = 0, stationsKnown = 0;

    // who hears you (state.stations[].hearsYou, short names, in display order)
    std::vector<std::string> hearing;
    bool anySolo = false;

    // mic (state.mic) and platform (state.platform)
    bool micOpenMode = true;            // mic.mode == "open"
    bool micOn = true;
    bool transmitting = false;
    bool micGranted = true;             // platform.micPermission == granted (A9; true until it reports)
    bool phoneCall = false;             // platform.phoneCall
};

enum class MicAction { None, TurnOff, TurnOn };

struct Content
{
    std::string title, text;
    bool live = false;                  // voice reaches at least one station: tint #ff4f7b instead of accent
    MicAction micAction = MicAction::None;

    bool operator== (const Content& o) const
    {
        return title == o.title && text == o.text && live == o.live && micAction == o.micAction;
    }
    bool operator!= (const Content& o) const { return ! (*this == o); }
};

/** "A" / "A and B" / "A, B and C" (same as console-ui listNames). */
inline std::string listNames (const std::vector<std::string>& names)
{
    std::string out;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0) out += (i + 1 == names.size()) ? " and " : ", ";
        out += names[i];
    }
    return out;
}

inline Content build (const Input& in)
{
    Content c;
    const size_t n = in.hearing.size();
    const bool all = n > 0 && (int) n == in.stationsKnown;

    // ---- title = who hears you (android.md section 4, first table) ----
    if (in.phoneCall)                                   c.title = "Mic off during your phone call";
    else if (! in.micGranted)                           c.title = "Microphone blocked \xC2\xB7 nobody hears you";
    else if (in.transmitting)
    {
        if (n == 0)                                     c.title = "Mic open, but no station is set to hear you";
        else if (all && n > 1)                          c.title = "Live: all " + std::to_string (n) + " stations hear you";
        else if (in.anySolo)                            c.title = "Live: solo, only " + listNames (in.hearing) + (n == 1 ? " hears you" : " hear you");
        else                                            c.title = "Live: " + listNames (in.hearing) + (n == 1 ? " hears you" : " hear you");
    }
    else if (! in.micOpenMode)                          c.title = "Push to talk \xC2\xB7 nobody hears you";
    else                                                c.title = "Mic off \xC2\xB7 nobody hears you";

    c.live = ! in.phoneCall && in.micGranted && in.transmitting && n > 0;

    // ---- text = connection (second table) ----
    if (in.connState == "connected")
        c.text = "Listening to " + std::to_string (in.stationsOnline) + " of " + std::to_string (in.stationsKnown)
                 + " stations \xC2\xB7 " + in.group;
    else if (in.connState == "reconnecting")           c.text = "Reconnecting" + (in.group.empty() ? std::string() : " to " + in.group) + "\xE2\x80\xA6";
    else if (in.connState == "failed")                 c.text = "Not connected: " + in.reason;
    else                                               c.text = "Connecting" + (in.group.empty() ? std::string() : " to " + in.group) + "\xE2\x80\xA6";

    // ---- actions: Mic off / Mic on only in open mode, with the mic granted, outside a call ----
    if (in.micOpenMode && in.micGranted && ! in.phoneCall)
        c.micAction = in.micOn ? MicAction::TurnOff : MicAction::TurnOn;

    return c;
}

} // namespace crosspoint::notification
