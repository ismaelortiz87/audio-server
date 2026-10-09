// Crosspoint control API server (P4.1): HTTP/1.1 + WebSocket (RFC 6455) inside
// the engine, as specified in docs/control-api.md.
//
// Threading model
//   * one accept thread (polls the listener, so stop() never blocks on accept)
//   * one thread per connection (HTTP request, or the whole WebSocket session)
//   * nothing here runs on the audio thread or blocks the JUCE message thread
//
// Plug-in points for the next tasks (all thread-safe):
//   P4.2  setStateProvider() + publishPatch()      full state / JSON-pointer patches
//   P4.3  broadcastTopic("meters", json)           honours sub/unsub and visibility
//   P4.4  setCommandHandler()                      runs on the JUCE message thread
//         setSessionClosedHandler()                message thread; release PTT (api 2.4)

#pragma once

#include "JuceHeader.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace crosspoint {

struct ApiServerImpl;
class ApiConnection;

struct ApiConfig
{
    int port = 0;                           // 0 = server disabled
    String bindAddress = "127.0.0.1";
    String token;                           // required unless the bind is loopback
    File uiDir;                             // static files root; invalid = no UI served
    StringArray allowedOrigins;             // extra WS origins, e.g. https://crosspoint.app.lagreca.io
    String role = "console";                // "console" or "vdi"
    String appName = "Crosspoint";
    String version;
    String selfName;                        // placeholder state: self.name
    StringArray features;                   // hello.features (P4.3/P4.4 add "meters", "ptt", ...)

    static bool isLoopbackAddress (const String& addr);
    bool isLoopbackBind() const             { return isLoopbackAddress (bindAddress); }
};

//==============================================================================
/** One connected WebSocket client. Created and owned by the server; handlers
    receive it as a shared_ptr and may keep it (send() on a closed session is a
    harmless no-op). */
class ApiSession
{
public:
    ~ApiSession() = default;

    int64 getId() const                     { return sessionId; }
    String getRemoteAddress() const         { return remoteAddress; }
    bool isAuthenticated() const            { return authenticated; }
    bool isClosed() const                   { return closed; }
    bool isSubscribed (const String& topic) const;
    bool isHidden() const                   { return hidden; }

    /** Queue one JSON text frame. Thread-safe, never blocks on the network. */
    void send (const String& json);

    /** Queue an `ack` for a `cmd` (api 5). `result` is omitted when void. */
    void sendAck (const String& id, bool ok, const String& errorCode = {},
                  const String& errorMessage = {}, const var& result = {});

    /** Ask the connection thread to send a close frame and hang up. */
    void close (int code, const String& reason = {});

private:
    friend struct ApiServerImpl;
    friend class ApiConnection;

    ApiSession (int64 id, const String& remote) : sessionId (id), remoteAddress (remote) {}
    void enqueueRaw (std::string frame);

    const int64 sessionId;
    const String remoteAddress;
    std::atomic<bool> authenticated { false };
    std::atomic<bool> closed { false };
    std::atomic<bool> hidden { false };
    std::atomic<int> closeCode { -1 };

    mutable std::mutex lock;                // guards everything below
    std::deque<std::string> outbound;
    size_t outboundBytes = 0;
    String closeReason;
    StringArray topics;
};

//==============================================================================
class ApiServer
{
public:
    explicit ApiServer (const ApiConfig& config);
    ~ApiServer();                           // calls stop()

    /** Binds the listener and starts the threads. On failure the Result carries
        a message fit for the user and nothing is left running. */
    Result start();

    /** Closes every session with 1001, closes the listener and joins all
        threads. Idempotent. Bounded to a few seconds. */
    void stop();

    int getPort() const;
    int getNumSessions() const;

    //== P4.2: state ===========================================================
    /** Returns the full state object sent after auth and on `resync`. Called on a
        connection thread under the state lock, so it must be thread-safe and
        quick. Default: a placeholder {"self":{"name","role"}}. */
    void setStateProvider (std::function<var()> provider);

    /** Bumps `rev` and sends {"t":"patch","rev","ops"} to every authenticated
        session. `ops` is a var array of {op,path,value}. Returns the new rev. */
    int64 publishPatch (const var& ops);

    //== P4.3: meters and other topics ==========================================
    void broadcast (const String& json);
    /** Only authenticated sessions subscribed to `topic` and not hidden. */
    void broadcastTopic (const String& topic, const String& json);

    //== P4.4: commands =========================================================
    using CommandHandler = std::function<void (std::shared_ptr<ApiSession>, const var& message)>;
    /** Called on the JUCE message thread for every well-formed `cmd` message and
        must produce exactly one ack via session->sendAck(). With no handler set
        every command is answered `not_supported`. */
    void setCommandHandler (CommandHandler handler);

    /** Message thread; fired after a session's socket is gone. */
    void setSessionClosedHandler (std::function<void (int64 sessionId)> handler);

private:
    std::unique_ptr<ApiServerImpl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApiServer)
};

} // namespace crosspoint
