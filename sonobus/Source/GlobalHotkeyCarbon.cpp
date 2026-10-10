// Carbon RegisterEventHotKey wrapper (P6.5). No Accessibility permission is
// needed: the system calls us for the registered key only. Both the press and
// the release event are delivered (kEventHotKeyPressed / kEventHotKeyReleased).
#include "GlobalHotkeyCarbon.h"

#if defined(__APPLE__)
#include <Carbon/Carbon.h>

namespace crosspoint { namespace carbon {

namespace {
    EventHotKeyRef   hotKeyRef  = nullptr;
    EventHandlerRef  handlerRef = nullptr;
    HotkeyCallback   callback   = nullptr;
    void*            callbackCtx = nullptr;

    OSStatus onHotKeyEvent (EventHandlerCallRef, EventRef event, void*)
    {
        EventHotKeyID id {};
        if (GetEventParameter (event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof (id), nullptr, &id) != noErr
            || id.signature != 'Xpnt')
            return eventNotHandledErr;
        if (callback != nullptr)
            callback (callbackCtx, GetEventKind (event) == kEventHotKeyPressed);
        return noErr;
    }
}

bool registerHotkey (unsigned keyCode, unsigned modifiers, HotkeyCallback cb, void* context, std::string& error)
{
    unregisterHotkey();

    if (handlerRef == nullptr)
    {
        const EventTypeSpec types[2] = { { kEventClassKeyboard, kEventHotKeyPressed },
                                         { kEventClassKeyboard, kEventHotKeyReleased } };
        if (InstallApplicationEventHandler (NewEventHandlerUPP (onHotKeyEvent), 2, types, nullptr, &handlerRef) != noErr)
        {
            error = "could not install the hotkey event handler";
            return false;
        }
    }

    callback = cb;
    callbackCtx = context;
    const EventHotKeyID id { 'Xpnt', 1 };
    const OSStatus s = RegisterEventHotKey (keyCode, modifiers, id, GetApplicationEventTarget(), 0, &hotKeyRef);
    if (s != noErr)
    {
        hotKeyRef = nullptr;
        callback = nullptr;
        error = s == eventHotKeyExistsErr ? "that key combination is already taken by another app"
                                          : "RegisterEventHotKey failed (" + std::to_string ((int) s) + ")";
        return false;
    }
    return true;
}

void unregisterHotkey()
{
    if (hotKeyRef != nullptr) { UnregisterEventHotKey (hotKeyRef); hotKeyRef = nullptr; }
    callback = nullptr;
    callbackCtx = nullptr;
}

}} // namespace

#else

namespace crosspoint { namespace carbon {
bool registerHotkey (unsigned, unsigned, HotkeyCallback, void*, std::string& error) { error = "no global hotkey on this platform"; return false; }
void unregisterHotkey() {}
}}

#endif
