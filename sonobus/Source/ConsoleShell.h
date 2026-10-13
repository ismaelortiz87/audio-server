// P6.1 / A7: the window content on the Mac and on Android. A WKWebView (Mac) or
// android.webkit.WebView (Android), wrapped by JUCE's WebBrowserComponent, that
// loads the Console UI from the engine's own control API server
// (http://127.0.0.1:<port>/), or, when the API could not start, a native error
// page explaining why.
//
// Audio stays native: the page only talks to the local engine over its API.
//
// The phone layout, the system bars and the error-page copy are specified in
// docs/design/android.md (sections 2 and 8).

#pragma once

#include "JuceHeader.h"

#if (JUCE_MAC || JUCE_ANDROID) && JUCE_WEB_BROWSER
 #define CROSSPOINT_HAS_WEB_SHELL 1

#if JUCE_ANDROID
// Native helpers for the Android shell (android.md section 2); defined in ConsoleShellAndroid.h,
// which only the app's one translation unit includes (JUCE's JNI helpers have no include guard).
namespace crosspoint_android
{
    bool paintWebViewBackground (void* webView, uint32 argb);   // the web view's background, before any page paints
    void haptic (void* webView, bool starting);                  // performHapticFeedback (honours the system setting)
    void moveTaskToBack();                        // same as Home; never disconnects
}
#endif


/** How the "can't show the Console at all" page looks and behaves. All fields are
    optional: the defaults are the Mac's reviewed P6.1 page (default title, no button). */
struct ConsoleShellFatal
{
    String title;                      // empty = the platform's default heading
    String buttonLabel;                // empty = the platform's default label
    std::function<bool()> onRetry;     // set = the page has a button; it calls this, and a
                                       // true result loads the Console page (false: stay)
};

class ConsoleShellView : public Component
{
public:
    /** `url` = the engine's UI root; `fatalError` non-empty = show it instead of a page. */
    ConsoleShellView (const String& url, const String& fatalError, ConsoleShellFatal fatalOptions = {})
        : baseUrl (url), retryButton (fatalOptions.buttonLabel.isNotEmpty() ? fatalOptions.buttonLabel : defaultButtonLabel()),
          fatalRetry (std::move (fatalOptions.onRetry))
    {
        setOpaque (true);
        setWantsKeyboardFocus (false);

       #if JUCE_ANDROID
        // android.md section 8: title 20/700 ink, body 15/1.45 ink2, one 48 px
        // full-width accent button. The Mac keeps its reviewed P6.1 look.
        title.setFont (Font (20.0f, Font::bold));
        message.setFont (Font (15.0f));
        title.setColour (Label::textColourId, Colour (0xffe8e4da));                 // --ink
        message.setColour (Label::textColourId, Colour (0xffa8a69d));               // --ink2
        retryButton.setColour (TextButton::buttonColourId, Colour (0xff2e6b66));    // --accent
        retryButton.setColour (TextButton::textColourOffId, Colour (0xffe8e4da));   // --ink
       #else
        title.setFont (Font (20.0f, Font::bold));
        title.setColour (Label::textColourId, Colour (0xfff2f4f6));
        message.setFont (Font (14.0f));
        message.setColour (Label::textColourId, Colour (0xffa8b0b8));
       #endif
        title.setJustificationType (Justification::centredLeft);
        message.setJustificationType (Justification::topLeft);
        message.setMinimumHorizontalScale (1.0f);
        addChildComponent (title);
        addChildComponent (message);
        addChildComponent (retryButton);
        retryButton.onClick = [this]
        {
            if (fatalRetry != nullptr)
            {
                // Asked to start the engine's API again: only load the page if it did.
                if (! fatalRetry())
                    return;
                fatalRetry = nullptr;   // from here on the button is the plain page reload
            }
            loadPage();
        };

        if (fatalError.isNotEmpty())
            showError (fatalOptions.title.isNotEmpty() ? fatalOptions.title : errorTitle(), fatalError, fatalRetry != nullptr);
        else
            loadPage();
    }

    void paint (Graphics& g) override { g.fillAll (Colour (0xff0b0d0f)); }   // --bg of the console-ui tokens

    void resized() override
    {
        // The web view fills the whole shell. On Android the window itself already sits
        // between the system bars (they are opaque, `rail`), so subtracting the safe-area
        // insets here as well would leave an empty band (A7).
        auto r = getLocalBounds();
        if (page != nullptr) page->setBounds (r);

        auto box = r.reduced (24).withTrimmedTop (r.getHeight() / 4);
       #if JUCE_ANDROID
        title.setBounds (box.removeFromTop (28));
        box.removeFromTop (10);
        message.setBounds (box.removeFromTop (juce::jmin (box.getHeight(), 220)));
        box.removeFromTop (18);
        retryButton.setBounds (box.removeFromTop (48));
       #else
        title.setBounds (box.removeFromTop (32));
        box.removeFromTop (8);
        message.setBounds (box.removeFromTop (juce::jmin (box.getHeight(), 160)));
        box.removeFromTop (12);
        retryButton.setBounds (box.removeFromTop (30).removeFromLeft (100));
       #endif
    }

    bool isShowingError() const { return page == nullptr; }

   #if JUCE_ANDROID
    /** Back button (android.md section 2). The page closes its settings sheet if one is
        open and otherwise asks us (crosspoint-shell://background) to go to the background.
        With no page (the native error page) there is nothing to close. */
    void handleBack()
    {
        if (page == nullptr)
            crosspoint_android::moveTaskToBack();
        else
            page->goToURL ("javascript:void((window.crosspointBack||function(){location.href='crosspoint-shell://background'})())");
    }
   #endif

private:
    static String defaultButtonLabel()
    {
       #if JUCE_ANDROID
        return "Try again";
       #else
        return "Retry";
       #endif
    }

    // android.md section 8 gives the phone its own error copy; the Mac keeps its
    // original heading.
    static String errorTitle()
    {
       #if JUCE_ANDROID
        return "Crosspoint couldn't start";
       #else
        return "Crosspoint can't start its Console";
       #endif
    }

    static String pageLoadErrorTitle()
    {
       #if JUCE_ANDROID
        return "The Console didn't load";
       #else
        return "The Console page could not be loaded";
       #endif
    }

    static String pageLoadErrorBody (const String& baseUrl, const String& errorInfo)
    {
       #if JUCE_ANDROID
        return "Crosspoint's engine didn't answer. " + errorInfo;
       #else
        return "Crosspoint's local engine did not answer at " + baseUrl + "\n\n" + errorInfo;
       #endif
    }

    //==========================================================================
    struct Page : public WebBrowserComponent
    {
        Page (ConsoleShellView& o)
            : WebBrowserComponent (Options().withBackend (Options::Backend::defaultBackend)
                                            .withKeepPageLoadedWhenBrowserIsHidden()),
              owner (o) {}

        // Only the engine's own pages load in the window; any other link opens in the browser.
        bool pageAboutToLoad (const String& newURL) override
        {
           #if JUCE_ANDROID
            // The page's way to talk to the shell (console-ui app.js shellCall()): never loaded.
            if (newURL.startsWith ("crosspoint-shell://"))
            {
                auto what = newURL.fromFirstOccurrenceOf ("crosspoint-shell://", false, false).upToFirstOccurrenceOf ("?", false, false);
                if (what == "background")        crosspoint_android::moveTaskToBack();
                else if (what == "haptic/start") crosspoint_android::haptic (nativeView(), true);
                else if (what == "haptic/stop")  crosspoint_android::haptic (nativeView(), false);
                return false;
            }
           #endif
            if (newURL.startsWith (owner.baseUrl) || newURL.startsWith ("about:") || newURL.startsWith ("blob:") || newURL.startsWith ("data:"))
                return true;
            URL (newURL).launchInDefaultBrowser();
            return false;
        }

        void newWindowAttemptingToLoad (const String& newURL) override { URL (newURL).launchInDefaultBrowser(); }

        bool pageLoadHadNetworkError (const String& errorInfo) override
        {
           #if JUCE_ANDROID
            // JUCE's Android WebView reports *every* subresource HTTP error here, not just
            // the page. The Console probes rtc/config, which is a 404 on a phone (no WebRTC
            // gateway), and that must not replace the page with the error screen. Real
            // load failures arrive as "net::ERR_..." (or empty, for TLS errors) (A3).
            if (errorInfo.isNotEmpty() && ! errorInfo.contains ("net::"))
                return true;
           #endif
            // Posted so we never delete the web view from inside its own callback.
            Component::SafePointer<ConsoleShellView> sp (&owner);
            MessageManager::callAsync ([sp, errorInfo]
            {
                if (sp != nullptr)
                    sp->showError (pageLoadErrorTitle(), pageLoadErrorBody (sp->baseUrl, errorInfo), true);
            });
            return false;
        }

        void pageFinishedLoading (const String&) override { grabKeyboardFocus(); }

       #if JUCE_ANDROID
        /** The android.webkit.WebView behind this component (JUCE's private Pimpl is its first
            child, an AndroidViewComponent), or null. */
        void* nativeView()
        {
            if (auto* c = dynamic_cast<AndroidViewComponent*> (getChildComponent (0)))
                return c->getView();
            return nullptr;
        }
       #endif

        ConsoleShellView& owner;
    };

    void loadPage()
    {
        title.setVisible (false);
        message.setVisible (false);
        retryButton.setVisible (false);
        if (page == nullptr)
        {
            page = std::make_unique<Page> (*this);
            addAndMakeVisible (*page);
            resized();
           #if JUCE_ANDROID
            paintWebViewWhenAttached (0);
           #endif
        }
        page->goToURL (baseUrl);
    }

   #if JUCE_ANDROID
    // The web view joins the view tree when the peer attaches it; paint it `bg`
    // (--bg of the console-ui tokens) as soon as it is there, before a page can flash white.
    void paintWebViewWhenAttached (int attempt)
    {
        if (page == nullptr || attempt > 100)
            return;
        if (crosspoint_android::paintWebViewBackground (page->nativeView(), 0xff0b0d0f))
            return;
        Component::SafePointer<ConsoleShellView> sp (this);
        Timer::callAfterDelay (10, [sp, attempt] { if (sp != nullptr) sp->paintWebViewWhenAttached (attempt + 1); });
    }
   #endif

    void showError (const String& heading, const String& text, bool canRetry)
    {
        page.reset();   // never show a blank page
        title.setText (heading, dontSendNotification);
        message.setText (text, dontSendNotification);
        title.setVisible (true);
        message.setVisible (true);
        retryButton.setVisible (canRetry);
        resized();
        repaint();
    }

    String baseUrl;
    std::unique_ptr<Page> page;
    Label title, message;
    TextButton retryButton;
    std::function<bool()> fatalRetry;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsoleShellView)
};

#else
 #define CROSSPOINT_HAS_WEB_SHELL 0
#endif
