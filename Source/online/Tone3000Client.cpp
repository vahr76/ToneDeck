#include "Tone3000Client.h"
#include <juce_cryptography/juce_cryptography.h>
#include <random>

#ifndef TONEDECK_T3K_CLIENT_ID
 #define TONEDECK_T3K_CLIENT_ID ""
#endif

namespace
{
const juce::String apiOrigin = "https://www.tone3000.com";
const juce::String apiBase = apiOrigin + "/api/v1";
constexpr int kHttpTimeoutMs   = 20000;
constexpr int kBrowserWaitMs   = 5 * 60 * 1000;
constexpr int kFirstPort       = 53917;   // loopback redirect port range
constexpr int kNumPorts        = 20;

juce::String base64Url (const void* data, size_t size)
{
    return juce::Base64::toBase64 (data, size).replace ("+", "-").replace ("/", "_").trimCharactersAtEnd ("=");
}

juce::String randomToken (int bytes)
{
    // OS entropy (std::random_device is a CSPRNG on Windows, macOS and Linux)
    juce::HeapBlock<juce::uint8> buf ((size_t) bytes);
    std::random_device rd;
    for (int i = 0; i < bytes; ++i)
        buf[i] = (juce::uint8) (rd() & 0xff);
    return base64Url (buf.getData(), (size_t) bytes);
}

juce::String formEncode (const juce::StringPairArray& p)
{
    juce::StringArray parts;
    for (int i = 0; i < p.size(); ++i)
        parts.add (juce::URL::addEscapeChars (p.getAllKeys()[i], true) + "="
                   + juce::URL::addEscapeChars (p.getAllValues()[i], true));
    return parts.joinIntoString ("&");
}

juce::String safeName (const juce::String& s)
{
    auto n = juce::File::createLegalFileName (s.trim()).replace ("/", "-").replace ("\\", "-");
    return n.isEmpty() ? juce::String ("untitled") : n.substring (0, 80);
}

const char* callbackPage (bool ok)
{
    return ok
        ? "<html><head><meta charset='utf-8'><title>ToneDeck</title></head>"
          "<body style='background:#0d1424;color:#e9f1ff;font-family:sans-serif;text-align:center;padding-top:15vh'>"
          "<h1 style='color:#34d1ff'>ToneDeck</h1><p>Listo. Ya podes volver al plugin.</p>"
          "<p style='color:#5d6b82'>Done - you can close this tab.</p></body></html>"
        : "<html><head><meta charset='utf-8'><title>ToneDeck</title></head>"
          "<body style='background:#0d1424;color:#e9f1ff;font-family:sans-serif;text-align:center;padding-top:15vh'>"
          "<h1 style='color:#ff6b6b'>ToneDeck</h1><p>No se completo la seleccion. Podes cerrar esta pestana.</p></body></html>";
}
} // namespace

//==============================================================================
Tone3000Client::Tone3000Client (juce::PropertiesFile& settings, std::function<juce::File()> libraryFolder)
    : props (settings), getLibrary (std::move (libraryFolder))
{
    tokens.access      = props.getValue ("t3k_access");
    tokens.refresh     = props.getValue ("t3k_refresh");
    tokens.expiresAtMs = props.getValue ("t3k_expires", "0").getLargeIntValue();
}

Tone3000Client::~Tone3000Client()
{
    alive->store (false);
    pool.removeAllJobs (true, 25000);
}

juce::String Tone3000Client::getClientId() const
{
    auto id = props.getValue ("t3k_client_id").trim();
    return id.isNotEmpty() ? id : juce::String (TONEDECK_T3K_CLIENT_ID);
}

void Tone3000Client::setClientId (const juce::String& id)
{
    props.setValue ("t3k_client_id", id.trim());
    props.saveIfNeeded();
}

bool Tone3000Client::isSignedIn() const
{
    const juce::ScopedLock sl (tokenLock);
    return tokens.refresh.isNotEmpty() || tokens.access.isNotEmpty();
}

juce::String Tone3000Client::getUsername() const { return props.getValue ("t3k_user"); }

void Tone3000Client::signOut()
{
    {
        const juce::ScopedLock sl (tokenLock);
        tokens = {};
    }
    for (auto* k : { "t3k_access", "t3k_refresh", "t3k_expires", "t3k_user" })
        props.removeValue (k);
    props.saveIfNeeded();
}

void Tone3000Client::status (const juce::String& msg)
{
    auto flag = alive;
    juce::MessageManager::callAsync ([this, flag, msg]
    {
        if (flag->load() && onStatus)
            onStatus (msg);
    });
}

template <typename Fn>
void Tone3000Client::runAsync (Fn&& fn)
{
    pool.addJob (std::forward<Fn> (fn));
}

//==============================================================================
juce::String Tone3000Client::makeCodeChallenge (const juce::String& verifier)
{
    const auto utf8 = verifier.toUTF8();
    juce::SHA256 sha (utf8.getAddress(), utf8.sizeInBytes() - 1);
    const auto raw = sha.getRawData();
    return base64Url (raw.getData(), raw.getSize());
}

juce::StringPairArray Tone3000Client::parseQuery (const juce::String& query)
{
    juce::StringPairArray out;
    for (const auto& part : juce::StringArray::fromTokens (query, "&", ""))
    {
        const auto key = juce::URL::removeEscapeChars (part.upToFirstOccurrenceOf ("=", false, false).replace ("+", " "));
        const auto val = juce::URL::removeEscapeChars (part.fromFirstOccurrenceOf ("=", false, false).replace ("+", " "));
        if (key.isNotEmpty())
            out.set (key, val);
    }
    return out;
}

//==============================================================================
bool Tone3000Client::runBrowserAuth (const juce::String& prompt, juce::String& toneIdOut, juce::String& error)
{
    const auto clientId = getClientId();
    if (clientId.isEmpty())
    {
        error = "Falta la clave publica de TONE3000";
        return false;
    }

    juce::StreamingSocket listener;
    int port = 0;
    for (int p = kFirstPort; p < kFirstPort + kNumPorts; ++p)
        if (listener.createListener (p, "127.0.0.1")) { port = p; break; }
    if (port == 0)
    {
        error = "No se pudo abrir el puerto local para el login";
        return false;
    }

    const auto verifier = randomToken (48);
    const auto state    = randomToken (24);
    const auto redirect = "http://127.0.0.1:" + juce::String (port) + "/callback";

    auto url = juce::URL (apiBase + "/oauth/authorize")
                   .withParameter ("client_id", clientId)
                   .withParameter ("redirect_uri", redirect)
                   .withParameter ("response_type", "code")
                   .withParameter ("code_challenge", makeCodeChallenge (verifier))
                   .withParameter ("code_challenge_method", "S256")
                   .withParameter ("state", state);
    if (prompt.isNotEmpty())
        url = url.withParameter ("prompt", prompt);

    status (prompt == "select_tone" ? "TONE3000: elegi un tono en el navegador..." : "TONE3000: inicia sesion en el navegador...");
    if (! url.launchInDefaultBrowser())
    {
        error = "No se pudo abrir el navegador";
        return false;
    }

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) kBrowserWaitMs;
    while (alive->load() && juce::Time::getMillisecondCounter() < deadline)
    {
        if (listener.waitUntilReady (true, 500) != 1)
            continue;

        std::unique_ptr<juce::StreamingSocket> conn (listener.waitForNextConnection());
        if (conn == nullptr)
            continue;

        // read the request head
        juce::MemoryBlock req;
        char buf[2048];
        while (req.getSize() < 16384 && conn->waitUntilReady (true, 3000) == 1)
        {
            const int n = conn->read (buf, (int) sizeof (buf), false);
            if (n <= 0) break;
            req.append (buf, (size_t) n);
            if (req.toString().contains ("\r\n\r\n")) break;
        }

        const auto head = req.toString();
        const auto target = head.upToFirstOccurrenceOf ("\r\n", false, false)
                                .fromFirstOccurrenceOf (" ", false, false)
                                .upToFirstOccurrenceOf (" ", false, false);

        if (! target.startsWith ("/callback"))
        {
            const juce::String notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            conn->write (notFound.toRawUTF8(), (int) notFound.getNumBytesAsUTF8());
            continue;   // e.g. /favicon.ico
        }

        const auto q = parseQuery (target.fromFirstOccurrenceOf ("?", false, false));
        const bool stateOk = q["state"] == state;
        const auto code = q["code"];
        const bool ok = stateOk && code.isNotEmpty() && q["error"].isEmpty();

        const juce::String body = callbackPage (ok && q["canceled"] != "true");
        const juce::String resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
                                  + juce::String ((int) body.getNumBytesAsUTF8()) + "\r\nConnection: close\r\n\r\n" + body;
        conn->write (resp.toRawUTF8(), (int) resp.getNumBytesAsUTF8());
        conn->close();

        if (! stateOk)
        {
            error = "Respuesta de login invalida (state)";
            return false;
        }
        if (q["error"].isNotEmpty())
        {
            error = "TONE3000: " + q["error"];
            return false;
        }
        if (code.isNotEmpty() && ! exchangeCode (code, verifier, redirect, error))
            return false;
        if (code.isNotEmpty())
            fetchUsername();

        toneIdOut = q["tone_id"];
        if (q["canceled"] == "true")
        {
            error = "Seleccion cancelada";
            return false;
        }
        return code.isNotEmpty();
    }

    error = alive->load() ? "Se agoto el tiempo de espera del navegador" : "Cancelado";
    return false;
}

void Tone3000Client::storeTokens (const juce::var& json)
{
    const juce::ScopedLock sl (tokenLock);
    tokens.access = json["access_token"].toString();
    if (json["refresh_token"].toString().isNotEmpty())
        tokens.refresh = json["refresh_token"].toString();
    const int expires = (int) json.getProperty ("expires_in", 3600);
    tokens.expiresAtMs = juce::Time::currentTimeMillis() + (juce::int64) expires * 1000;

    props.setValue ("t3k_access", tokens.access);
    props.setValue ("t3k_refresh", tokens.refresh);
    props.setValue ("t3k_expires", juce::String (tokens.expiresAtMs));
    props.saveIfNeeded();
}

static juce::var postForm (const juce::String& url, const juce::StringPairArray& fields, int& statusCode)
{
    statusCode = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                    .withExtraHeaders ("Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json")
                    .withConnectionTimeoutMs (kHttpTimeoutMs)
                    .withStatusCode (&statusCode);
    if (auto stream = juce::URL (url).withPOSTData (formEncode (fields)).createInputStream (opts))
        return juce::JSON::parse (stream->readEntireStreamAsString());
    return {};
}

bool Tone3000Client::exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirect, juce::String& error)
{
    status ("TONE3000: conectando...");
    juce::StringPairArray f;
    f.set ("grant_type", "authorization_code");
    f.set ("code", code);
    f.set ("code_verifier", verifier);
    f.set ("redirect_uri", redirect);
    f.set ("client_id", getClientId());
    int sc = 0;
    const auto json = postForm (apiBase + "/oauth/token", f, sc);
    if (sc != 200 || json["access_token"].toString().isEmpty())
    {
        error = "No se pudo iniciar sesion (" + juce::String (sc) + ")";
        return false;
    }
    storeTokens (json);
    return true;
}

bool Tone3000Client::refreshToken (juce::String& error)
{
    juce::String refresh;
    {
        const juce::ScopedLock sl (tokenLock);
        refresh = tokens.refresh;
    }
    if (refresh.isEmpty())
    {
        error = "Sesion vencida";
        return false;
    }
    juce::StringPairArray f;
    f.set ("grant_type", "refresh_token");
    f.set ("refresh_token", refresh);
    f.set ("client_id", getClientId());
    int sc = 0;
    const auto json = postForm (apiBase + "/oauth/token", f, sc);
    if (sc == 200 && json["access_token"].toString().isNotEmpty())
    {
        storeTokens (json);
        return true;
    }
    if (sc == 400 || sc == 401)
        signOut();   // per TONE3000 guidance: clear and reconnect
    error = "Sesion vencida, volve a iniciar sesion";
    return false;
}

bool Tone3000Client::ensureToken (juce::String& error)
{
    bool needRefresh = false, haveAny = false;
    {
        const juce::ScopedLock sl (tokenLock);
        haveAny = tokens.access.isNotEmpty() || tokens.refresh.isNotEmpty();
        needRefresh = tokens.access.isEmpty() || juce::Time::currentTimeMillis() > tokens.expiresAtMs - 60000;
    }
    if (! haveAny)
    {
        juce::String unusedTone;
        return runBrowserAuth ({}, unusedTone, error);
    }
    if (needRefresh && ! refreshToken (error))
    {
        juce::String unusedTone;
        error.clear();
        return runBrowserAuth ({}, unusedTone, error);
    }
    return true;
}

juce::var Tone3000Client::apiGet (const juce::String& pathAndQuery, juce::String& error)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        juce::String access;
        {
            const juce::ScopedLock sl (tokenLock);
            access = tokens.access;
        }
        int sc = 0;
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withExtraHeaders ("Authorization: Bearer " + access + "\r\nAccept: application/json")
                        .withConnectionTimeoutMs (kHttpTimeoutMs)
                        .withStatusCode (&sc);
        auto stream = juce::URL (apiBase + pathAndQuery).createInputStream (opts);
        const auto text = stream != nullptr ? stream->readEntireStreamAsString() : juce::String();

        if (sc == 401 && attempt == 0)   // retry once after refreshing
        {
            if (! refreshToken (error))
                return {};
            continue;
        }
        if (sc == 429) { error = "TONE3000: demasiadas solicitudes, proba en un minuto"; return {}; }
        if (sc < 200 || sc >= 300)
        {
            const auto where = pathAndQuery.upToFirstOccurrenceOf ("?", false, false);
            error = sc == 0 ? "TONE3000: sin conexion (" + where + ")"
                            : "TONE3000 respondio " + juce::String (sc) + " en " + where
                                  + (sc == 403 ? " (tu clave no tiene permiso para esto)" : sc == 404 ? " (no encontrado)" : "");
            return {};
        }
        return juce::JSON::parse (text);
    }
    return {};
}

void Tone3000Client::fetchUsername()
{
    juce::String err;
    const auto user = apiGet ("/user", err);
    const auto name = user["username"].toString();
    if (name.isNotEmpty())
    {
        props.setValue ("t3k_user", name);
        props.saveIfNeeded();
    }
}

juce::Array<Tone3000Client::DownloadedModel> Tone3000Client::downloadModels (const juce::String& toneId, juce::String& error)
{
    juce::Array<DownloadedModel> out;
    status ("TONE3000: buscando modelos...");

    // Tone title is only cosmetic: don't fail the download if this lookup is refused.
    juce::String titleError;
    const auto tone = apiGet ("/tones/" + juce::URL::addEscapeChars (toneId, true), titleError);
    const auto title = tone["title"].toString().isNotEmpty() ? tone["title"].toString() : "tone-" + toneId;
    const auto gear  = tone["gear"].toString();

    const auto list = apiGet ("/models?tone_id=" + juce::URL::addEscapeChars (toneId, true) + "&page_size=300", error);
    if (error.isNotEmpty()) return out;
    const auto* models = list["data"].getArray();
    if (models == nullptr || models->isEmpty())
    {
        error = "TONE3000: el tono " + toneId + " no tiene modelos descargables";
        return out;
    }

    const auto dir = getLibrary().getChildFile ("TONE3000").getChildFile (safeName (title));
    dir.createDirectory();

    int index = 0, lastStatus = 0;
    for (const auto& m : *models)
    {
        ++index;
        const auto modelUrl = m["model_url"].toString();
        if (modelUrl.isEmpty()) continue;

        // Like the official client: keep the path, always send it to the API origin with the
        // Bearer token (a cross-host redirect would drop the Authorization header).
        const juce::URL parsed (modelUrl);
        const auto subPath = parsed.getSubPath();
        const auto fileName = subPath.fromLastOccurrenceOf ("/", false, false);
        auto ext = fileName.contains (".") ? "." + fileName.fromLastOccurrenceOf (".", false, false).toLowerCase() : juce::String();
        auto name = m["name"].toString();
        if (name.isEmpty()) name = title + " " + juce::String (index);
        const auto id = m["id"].toString();

        const juce::String query = modelUrl.containsChar ('?') ? "?" + modelUrl.fromFirstOccurrenceOf ("?", false, false) : juce::String();
        const auto sameOrigin = modelUrl.startsWithIgnoreCase ("https://www.tone3000.com") || modelUrl.startsWithIgnoreCase ("https://tone3000.com")
                                || modelUrl.startsWith ("/");
        const auto requestUrl = sameOrigin ? apiOrigin + "/" + subPath + query : modelUrl;

        status ("TONE3000: descargando " + name + " (" + juce::String (index) + "/" + juce::String (models->size()) + ")");
        juce::MemoryBlock data;
        for (int attempt = 0; attempt < 3 && data.isEmpty(); ++attempt)
        {
            juce::String access;
            {
                const juce::ScopedLock sl (tokenLock);
                access = tokens.access;
            }
            const bool withAuth = sameOrigin || attempt == 0;
            int sc = 0;
            auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                            .withExtraHeaders (withAuth ? "Authorization: Bearer " + access : juce::String())
                            .withConnectionTimeoutMs (kHttpTimeoutMs)
                            .withNumRedirectsToFollow (5)
                            .withStatusCode (&sc);
            if (auto stream = juce::URL (attempt == 2 ? modelUrl : requestUrl).createInputStream (opts))
                if (sc >= 200 && sc < 300)
                    stream->readIntoMemoryBlock (data);
            lastStatus = sc;
            if (sc == 401 && attempt == 0) { juce::String e; refreshToken (e); }
        }
        if (data.isEmpty()) continue;

        if (ext.isEmpty() || ext.length() > 6)   // sniff: RIFF = wav, JSON = model
        {
            const auto head = juce::String::fromUTF8 ((const char*) data.getData(), (int) juce::jmin ((size_t) 4096, data.getSize()));
            ext = head.startsWith ("RIFF") ? ".wav" : (head.contains ("state_dict") || head.contains ("\"in_shape\"") ? ".json" : ".nam");
        }
        const auto target = dir.getChildFile ((id.isNotEmpty() ? id + "-" : juce::String()) + safeName (name) + ext);
        if (target.replaceWithData (data.getData(), data.getSize()))
            out.add ({ target, title, name, gear });
    }

    if (out.isEmpty())
        error = "TONE3000: no se pudo descargar el modelo (HTTP " + juce::String (lastStatus) + ")";
    return out;
}

//==============================================================================
void Tone3000Client::selectTone (ModelsCallback done)
{
    if (busy.exchange (true))
    {
        done ({}, "Ya hay una operacion de TONE3000 en curso");
        return;
    }
    auto flag = alive;
    runAsync ([this, flag, done]
    {
        juce::String error, toneId;
        juce::Array<DownloadedModel> result;
        if (runBrowserAuth ("select_tone", toneId, error))
        {
            if (toneId.isEmpty())
                error = "No se eligio ningun tono";
            else
                result = downloadModels (toneId, error);
        }
        busy = false;
        juce::MessageManager::callAsync ([flag, done, result, error] { if (flag->load()) done (result, error); });
    });
}

void Tone3000Client::fetchTone (const juce::String& toneId, ModelsCallback done)
{
    if (busy.exchange (true))
    {
        done ({}, "Ya hay una operacion de TONE3000 en curso");
        return;
    }
    auto flag = alive;
    runAsync ([this, flag, done, toneId]
    {
        juce::String error;
        juce::Array<DownloadedModel> result;
        if (ensureToken (error))
            result = downloadModels (toneId, error);
        busy = false;
        juce::MessageManager::callAsync ([flag, done, result, error] { if (flag->load()) done (result, error); });
    });
}

void Tone3000Client::fetchList (const juce::String& listName, ListCallback done)
{
    if (busy.exchange (true))
    {
        done ({}, "Ya hay una operacion de TONE3000 en curso");
        return;
    }
    auto flag = alive;
    runAsync ([this, flag, done, listName]
    {
        juce::String error;
        juce::Array<ToneItem> items;
        if (ensureToken (error))
        {
            status ("TONE3000: cargando lista...");
            const bool paged = listName != "trending" && listName != "latest";
            const auto json = apiGet ("/tones/" + listName + (paged ? "?page_size=100" : ""), error);
            if (const auto* arr = json["data"].getArray())
            {
                for (const auto& t : *arr)
                {
                    ToneItem it;
                    it.id     = t["id"].toString();
                    it.title  = t["title"].toString();
                    it.gear   = t["gear"].toString();
                    it.format = t["format"].toString();
                    if (auto* user = t["user"].getDynamicObject())
                        it.author = user->getProperty ("username").toString();
                    if (it.id.isNotEmpty())
                        items.add (it);
                }
            }
        }
        busy = false;
        juce::MessageManager::callAsync ([flag, done, items, error] { if (flag->load()) done (items, error); });
    });
}
