// Crosspoint control API server (P4.1). See ApiServer.h and docs/control-api.md.

#include "ApiServer.h"
#include "ApiSha1.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#if ! JUCE_WINDOWS
 #include <signal.h>
 #include <sys/socket.h>
#endif

namespace crosspoint {

namespace {

constexpr int    kMaxConnections    = 32;
constexpr size_t kMaxHeaderBytes    = 16 * 1024;
constexpr size_t kMaxMessageBytes   = 1024 * 1024;       // one incoming WS message
constexpr size_t kMaxQueuedBytes    = 8 * 1024 * 1024;   // slow-consumer cut-off
constexpr int64  kMaxStaticBytes    = 32 * 1024 * 1024;
constexpr int    kHeaderTimeoutMs   = 5000;
constexpr int    kAuthTimeoutMs     = 10000;
constexpr int    kPingIntervalMs    = 30000;
constexpr int    kSendTimeoutMs     = 2000;
constexpr int    kJsonMaxDepth      = 64;

const char* const kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

String toJson (const var& v)                { return JSON::toString (v, true); }

var makeObject (std::initializer_list<std::pair<const char*, var>> props)
{
    auto* o = new DynamicObject();
    for (auto& p : props) o->setProperty (p.first, p.second);
    return var (o);
}

bool constantTimeEquals (const String& a, const String& b)
{
    const auto ua = a.toStdString(), ub = b.toStdString();
    unsigned diff = (unsigned) (ua.size() ^ ub.size());
    const size_t n = std::max (ua.size(), ub.size());
    for (size_t i = 0; i < n; ++i)
        diff |= (unsigned) ((i < ua.size() ? (uint8_t) ua[i] : 0) ^ (i < ub.size() ? (uint8_t) ub[i] : 0));
    return diff == 0;
}

/** Nesting depth guard: JUCE's JSON parser is recursive, so a 1 MB message of
    '[' characters would otherwise overflow the thread's stack. */
bool jsonDepthOk (const std::string& s)
{
    int depth = 0;
    bool inString = false, escape = false;
    for (char c : s)
    {
        if (inString)
        {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') inString = false;
        }
        else if (c == '"') inString = true;
        else if (c == '[' || c == '{') { if (++depth > kJsonMaxDepth) return false; }
        else if (c == ']' || c == '}') { if (depth > 0) --depth; }
    }
    return true;
}

std::string makeFrame (uint8_t opcode, const void* payload, size_t len)
{
    std::string f;
    f.reserve (len + 10);
    f.push_back ((char) (0x80 | opcode));                  // FIN, server frames are never masked
    if (len < 126)            f.push_back ((char) len);
    else if (len <= 0xFFFF) { f.push_back ((char) 126); f.push_back ((char) (len >> 8)); f.push_back ((char) len); }
    else
    {
        f.push_back ((char) 127);
        for (int i = 7; i >= 0; --i) f.push_back ((char) ((uint64_t) len >> (8 * i)));
    }
    f.append (static_cast<const char*> (payload), len);
    return f;
}

std::string makeCloseFrame (int code, const String& reason)
{
    std::string p;
    p.push_back ((char) (code >> 8));
    p.push_back ((char) code);
    auto r = reason.toStdString();
    if (r.size() > 100) r.resize (100);                    // may cut a UTF-8 sequence; reasons are ASCII
    p += r;
    return makeFrame (0x8, p.data(), p.size());
}

/** "host", "host:port", "[::1]:port" -> host part, lower case. */
String hostOf (const String& hostPort)
{
    if (hostPort.startsWithChar ('['))
    {
        auto end = hostPort.indexOfChar (']');
        return end > 0 ? hostPort.substring (0, end + 1) : hostPort;
    }
    return hostPort.containsChar (':') ? hostPort.upToLastOccurrenceOf (":", false, false) : hostPort;
}

int portOf (const String& hostPort, int defaultPort)
{
    String rest = hostPort.startsWithChar ('[') ? hostPort.fromFirstOccurrenceOf ("]", false, false) : hostPort;
    if (rest.containsChar (':'))
    {
        auto p = rest.fromLastOccurrenceOf (":", false, false);
        if (p.isNotEmpty() && p.containsOnly ("0123456789") && p.length() <= 5) return p.getIntValue();
        return -1;
    }
    return defaultPort;
}

bool isLocalHostName (const String& host)
{
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

String contentTypeFor (const File& f)
{
    auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".js" || ext == ".mjs")   return "text/javascript; charset=utf-8";
    if (ext == ".css")                   return "text/css; charset=utf-8";
    if (ext == ".json" || ext == ".map") return "application/json; charset=utf-8";
    if (ext == ".webmanifest")           return "application/manifest+json; charset=utf-8";
    if (ext == ".png")                   return "image/png";
    if (ext == ".svg")                   return "image/svg+xml";
    if (ext == ".ico")                   return "image/x-icon";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")                   return "image/gif";
    if (ext == ".webp")                  return "image/webp";
    if (ext == ".woff2")                 return "font/woff2";
    if (ext == ".woff")                  return "font/woff";
    if (ext == ".txt")                   return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

const char* statusText (int code)
{
    switch (code)
    {
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 426: return "Upgrade Required";
        case 431: return "Request Header Fields Too Large";
        case 503: return "Service Unavailable";
        default:  return "Error";
    }
}

} // namespace

//==============================================================================
bool ApiConfig::isLoopbackAddress (const String& addr)
{
    auto a = addr.trim().toLowerCase();
    return a == "localhost" || a == "::1" || a == "[::1]" || a.startsWith ("127.");
}

//==============================================================================
bool ApiSession::isSubscribed (const String& topic) const
{
    std::lock_guard<std::mutex> g (lock);
    return topics.contains (topic);
}

void ApiSession::enqueueRaw (std::string frame)
{
    if (closed) return;
    std::lock_guard<std::mutex> g (lock);
    if (outboundBytes + frame.size() > kMaxQueuedBytes)
    {
        int expected = -1;                                  // slow consumer: drop it
        closeCode.compare_exchange_strong (expected, 1008);
        closeReason = "slow consumer";
        return;
    }
    outboundBytes += frame.size();
    outbound.push_back (std::move (frame));
}

void ApiSession::send (const String& json)
{
    const auto utf8 = json.toStdString();
    enqueueRaw (makeFrame (0x1, utf8.data(), utf8.size()));
}

void ApiSession::sendAck (const String& id, bool ok, const String& errorCode,
                          const String& errorMessage, const var& result)
{
    auto* o = new DynamicObject();
    o->setProperty ("t", "ack");
    o->setProperty ("id", id);
    o->setProperty ("ok", ok);
    if (! ok)
        o->setProperty ("error", makeObject ({ { "code", errorCode.isEmpty() ? String ("internal") : errorCode },
                                               { "message", errorMessage } }));
    else if (! result.isVoid())
        o->setProperty ("result", result);
    send (toJson (var (o)));
}

void ApiSession::close (int code, const String& reason)
{
    int expected = -1;
    if (closeCode.compare_exchange_strong (expected, code))
    {
        std::lock_guard<std::mutex> g (lock);
        closeReason = reason;
    }
}

//==============================================================================
struct ApiServerImpl
{
    explicit ApiServerImpl (const ApiConfig& c) : config (c) {}

    ApiConfig config;
    StreamingSocket listener;
    std::atomic<bool> stopping { false };
    std::atomic<bool> running { false };

    std::unique_ptr<Thread> acceptThread;
    std::vector<std::unique_ptr<ApiConnection>> connections;    // accept thread, then stop()

    mutable std::mutex sessionsLock;
    std::vector<std::shared_ptr<ApiSession>> sessions;
    std::atomic<int64> nextSessionId { 1 };

    std::mutex stateLock;                                       // orders state/patch delivery
    int64 rev = 1;
    std::function<var()> stateProvider;

    std::mutex handlerLock;
    ApiServer::CommandHandler commandHandler;
    std::function<void (int64)> closedHandler;

    //--------------------------------------------------------------------------
    var placeholderState() const
    {
        return makeObject ({ { "self", makeObject ({ { "name", config.selfName }, { "role", config.role } }) } });
    }

    String stateMessageLocked()
    {
        var st = stateProvider ? stateProvider() : placeholderState();
        return toJson (makeObject ({ { "t", "state" }, { "rev", (int64) rev }, { "state", st } }));
    }

    /** Marks the session authenticated and queues the first state atomically
        with respect to publishPatch(), so no patch can overtake the snapshot. */
    void authenticateAndSendState (ApiSession& s)
    {
        std::lock_guard<std::mutex> g (stateLock);
        s.authenticated = true;
        s.send (stateMessageLocked());
    }

    void sendState (ApiSession& s)
    {
        std::lock_guard<std::mutex> g (stateLock);
        s.send (stateMessageLocked());
    }

    std::vector<std::shared_ptr<ApiSession>> snapshotSessions() const
    {
        std::lock_guard<std::mutex> g (sessionsLock);
        return sessions;
    }

    void addSession (const std::shared_ptr<ApiSession>& s)
    {
        std::lock_guard<std::mutex> g (sessionsLock);
        sessions.push_back (s);
    }

    void removeSession (const std::shared_ptr<ApiSession>& s)
    {
        s->closed = true;
        {
            std::lock_guard<std::mutex> g (sessionsLock);
            sessions.erase (std::remove (sessions.begin(), sessions.end(), s), sessions.end());
        }
        std::function<void (int64)> h;
        { std::lock_guard<std::mutex> g (handlerLock); h = closedHandler; }
        if (h)
        {
            const auto id = s->getId();
            MessageManager::callAsync ([h, id] { h (id); });
        }
    }

    bool originAllowed (const String& origin, const String& hostHeader) const;
    bool hostAllowed (const String& hostHeader) const;
};

//==============================================================================
/** One accepted socket: serves a single HTTP request, or becomes a WebSocket
    session for as long as it lives. */
class ApiConnection : public Thread
{
public:
    ApiConnection (ApiServerImpl& s, std::unique_ptr<StreamingSocket> sock)
        : Thread ("crosspoint-api-conn"), server (s), socket (std::move (sock)) {}

    ~ApiConnection() override { stopThread (3000); }

    void run() override
    {
        try
        {
            configureSocket();
            serve();
        }
        catch (const std::exception& e)
        {
            DBG ("ApiConnection: exception " << e.what());
        }
        catch (...) {}
        socket->close();
    }

private:
    struct Request
    {
        std::string method, target, version;
        String path;
        std::map<std::string, std::string> headers;       // lower-case names
        String header (const char* name) const
        {
            auto it = headers.find (name);
            return it == headers.end() ? String() : String::fromUTF8 (it->second.data(), (int) it->second.size());
        }
        bool has (const char* name) const { return headers.count (name) != 0; }
    };

    ApiServerImpl& server;
    std::unique_ptr<StreamingSocket> socket;
    std::string inBuf;                                    // bytes read but not yet consumed
    std::shared_ptr<ApiSession> session;
    bool peerClosed = false;                              // received a close frame (echoed)
    bool peerGone = false;                                // TCP gone

    //--------------------------------------------------------------------------
    void configureSocket()
    {
        const int h = socket->getRawSocketHandle();
       #if JUCE_WINDOWS
        DWORD tv = kSendTimeoutMs;
        ::setsockopt ((SOCKET) h, SOL_SOCKET, SO_SNDTIMEO, (const char*) &tv, sizeof (tv));
       #else
        struct timeval tv;
        tv.tv_sec = kSendTimeoutMs / 1000;
        tv.tv_usec = (kSendTimeoutMs % 1000) * 1000;
        ::setsockopt (h, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv));
        #ifdef SO_NOSIGPIPE
        int one = 1;
        ::setsockopt (h, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof (one));
        #endif
       #endif
    }

    bool writeAll (const char* data, size_t len)
    {
        while (len > 0)
        {
            const int n = socket->write (data, (int) std::min<size_t> (len, 64 * 1024));
            if (n <= 0) { peerGone = true; return false; }
            data += n; len -= (size_t) n;
        }
        return true;
    }
    bool writeAll (const std::string& s)            { return writeAll (s.data(), s.size()); }

    /** Returns >0 bytes read, 0 on timeout, -1 on EOF/error. */
    int readSome (int timeoutMs)
    {
        const int r = socket->waitUntilReady (true, timeoutMs);
        if (r < 0) return -1;
        if (r == 0) return 0;
        char tmp[8192];
        const int n = socket->read (tmp, sizeof (tmp), false);
        if (n <= 0) return -1;
        inBuf.append (tmp, (size_t) n);
        return n;
    }

    //== HTTP ==================================================================
    void respond (int code, const String& contentType, const std::string& body,
                  bool headOnly = false, const String& extraHeaders = {})
    {
        std::string h = "HTTP/1.1 " + std::to_string (code) + " " + statusText (code) + "\r\n";
        h += "Content-Type: " + contentType.toStdString() + "\r\n";
        h += "Content-Length: " + std::to_string (body.size()) + "\r\n";
        h += "Connection: close\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\n";
        h += extraHeaders.toStdString();
        h += "\r\n";
        if (! headOnly) h += body;
        writeAll (h);
    }

    void respondText (int code, const String& msg, const String& extraHeaders = {})
    {
        respond (code, "text/plain; charset=utf-8", (msg + "\n").toStdString(), false, extraHeaders);
    }

    bool readRequest (Request& req)
    {
        const auto start = Time::getMillisecondCounter();
        size_t endPos;
        while ((endPos = inBuf.find ("\r\n\r\n")) == std::string::npos)
        {
            if (inBuf.size() > kMaxHeaderBytes) { respondText (431, "Request headers too large"); return false; }
            if (threadShouldExit() || Time::getMillisecondCounter() - start > (uint32) kHeaderTimeoutMs) return false;
            if (readSome (50) < 0) return false;
        }

        const std::string head = inBuf.substr (0, endPos);
        inBuf.erase (0, endPos + 4);

        size_t pos = 0;
        auto nextLine = [&] (std::string& line) {
            if (pos > head.size()) return false;
            auto e = head.find ("\r\n", pos);
            if (e == std::string::npos) e = head.size();
            line = head.substr (pos, e - pos);
            pos = e + 2;
            return true;
        };

        std::string line;
        nextLine (line);
        const auto sp1 = line.find (' ');
        const auto sp2 = line.rfind (' ');
        if (sp1 == std::string::npos || sp2 == sp1)
        {
            respondText (400, "Bad request line");
            return false;
        }
        req.method = line.substr (0, sp1);
        req.target = line.substr (sp1 + 1, sp2 - sp1 - 1);
        req.version = line.substr (sp2 + 1);
        if (req.version.compare (0, 7, "HTTP/1.") != 0 || req.target.empty() || req.target[0] != '/')
        {
            respondText (400, "Bad request");
            return false;
        }

        while (nextLine (line) && ! line.empty())
        {
            const auto colon = line.find (':');
            if (colon == std::string::npos || colon == 0) { respondText (400, "Bad header"); return false; }
            auto name = line.substr (0, colon);
            std::transform (name.begin(), name.end(), name.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            const auto value = String::fromUTF8 (line.data() + colon + 1, (int) (line.size() - colon - 1)).trim().toStdString();
            auto& slot = req.headers[name];
            slot = slot.empty() ? value : slot + ", " + value;
        }

        // Percent-decode the path (query string dropped). Reject bad escapes and NULs.
        std::string raw = req.target.substr (0, req.target.find ('?')), decoded;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] == '%')
            {
                if (i + 2 >= raw.size()) { respondText (400, "Bad escape"); return false; }
                const int hi = CharacterFunctions::getHexDigitValue ((juce_wchar) raw[i + 1]);
                const int lo = CharacterFunctions::getHexDigitValue ((juce_wchar) raw[i + 2]);
                if (hi < 0 || lo < 0) { respondText (400, "Bad escape"); return false; }
                decoded.push_back ((char) (hi * 16 + lo));
                i += 2;
            }
            else decoded.push_back (raw[i]);
        }
        if (decoded.find ('\0') != std::string::npos || ! CharPointer_UTF8::isValidString (decoded.c_str(), (int) decoded.size()))
        {
            respondText (400, "Bad path");
            return false;
        }
        req.path = String::fromUTF8 (decoded.data(), (int) decoded.size());
        return true;
    }

    void serve()
    {
        Request req;
        if (! readRequest (req)) return;

        // DNS-rebinding guard: on a loopback bind only loopback Host names are valid.
        if (! server.hostAllowed (req.header ("host")))
        {
            respondText (403, "Forbidden host");
            return;
        }

        const bool isGet  = req.method == "GET";
        const bool isHead = req.method == "HEAD";

        if (req.path == "/api/v1/ws")
        {
            if (! isGet) { respondText (405, "Method not allowed", "Allow: GET\r\n"); return; }
            upgradeToWebSocket (req);
            return;
        }

        if (! (isGet || isHead))
        {
            respondText (405, "Method not allowed", "Allow: GET, HEAD\r\n");
            return;
        }

        if (req.path == "/api/v1/health")
        {
            auto body = toJson (makeObject ({ { "ok", true }, { "app", server.config.appName },
                                              { "version", server.config.version },
                                              { "role", server.config.role } }));
            respond (200, "application/json; charset=utf-8", body.toStdString(), isHead);
            return;
        }

        if (req.path.startsWith ("/api/"))
        {
            respond (404, "application/json; charset=utf-8",
                     toJson (makeObject ({ { "ok", false }, { "error", "not_found" } })).toStdString(), isHead);
            return;
        }

        serveStatic (req, isHead);
    }

    void serveStatic (const Request& req, bool headOnly)
    {
        const File root = server.config.uiDir;
        if (root == File() || ! root.isDirectory())
        {
            respondText (404, "No UI directory configured (use --ui-dir)");
            return;
        }

        String rel = req.path;
        if (rel == "/")
            rel = server.config.role == "vdi" ? "/agent.html" : "/index.html";

        // Traversal: refuse outright rather than normalising. Backslashes are
        // refused too so a Windows build cannot be tricked by "..\\".
        if (rel.containsChar ('\\'))
        {
            respondText (403, "Forbidden");
            return;
        }
        StringArray segments;
        segments.addTokens (rel.substring (1), "/", "");
        for (auto& seg : segments)
        {
            if (seg == ".." || seg == ".") { respondText (403, "Forbidden"); return; }
            if (seg.startsWithChar ('.'))  { respondText (404, "Not found"); return; }   // dotfiles are never served
        }

        const File f = root.getChildFile (rel.substring (1));
        if (! f.isAChildOf (root) || f.isDirectory() || ! f.existsAsFile())
        {
            respondText (404, "Not found");
            return;
        }
        if (f.getSize() > kMaxStaticBytes) { respondText (413, "File too large"); return; }

        MemoryBlock data;
        if (! f.loadFileAsData (data)) { respondText (404, "Not found"); return; }
        respond (200, contentTypeFor (f), std::string (static_cast<const char*> (data.getData()), data.getSize()), headOnly);
    }

    //== WebSocket =============================================================
    void upgradeToWebSocket (const Request& req)
    {
        auto contains = [] (const String& list, const String& token) {
            StringArray parts; parts.addTokens (list.toLowerCase(), ",", "");
            for (auto& p : parts) if (p.trim() == token) return true;
            return false;
        };

        if (! req.has ("upgrade") || req.header ("upgrade").trim().toLowerCase() != "websocket"
            || ! contains (req.header ("connection"), "upgrade"))
        {
            respondText (426, "WebSocket upgrade required", "Upgrade: websocket\r\n");
            return;
        }
        if (req.header ("sec-websocket-version").trim() != "13")
        {
            respondText (426, "Unsupported WebSocket version", "Sec-WebSocket-Version: 13\r\n");
            return;
        }
        const auto key = req.header ("sec-websocket-key").trim();
        MemoryOutputStream keyBytes;
        if (key.length() != 24 || ! Base64::convertFromBase64 (keyBytes, key) || keyBytes.getDataSize() != 16)
        {
            respondText (400, "Bad Sec-WebSocket-Key");
            return;
        }

        // Origin (api 1). Browsers always send it; non-browser clients (websocat,
        // Node) send none, which is allowed because they are not subject to CSRF.
        if (req.has ("origin") && ! server.originAllowed (req.header ("origin"), req.header ("host")))
        {
            respondText (403, "Origin not allowed");
            return;
        }

        if (server.stopping) { respondText (503, "Shutting down"); return; }

        const std::string seed = key.toStdString() + kWsGuid;
        const auto digest = Sha1::hash (seed.data(), seed.size());
        const auto accept = Base64::toBase64 (digest.data(), digest.size());

        if (! writeAll ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: " + accept.toStdString() + "\r\n\r\n"))
            return;

        session = std::shared_ptr<ApiSession> (new ApiSession (server.nextSessionId++, socket->getHostName()));
        server.addSession (session);

        const bool needToken = ! server.config.token.isEmpty() || ! server.config.isLoopbackBind();
        session->send (toJson (makeObject ({
            { "t", "hello" }, { "v", 1 }, { "app", server.config.appName }, { "version", server.config.version },
            { "role", server.config.role }, { "auth", needToken ? "token" : "none" },
            { "features", [&] { Array<var> a; for (auto& f : server.config.features) a.add (f); return var (a); }() } })));

        if (! needToken)
            server.authenticateAndSendState (*session);

        webSocketLoop();
        server.removeSession (session);
    }

    bool flushOutbound()
    {
        for (;;)
        {
            std::string frame;
            {
                std::lock_guard<std::mutex> g (session->lock);
                if (session->outbound.empty()) return true;
                frame = std::move (session->outbound.front());
                session->outbound.pop_front();
                session->outboundBytes -= frame.size();
            }
            if (! writeAll (frame)) return false;
        }
    }

    void webSocketLoop()
    {
        const auto start = Time::getMillisecondCounter();
        auto lastPing = start;

        while (! threadShouldExit())
        {
            if (! flushOutbound()) break;
            if (peerClosed || session->closeCode >= 0) break;

            const auto now = Time::getMillisecondCounter();
            if (! session->authenticated && now - start > (uint32) kAuthTimeoutMs)
            {
                session->close (4401, "auth timeout");
                continue;
            }
            if (now - lastPing > (uint32) kPingIntervalMs)
            {
                session->enqueueRaw (makeFrame (0x9, "", 0));
                lastPing = now;
            }

            const int n = readSome (5);
            if (n < 0) { peerGone = true; break; }
            if (n > 0) processFrames();
        }

        if (peerGone) return;

        // Closing handshake: flush what is queued (an auth reply, say), then our close frame,
        // then wait for the peer to hang up so the frame is not lost to a TCP reset.
        flushOutbound();
        if (! peerClosed)
        {
            int code = session->closeCode;
            String reason;
            { std::lock_guard<std::mutex> g (session->lock); reason = session->closeReason; }
            if (code < 0) code = 1001;
            writeAll (makeCloseFrame (code, reason));

           #if JUCE_WINDOWS
            ::shutdown ((SOCKET) socket->getRawSocketHandle(), 1);
           #else
            ::shutdown (socket->getRawSocketHandle(), SHUT_WR);
           #endif

            const auto t0 = Time::getMillisecondCounter();
            const uint32 budget = server.stopping ? 200 : 1000;
            while (Time::getMillisecondCounter() - t0 < budget)
            {
                if (readSome (20) < 0) break;
                inBuf.clear();
            }
        }
    }

    void fail (int code, const String& reason)
    {
        session->close (code, reason);
        inBuf.clear();
    }

    void processFrames()
    {
        for (;;)
        {
            if (session->closeCode >= 0 || peerClosed) return;
            if (inBuf.size() < 2) return;

            const auto* b = reinterpret_cast<const uint8_t*> (inBuf.data());
            const bool fin = (b[0] & 0x80) != 0;
            const int rsv = b[0] & 0x70;
            const int opcode = b[0] & 0x0F;
            const bool masked = (b[1] & 0x80) != 0;
            uint64_t len = b[1] & 0x7F;
            size_t hdr = 2;

            if (len == 126)      { if (inBuf.size() < 4) return;  len = ((uint64_t) b[2] << 8) | b[3]; hdr = 4; }
            else if (len == 127)
            {
                if (inBuf.size() < 10) return;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | b[2 + i];
                hdr = 10;
            }

            if (rsv != 0 || ! masked) { fail (1002, "protocol error"); return; }
            if (len > kMaxMessageBytes) { fail (1009, "message too big"); return; }
            if (inBuf.size() < hdr + 4 + (size_t) len) return;          // wait for the rest

            uint8_t mask[4];
            std::memcpy (mask, b + hdr, 4);
            std::string payload (inBuf, hdr + 4, (size_t) len);
            for (size_t i = 0; i < payload.size(); ++i) payload[i] = (char) ((uint8_t) payload[i] ^ mask[i & 3]);
            inBuf.erase (0, hdr + 4 + (size_t) len);

            if (opcode >= 0x8)                                          // control frames
            {
                if (! fin || len > 125) { fail (1002, "bad control frame"); return; }
                if (opcode == 0x8)
                {
                    int code = 1005;
                    if (payload.size() == 1) { fail (1002, "bad close"); return; }
                    if (payload.size() >= 2) code = ((uint8_t) payload[0] << 8) | (uint8_t) payload[1];
                    session->enqueueRaw (makeCloseFrame (code == 1005 ? 1000 : code, {}));
                    peerClosed = true;
                    return;
                }
                if (opcode == 0x9) session->enqueueRaw (makeFrame (0xA, payload.data(), payload.size()));
                else if (opcode != 0xA) { fail (1002, "unknown opcode"); return; }
                continue;
            }

            if (opcode == 0x0)                                          // continuation
            {
                if (fragOpcode == 0) { fail (1002, "unexpected continuation"); return; }
            }
            else if (opcode == 0x1 || opcode == 0x2)
            {
                if (fragOpcode != 0) { fail (1002, "expected continuation"); return; }
                fragOpcode = opcode;
                fragment.clear();
            }
            else { fail (1002, "unknown opcode"); return; }

            fragment += payload;
            if (fragment.size() > kMaxMessageBytes) { fail (1009, "message too big"); return; }
            if (! fin) continue;

            const int type = fragOpcode;
            fragOpcode = 0;
            std::string message = std::move (fragment);
            fragment.clear();

            if (type == 0x2) { fail (1003, "binary frames are not supported"); return; }
            if (! CharPointer_UTF8::isValidString (message.c_str(), (int) message.size())) { fail (1007, "invalid UTF-8"); return; }
            handleMessage (message);
        }
    }

    int fragOpcode = 0;
    std::string fragment;

    //== Protocol messages ======================================================
    void handleMessage (const std::string& text)
    {
        var msg;
        if (! jsonDepthOk (text) || ! JSON::parse (String::fromUTF8 (text.data(), (int) text.size()), msg).wasOk()
            || ! msg.isObject() || ! msg.getProperty ("t", var()).isString())
        {
            fail (4400, "bad message");
            return;
        }

        const String type = msg.getProperty ("t", var()).toString();

        if (! session->authenticated)
        {
            // Token required: the first message must be a valid auth (api 2.2).
            const bool ok = type == "auth" && msg.getProperty ("token", var()).isString()
                            && ! server.config.token.isEmpty()
                            && constantTimeEquals (msg.getProperty ("token", var()).toString(), server.config.token);
            session->send (toJson (makeObject ({ { "t", "auth" }, { "ok", ok } })));
            if (ok) server.authenticateAndSendState (*session);
            else    session->close (4401, "auth failed");
            return;
        }

        if (type == "auth")            session->send (toJson (makeObject ({ { "t", "auth" }, { "ok", true } })));
        else if (type == "resync")     server.sendState (*session);
        else if (type == "sub" || type == "unsub")
        {
            if (auto* arr = msg.getProperty ("topics", var()).getArray())
            {
                std::lock_guard<std::mutex> g (session->lock);
                for (auto& t : *arr)
                    if (t.isString())
                    {
                        if (type == "sub") session->topics.addIfNotAlreadyThere (t.toString());
                        else               session->topics.removeString (t.toString());
                    }
            }
        }
        else if (type == "visibility")
            session->hidden = (bool) msg.getProperty ("hidden", var (false));
        else if (type == "cmd")
        {
            const auto id = msg.getProperty ("id", var());
            const auto cmd = msg.getProperty ("cmd", var());
            if (! id.isString() || ! cmd.isString()) { fail (4400, "bad cmd"); return; }

            ApiServer::CommandHandler handler;
            { std::lock_guard<std::mutex> g (server.handlerLock); handler = server.commandHandler; }

            if (! handler)
            {
                session->sendAck (id.toString(), false, "not_supported", "Commands are not available yet");
                return;
            }
            // Engine calls belong on the message thread, never on this one.
            auto s = session;
            MessageManager::callAsync ([handler, s, msg] { handler (s, msg); });
        }
        // anything else is ignored (api 2.1)
    }
};

//==============================================================================
bool ApiServerImpl::hostAllowed (const String& hostHeader) const
{
    if (! config.isLoopbackBind()) return true;
    if (hostHeader.isEmpty()) return false;
    return isLocalHostName (hostOf (hostHeader.trim().toLowerCase()));
}

bool ApiServerImpl::originAllowed (const String& originRaw, const String& hostHeader) const
{
    auto origin = originRaw.trim().toLowerCase();
    while (origin.endsWithChar ('/')) origin = origin.dropLastCharacters (1);

    for (auto& a : config.allowedOrigins)
    {
        auto allowed = a.trim().toLowerCase();
        while (allowed.endsWithChar ('/')) allowed = allowed.dropLastCharacters (1);
        if (allowed.isNotEmpty() && allowed == origin) return true;
    }

    String scheme = origin.upToFirstOccurrenceOf ("://", false, false);
    if (! origin.contains ("://") || (scheme != "http" && scheme != "https")) return false;   // includes "null"
    const String hostPort = origin.fromFirstOccurrenceOf ("://", false, false);
    if (hostPort.isEmpty() || hostPort.containsAnyOf ("/?#@ ")) return false;

    const String host = hostOf (hostPort);
    const int port = portOf (hostPort, scheme == "https" ? 443 : 80);
    if (port < 0) return false;

    if (scheme == "http" && isLocalHostName (host) && port == config.port) return true;

    // Same origin as the Host header. Only meaningful on non-loopback binds: on loopback a
    // rebinding attacker controls the Host name too, which hostAllowed() already rejects.
    if (! config.isLoopbackBind() && hostHeader.isNotEmpty())
    {
        const auto hh = hostHeader.trim().toLowerCase();
        if (hostOf (hh) == host && (portOf (hh, port) == port)) return true;
    }
    return false;
}

//==============================================================================
namespace {
class AcceptThread : public Thread
{
public:
    explicit AcceptThread (ApiServerImpl& s) : Thread ("crosspoint-api-accept"), server (s) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            reap();

            const int ready = server.listener.waitUntilReady (true, 100);
            if (ready < 0) { Thread::sleep (50); continue; }
            if (ready == 0) continue;

            std::unique_ptr<StreamingSocket> sock (server.listener.waitForNextConnection());
            if (sock == nullptr) continue;

            if ((int) server.connections.size() >= kMaxConnections)
            {
                static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                sock->write (busy, (int) sizeof (busy) - 1);
                continue;
            }

            auto conn = std::make_unique<ApiConnection> (server, std::move (sock));
            conn->startThread();
            server.connections.push_back (std::move (conn));
        }
    }

private:
    void reap()
    {
        auto& c = server.connections;
        c.erase (std::remove_if (c.begin(), c.end(),
                                 [] (const std::unique_ptr<ApiConnection>& p) { return ! p->isThreadRunning(); }),
                 c.end());
    }

    ApiServerImpl& server;
};
} // namespace

//==============================================================================
ApiServer::ApiServer (const ApiConfig& config) : impl (new ApiServerImpl (config)) {}

ApiServer::~ApiServer() { stop(); }

Result ApiServer::start()
{
    auto& c = impl->config;
    if (impl->running) return Result::ok();
    if (c.port < 1 || c.port > 65535)
        return Result::fail ("API port must be 1-65535 (0 disables the server)");
    if (! c.isLoopbackBind() && c.token.isEmpty())
        return Result::fail ("refusing to serve the control API on non-loopback address '" + c.bindAddress
                             + "' without a token: set --api-token");

   #if ! JUCE_WINDOWS
    ::signal (SIGPIPE, SIG_IGN);       // a client vanishing mid-write must not kill the app
   #endif

    if (! impl->listener.createListener (c.port, c.bindAddress))
        return Result::fail ("cannot listen on " + c.bindAddress + ":" + String (c.port) + " (address in use or invalid)");

    impl->stopping = false;
    impl->running = true;
    impl->acceptThread = std::make_unique<AcceptThread> (*impl);
    impl->acceptThread->startThread();
    return Result::ok();
}

void ApiServer::stop()
{
    if (! impl->running.exchange (false)) return;
    impl->stopping = true;

    if (impl->acceptThread) impl->acceptThread->stopThread (3000);
    impl->acceptThread.reset();

    // Ask every connection to wind down first, then join them, so they close in parallel.
    for (auto& c : impl->connections) c->signalThreadShouldExit();
    for (auto& c : impl->connections) c->stopThread (5000);
    impl->connections.clear();

    impl->listener.close();
    {
        std::lock_guard<std::mutex> g (impl->sessionsLock);
        impl->sessions.clear();
    }
}

int ApiServer::getPort() const              { return impl->config.port; }

int ApiServer::getNumSessions() const
{
    std::lock_guard<std::mutex> g (impl->sessionsLock);
    return (int) impl->sessions.size();
}

void ApiServer::setStateProvider (std::function<var()> provider)
{
    std::lock_guard<std::mutex> g (impl->stateLock);
    impl->stateProvider = std::move (provider);

    // The engine registers its provider after the listener is up, so sessions
    // that connected earlier hold the placeholder: send them the real state
    // (rev + 1, like any state/patch) so they never keep a stale snapshot.
    if (impl->stateProvider)
    {
        ++impl->rev;
        const auto json = impl->stateMessageLocked();
        for (auto& s : impl->snapshotSessions())
            if (s->isAuthenticated()) s->send (json);
    }
}

int64 ApiServer::publishPatch (const var& ops)
{
    return publishPatch (ops, {});
}

int64 ApiServer::publishPatch (const var& ops, const std::function<void()>& underStateLock)
{
    std::lock_guard<std::mutex> g (impl->stateLock);
    const auto r = ++impl->rev;
    if (underStateLock) underStateLock();
    const auto json = toJson (makeObject ({ { "t", "patch" }, { "rev", (int64) r }, { "ops", ops } }));
    for (auto& s : impl->snapshotSessions())
        if (s->isAuthenticated()) s->send (json);
    return r;
}

void ApiServer::broadcast (const String& json)
{
    for (auto& s : impl->snapshotSessions())
        if (s->isAuthenticated()) s->send (json);
}

void ApiServer::broadcastTopic (const String& topic, const String& json)
{
    for (auto& s : impl->snapshotSessions())
        if (s->isAuthenticated() && ! s->isHidden() && s->isSubscribed (topic)) s->send (json);
}

void ApiServer::setCommandHandler (CommandHandler handler)
{
    std::lock_guard<std::mutex> g (impl->handlerLock);
    impl->commandHandler = std::move (handler);
}

void ApiServer::setSessionClosedHandler (std::function<void (int64)> handler)
{
    std::lock_guard<std::mutex> g (impl->handlerLock);
    impl->closedHandler = std::move (handler);
}

} // namespace crosspoint
