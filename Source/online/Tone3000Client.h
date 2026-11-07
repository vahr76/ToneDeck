#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

// TONE3000 integration (https://www.tone3000.com/api)
//  - OAuth 2 + PKCE through the user's browser, with a loopback redirect
//    (http://127.0.0.1:<port>/callback) answered by a tiny local HTTP listener.
//  - "select_tone" flow: the user picks a tone on tone3000.com, we download its
//    model file(s) into the library folder and hand them to the processor.
//  - Lists available on the free tier: favorited / downloaded / created / trending / latest.
// All network work runs on a background thread; callbacks arrive on the message thread.
class Tone3000Client
{
public:
    struct ToneItem
    {
        juce::String id, title, gear, format, author;
    };

    struct DownloadedModel
    {
        juce::File file;
        juce::String toneTitle, modelName, gear;   // gear: amp, amp-cab, pedal, outboard...
    };

    using ListCallback     = std::function<void (juce::Array<ToneItem>, juce::String error)>;
    using ModelsCallback   = std::function<void (juce::Array<DownloadedModel>, juce::String error)>;
    using StatusCallback   = std::function<void (const juce::String&)>;

    Tone3000Client (juce::PropertiesFile& settings, std::function<juce::File()> libraryFolder);
    ~Tone3000Client();

    // Publishable key (t3k_pub_...). Stored in the user's settings file.
    juce::String getClientId() const;
    void setClientId (const juce::String&);
    bool isSignedIn() const;
    juce::String getUsername() const;
    void signOut();

    // Opens the browser on tone3000.com so the user picks a tone; downloads its models.
    void selectTone (ModelsCallback done);
    // Re-downloads (or reuses) the models of a known tone id.
    void fetchTone (const juce::String& toneId, ModelsCallback done);
    // listName: "favorited", "downloaded", "created", "trending", "latest"
    void fetchList (const juce::String& listName, ListCallback done);

    StatusCallback onStatus;   // progress messages for the UI ("Waiting for browser..." etc.)

    // Exposed for tests
    static juce::String makeCodeChallenge (const juce::String& verifier);
    static juce::StringPairArray parseQuery (const juce::String& query);

private:
    struct Tokens { juce::String access, refresh; juce::int64 expiresAtMs = 0; };

    // all of these run on the worker thread
    bool runBrowserAuth (const juce::String& prompt, juce::String& toneIdOut, juce::String& error);
    bool ensureToken (juce::String& error);
    bool exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirect, juce::String& error);
    bool refreshToken (juce::String& error);
    juce::var apiGet (const juce::String& pathAndQuery, juce::String& error);
    juce::Array<DownloadedModel> downloadModels (const juce::String& toneId, juce::String& error);
    void storeTokens (const juce::var& json);
    void fetchUsername();
    void status (const juce::String& msg);

    template <typename Fn> void runAsync (Fn&& fn);

    juce::PropertiesFile& props;
    std::function<juce::File()> getLibrary;
    juce::ThreadPool pool { 1 };
    juce::CriticalSection tokenLock;
    Tokens tokens;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<bool> busy { false };
};
