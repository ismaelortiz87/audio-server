// A10: every row of android.md section 4's title / text / action tables.
//   clang++ -std=c++17 -I sonobus/Source tests/android/a10-notification.cpp -o a10 && ./a10
#include "ConnectionNotification.h"
#include <cstdio>

using namespace crosspoint::notification;
static int failures = 0;
#define EXPECT_EQ(a, b) do { const std::string x_ = (a), y_ = (b); if (x_ != y_) { ++failures; std::printf ("FAIL line %d\n  got:  %s\n  want: %s\n", __LINE__, x_.c_str(), y_.c_str()); } } while (0)
#define EXPECT(c) do { if (!(c)) { ++failures; std::printf ("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Input base()
{
    Input in;
    in.connState = "connected"; in.group = "ops"; in.stationsOnline = 3; in.stationsKnown = 3;
    in.hearing = { "ACCT-07", "DEV-02", "QA-11" };
    in.transmitting = true;
    return in;
}

int main()
{
    // titles
    { auto c = build (base()); EXPECT_EQ (c.title, "Live: all 3 stations hear you"); EXPECT (c.live); EXPECT (c.micAction == MicAction::TurnOff); }
    { auto in = base(); in.hearing = { "ACCT-07", "DEV-02" }; EXPECT_EQ (build (in).title, "Live: ACCT-07 and DEV-02 hear you"); }
    { auto in = base(); in.hearing = { "ACCT-07" }; EXPECT_EQ (build (in).title, "Live: ACCT-07 hears you"); }
    { auto in = base(); in.hearing = { "ACCT-07", "DEV-02", "QA-11" }; in.stationsKnown = 4; EXPECT_EQ (build (in).title, "Live: ACCT-07, DEV-02 and QA-11 hear you"); }
    // review 2: one known station is named, never "all 1 stations" (spec 3.2), soloed or not
    { auto in = base(); in.hearing = { "ACCT-07" }; in.stationsKnown = 1; in.stationsOnline = 1; EXPECT_EQ (build (in).title, "Live: ACCT-07 hears you"); }
    { auto in = base(); in.hearing = { "ACCT-07" }; in.stationsKnown = 1; in.stationsOnline = 1; in.anySolo = true; EXPECT_EQ (build (in).title, "Live: ACCT-07 hears you"); }
    { auto in = base(); in.hearing = { "ACCT-07" }; in.anySolo = true; EXPECT_EQ (build (in).title, "Live: solo, only ACCT-07 hears you"); }
    { auto in = base(); in.hearing = { "ACCT-07", "DEV-02" }; in.anySolo = true; EXPECT_EQ (build (in).title, "Live: solo, only ACCT-07 and DEV-02 hear you"); }
    { auto in = base(); in.hearing = {}; auto c = build (in); EXPECT_EQ (c.title, "Mic open, but no station is set to hear you"); EXPECT (! c.live); }
    { auto in = base(); in.transmitting = false; in.micOn = false; auto c = build (in); EXPECT_EQ (c.title, "Mic off \xC2\xB7 nobody hears you"); EXPECT (! c.live); EXPECT (c.micAction == MicAction::TurnOn); }
    { auto in = base(); in.micOpenMode = false; in.transmitting = false; auto c = build (in); EXPECT_EQ (c.title, "Push to talk \xC2\xB7 nobody hears you"); EXPECT (c.micAction == MicAction::None); }
    { auto in = base(); in.micOpenMode = false; in.transmitting = true; in.hearing = { "ACCT-07" }; EXPECT_EQ (build (in).title, "Live: ACCT-07 hears you"); }
    { auto in = base(); in.micGranted = false; in.transmitting = false; auto c = build (in); EXPECT_EQ (c.title, "Microphone blocked \xC2\xB7 nobody hears you"); EXPECT (c.micAction == MicAction::None); EXPECT (! c.live); }
    { auto in = base(); in.phoneCall = true; in.transmitting = false; in.micOn = false; auto c = build (in); EXPECT_EQ (c.title, "Mic off during your phone call"); EXPECT (c.micAction == MicAction::None); EXPECT (! c.live); }

    // text
    EXPECT_EQ (build (base()).text, "Listening to 3 of 3 stations \xC2\xB7 ops");
    { auto in = base(); in.stationsOnline = 2; in.stationsKnown = 4; EXPECT_EQ (build (in).text, "Listening to 2 of 4 stations \xC2\xB7 ops"); }
    { auto in = base(); in.connState = "connecting"; EXPECT_EQ (build (in).text, "Connecting to ops\xE2\x80\xA6"); }
    { auto in = base(); in.connState = "reconnecting"; EXPECT_EQ (build (in).text, "Reconnecting to ops\xE2\x80\xA6"); }
    { auto in = base(); in.connState = "failed"; in.reason = "Connection to the server was lost"; EXPECT_EQ (build (in).text, "Not connected: Connection to the server was lost"); }

    // equality drives "update only when something changed"
    EXPECT (build (base()) == build (base()));
    { auto in = base(); in.stationsOnline = 2; EXPECT (build (in) != build (base())); }

    std::printf (failures ? "%d FAILED\n" : "notification copy: all checks passed\n", failures);
    return failures != 0;
}
