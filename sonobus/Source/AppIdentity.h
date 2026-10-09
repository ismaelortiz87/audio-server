// App identity (P3.9). This fork runs side by side with stock SonoBus, so it
// keeps its settings, links and updates separate. The values come from the
// APP_ID_* compile definitions set in CMakeLists.txt; the fallbacks below only
// apply to builds that don't go through CMake (the deferred mobile .jucer).

#pragma once

#ifndef APP_ID_SETTINGS_DIR
 #define APP_ID_SETTINGS_DIR "Crosspoint"   // macOS "Application Support/<this>", Windows %APPDATA%\<this>
#endif

#ifndef APP_ID_LINUX_DIR
 #define APP_ID_LINUX_DIR "crosspoint"      // Linux ~/.config/<this>
#endif

#ifndef APP_ID_URL_SCHEME
 #define APP_ID_URL_SCHEME "crosspoint"     // <scheme>://host:port/?g=group&p=password
#endif

// Upstream's updater downloads stock SonoBus from sonobus.net, which would
// replace this fork. Keep it off unless this fork gets its own release feed.
#ifndef APP_ID_ENABLE_UPDATE_CHECK
 #define APP_ID_ENABLE_UPDATE_CHECK 0
#endif
