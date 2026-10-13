// A8: host check of the Android device naming (sonobus/Source/AndroidAudioRoute.h,
// the plain C++ half). Build and run:
//   c++ -std=c++17 -o /tmp/a8check tests/android/a8-device-names.cpp && /tmp/a8check
#include "../../sonobus/Source/AndroidAudioRoute.h"
#include <cstdio>

using namespace crosspoint::androidaudio;

static int failures = 0;
static void expectEq (int line, const std::string& got, const std::string& want)
{
    if (got != want)
    {
        ++failures;
        std::printf ("FAIL line %d\n  got:      %s\n  expected: %s\n", line, got.c_str(), want.c_str());
    }
}
#define EXPECT_EQ(a, b) expectEq (__LINE__, (a), (b))
static std::string yn (bool b) { return b ? "yes" : "no"; }

int main()
{
    // outputs, as JUCE names them: "<product> <type text>"
    EXPECT_EQ (labelForJuceName ("Pixel 8 built-in speaker", false), "Phone speaker");
    EXPECT_EQ (labelForJuceName ("Pixel 8 built-in speaker safe", false), "Phone speaker");
    EXPECT_EQ (labelForJuceName ("Pixel 8 built-in earphone speaker", false), "Earpiece");
    EXPECT_EQ (labelForJuceName ("Wired Headphones wired headphones", false), "Wired headphones");
    EXPECT_EQ (labelForJuceName ("Headset wired headset", false), "Wired headphones");
    EXPECT_EQ (labelForJuceName ("Pixel Buds Pro Bluetooth device supporting the A2DP profile", false),
               "Bluetooth \xC2\xB7 Pixel Buds Pro");
    EXPECT_EQ (labelForJuceName ("Pixel Buds Pro Bluetooth device typically used for telephony", false),
               "Bluetooth \xC2\xB7 Pixel Buds Pro");
    EXPECT_EQ (labelForJuceName ("Scarlett 2i2 USB device", false), "USB \xC2\xB7 Scarlett 2i2");
    EXPECT_EQ (labelForJuceName ("Dongle USB headset", false), "USB \xC2\xB7 Dongle");
    // inputs
    EXPECT_EQ (labelForJuceName ("Pixel 8 built-in microphone", true), "Phone mic");
    EXPECT_EQ (labelForJuceName ("Headset wired headset", true), "Headset mic");
    EXPECT_EQ (labelForJuceName ("Pixel Buds Pro Bluetooth device typically used for telephony", true),
               "Bluetooth \xC2\xB7 Pixel Buds Pro");
    EXPECT_EQ (labelForJuceName ("Scarlett 2i2 USB device", true), "USB \xC2\xB7 Scarlett 2i2");
    // "System Default" takes the label of the device Android routes to
    EXPECT_EQ (labelForJuceName ("System Default (Output)", false, 0), "Phone speaker");
    EXPECT_EQ (labelForJuceName ("System Default (Output)", false, kWiredHeadphones), "Wired headphones");
    EXPECT_EQ (labelForJuceName ("System Default (Input)", true, 0), "Phone mic");
    EXPECT_EQ (labelForJuceName ("System Default (Input)", true, kWiredHeadset), "Headset mic");
    // a product name that is empty
    EXPECT_EQ (labelFor (kBluetoothA2dp, "", false), "Bluetooth");

    // type parsing: the longest suffix wins
    std::string product;
    EXPECT_EQ (std::to_string (typeFromJuceName ("X built-in speaker safe", &product)), "24");
    EXPECT_EQ (product, "X");
    EXPECT_EQ (std::to_string (typeFromJuceName ("X HDMI audio return channel")), "10");
    EXPECT_EQ (std::to_string (typeFromJuceName ("System Default (Output)")), "0");

    // routes (control-api 5.5)
    EXPECT_EQ (routeOf (kSpeaker), "speaker");
    EXPECT_EQ (routeOf (kEarpiece), "earpiece");
    EXPECT_EQ (routeOf (kWiredHeadphones), "wired");
    EXPECT_EQ (routeOf (kBluetoothA2dp), "bluetooth");
    EXPECT_EQ (routeOf (kUsbHeadset), "usb");
    EXPECT_EQ (routeOf (0), "speaker");

    // pickers hide what nobody can plug in
    EXPECT_EQ (yn (isHidden (kRemoteSubmix, false)), "yes");
    EXPECT_EQ (yn (isHidden (kTelephony, true)), "yes");
    EXPECT_EQ (yn (isHidden (kHdmi, false)), "yes");
    EXPECT_EQ (yn (isHidden (kBuiltinMic, true)), "no");
    EXPECT_EQ (yn (isHidden (kBluetoothA2dp, true)), "yes");   // A2DP has no mic
    EXPECT_EQ (yn (isHidden (kBluetoothA2dp, false)), "no");

    // echoRisk: speaker, no working AEC, mic transmitting
    EXPECT_EQ (yn (echoRisk ("speaker", false, true, true)), "yes");
    EXPECT_EQ (yn (echoRisk ("speaker", true, true, true)), "no");
    EXPECT_EQ (yn (echoRisk ("speaker", false, false, true)), "no");
    EXPECT_EQ (yn (echoRisk ("wired", false, true, true)), "no");
    EXPECT_EQ (yn (echoRisk ("earpiece", false, true, true)), "no");
    EXPECT_EQ (yn (echoRisk ("speaker", false, true, false)), "no");   // mic open but no station hears you

    std::printf (failures ? "%d FAILED\n" : "a8 device names: all ok\n", failures);
    return failures ? 1 : 0;
}
