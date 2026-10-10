// P6.1: the Mac window content. A WKWebView (JUCE WebBrowserComponent) that loads
// the Console UI from the engine's own control API server (http://127.0.0.1:<port>/),
// or, when the API could not start, a native error page explaining why.
//
// Audio stays native: the page only talks to the local engine over its API.

#pragma once

#include "JuceHeader.h"

#if JUCE_MAC && JUCE_WEB_BROWSER
 #define CROSSPOINT_HAS_WEB_SHELL 1

class ConsoleShellView : public Component
{
public:
    /** `url` = the engine's UI root; `fatalError` non-empty = show it instead of a page. */
    ConsoleShellView (const String& url, const String& fatalError)
        : baseUrl (url), retryButton ("Retry")
    {
        setOpaque (true);
        setWantsKeyboardFocus (false);

        title.setFont (Font (20.0f, Font::bold));
        title.setColour (Label::textColourId, Colour (0xfff2f4f6));
        title.setJustificationType (Justification::centredLeft);
        message.setFont (Font (14.0f));
        message.setColour (Label::textColourId, Colour (0xffa8b0b8));
        message.setJustificationType (Justification::topLeft);
        message.setMinimumHorizontalScale (1.0f);
        addChildComponent (title);
        addChildComponent (message);
        addChildComponent (retryButton);
        retryButton.onClick = [this] { loadPage(); };

        if (fatalError.isNotEmpty())
            showError ("Crosspoint can't start its Console", fatalError, false);
        else
            loadPage();
    }

    void paint (Graphics& g) override { g.fillAll (Colour (0xff0b0d0f)); }   // --bg of the console-ui tokens

    void resized() override
    {
        auto r = getLocalBounds();
        if (page != nullptr) page->setBounds (r);
        auto box = r.reduced (24).withTrimmedTop (r.getHeight() / 5);
        title.setBounds (box.removeFromTop (32));
        box.removeFromTop (8);
        message.setBounds (box.removeFromTop (juce::jmin (box.getHeight(), 160)));
        box.removeFromTop (12);
        retryButton.setBounds (box.removeFromTop (30).removeFromLeft (100));
    }

    bool isShowingError() const { return page == nullptr; }

private:
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
            if (newURL.startsWith (owner.baseUrl) || newURL.startsWith ("about:") || newURL.startsWith ("blob:") || newURL.startsWith ("data:"))
                return true;
            URL (newURL).launchInDefaultBrowser();
            return false;
        }

        void newWindowAttemptingToLoad (const String& newURL) override { URL (newURL).launchInDefaultBrowser(); }

        bool pageLoadHadNetworkError (const String& errorInfo) override
        {
            // Posted so we never delete the web view from inside its own callback.
            Component::SafePointer<ConsoleShellView> sp (&owner);
            MessageManager::callAsync ([sp, errorInfo]
            {
                if (sp != nullptr)
                    sp->showError ("The Console page could not be loaded",
                                   "Crosspoint's local engine did not answer at " + sp->baseUrl + "\n\n" + errorInfo, true);
            });
            return false;
        }

        void pageFinishedLoading (const String&) override { grabKeyboardFocus(); }

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
        }
        page->goToURL (baseUrl);
    }

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsoleShellView)
};

#else
 #define CROSSPOINT_HAS_WEB_SHELL 0
#endif
