// See GlobalHotkey.h (P6.5).
#include "GlobalHotkey.h"
#include "GlobalHotkeyCarbon.h"

namespace crosspoint {

namespace {
    // macOS virtual key codes (HIToolbox Events.h, kVK_ANSI_*), US layout positions.
    struct KeyName { const char* name; unsigned code; };
    const KeyName kKeys[] = {
        { "Space", 0x31 },
        { "A", 0x00 }, { "B", 0x0B }, { "C", 0x08 }, { "D", 0x02 }, { "E", 0x0E }, { "F", 0x03 },
        { "G", 0x05 }, { "H", 0x04 }, { "I", 0x22 }, { "J", 0x26 }, { "K", 0x28 }, { "L", 0x25 },
        { "M", 0x2E }, { "N", 0x2D }, { "O", 0x1F }, { "P", 0x23 }, { "Q", 0x0C }, { "R", 0x0F },
        { "S", 0x01 }, { "T", 0x11 }, { "U", 0x20 }, { "V", 0x09 }, { "W", 0x0D }, { "X", 0x07 },
        { "Y", 0x10 }, { "Z", 0x06 },
        { "0", 0x1D }, { "1", 0x12 }, { "2", 0x13 }, { "3", 0x14 }, { "4", 0x15 }, { "5", 0x17 },
        { "6", 0x16 }, { "7", 0x1A }, { "8", 0x1C }, { "9", 0x19 },
        { "F1", 0x7A }, { "F2", 0x78 }, { "F3", 0x63 }, { "F4", 0x76 }, { "F5", 0x60 }, { "F6", 0x61 },
        { "F7", 0x62 }, { "F8", 0x64 }, { "F9", 0x65 }, { "F10", 0x6D }, { "F11", 0x67 }, { "F12", 0x6F },
    };

    struct Parsed { unsigned mods = 0; unsigned keyCode = 0; String canonical; };

    bool parseBinding (const String& specIn, Parsed& out, String& error)
    {
        const String spec = specIn.trim();
        if (spec.isEmpty()) { error = "empty hotkey"; return false; }

        // Modifier symbols and words (ctrl/alt/shift/cmd), separated by + - or space.
        unsigned mods = 0;
        String keyPart, word;
        bool ok = true;

        auto flush = [&]
        {
            if (word.isEmpty()) return;
            const String l = word.toLowerCase();
            if      (l == "ctrl" || l == "control")               mods |= carbon::modControl;
            else if (l == "alt" || l == "opt" || l == "option")   mods |= carbon::modOption;
            else if (l == "shift")                                mods |= carbon::modShift;
            else if (l == "cmd" || l == "command")                mods |= carbon::modCommand;
            else if (keyPart.isEmpty())                           keyPart = word;
            else { error = "more than one key in the hotkey"; ok = false; }
            word.clear();
        };

        for (auto it = spec.getCharPointer(); ! it.isEmpty() && ok; ++it)
        {
            const juce_wchar c = *it;
            if      (c == 0x2303) { flush(); mods |= carbon::modControl; }   // control
            else if (c == 0x2325) { flush(); mods |= carbon::modOption; }    // option
            else if (c == 0x21E7) { flush(); mods |= carbon::modShift; }     // shift
            else if (c == 0x2318) { flush(); mods |= carbon::modCommand; }   // command
            else if (c == '+' || c == ' ' || c == '-') flush();
            else word += String::charToString (c);
        }
        if (ok) flush();
        if (! ok) return false;

        if (keyPart.isEmpty()) { error = "the hotkey needs a key (Space, a letter, a digit or F1-F12)"; return false; }
        if (mods == 0)         { error = "the hotkey needs at least one modifier (ctrl, alt, shift or cmd)"; return false; }

        const KeyName* found = nullptr;
        for (auto& k : kKeys)
            if (keyPart.equalsIgnoreCase (k.name)) { found = &k; break; }
        if (found == nullptr) { error = "unknown key '" + keyPart + "' (use Space, A-Z, 0-9 or F1-F12)"; return false; }

        out.mods = mods;
        out.keyCode = found->code;
        String s;
        if (mods & carbon::modControl) s += String::fromUTF8 ("\xE2\x8C\x83");
        if (mods & carbon::modOption)  s += String::fromUTF8 ("\xE2\x8C\xA5");
        if (mods & carbon::modShift)   s += String::fromUTF8 ("\xE2\x87\xA7");
        if (mods & carbon::modCommand) s += String::fromUTF8 ("\xE2\x8C\x98");
        out.canonical = s + found->name;
        return true;
    }

    void carbonTrampoline (void* ctx, bool pressed) { static_cast<GlobalHotkey*> (ctx)->handleEvent (pressed); }
}

struct GlobalHotkey::Impl {};

GlobalHotkey::GlobalHotkey() {}
GlobalHotkey::~GlobalHotkey() { onChange = nullptr; carbon::unregisterHotkey(); }

bool GlobalHotkey::supported()
{
   #if JUCE_MAC
    return true;
   #else
    return false;
   #endif
}

String GlobalHotkey::canonicalise (const String& spec, String& error)
{
    Parsed p;
    return parseBinding (spec, p, error) ? p.canonical : String();
}

bool GlobalHotkey::setBinding (const String& spec, String& error)
{
    Parsed p;
    if (! parseBinding (spec, p, error)) return false;

    if (down) handleEvent (false);   // never leave a hold behind when the key changes
    std::string err;
    if (! carbon::registerHotkey (p.keyCode, p.mods, &carbonTrampoline, this, err))
    {
        error = String (err);
        binding.clear();
        return false;
    }
    binding = p.canonical;
    return true;
}

void GlobalHotkey::clear()
{
    carbon::unregisterHotkey();
    binding.clear();
    if (down) handleEvent (false);
}

void GlobalHotkey::handleEvent (bool pressed)
{
    if (pressed == down) return;     // key repeat or a duplicate edge
    down = pressed;
    if (onChange) onChange (pressed);
}

} // namespace crosspoint
