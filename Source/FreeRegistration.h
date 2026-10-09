#pragma once

#include <JuceHeader.h>

/**
    FreeRegistration
    ----------------
    Suppressor est GRATUIT : pas de cle de licence. A la place, l'utilisateur
    indique UNE FOIS son nom + son email (voir RegistrationOverlay) -- c'est
    ce qui "active" le plugin. Cette classe :
      - memorise l'inscription sur la machine (ProgramData, repli AppData) ;
      - l envoie au serveur SideKey (license.sidekey.fr, /v1/register-free) :
        l installation devient une ligne de la liste PRINCIPALE des licences de
        /admin (nom, email, machine, derniere utilisation), sans cle a saisir
        (repli sur /v1/trial-ping si le serveur n est pas encore a jour) ;
      - rapporte ensuite le temps d'utilisation (/v1/usage-ping, toutes les
        5 min) pour la colonne "Temps utilise".

    Une seule instance par processus (juce::SharedResourcePointer) : 20
    Suppressor dans un projet = 1 seul thread, 1 seul envoi.

    SECURITE HOTE : le thread attend 20 s avant de toucher au reseau, et il est
    arrete (stopThread) a la destruction -- un scan de plugins, qui cree puis
    detruit l'instance en moins d'une seconde, ne lance donc JAMAIS de requete,
    et aucun thread ne survit au dechargement de la DLL. Le serveur n'est
    jamais bloquant : la reponse n'est pas utilisee (pas de revocation).
*/
class FreeRegistration : private juce::Thread
{
public:
    FreeRegistration() : juce::Thread ("Suppressor registration")
    {
        machineId = juce::SystemStats::getUniqueDeviceID();
        load();
        startThread();
    }

    ~FreeRegistration() override
    {
        // stopThread() signale + reveille le thread (wait()) : retour immediat
        // sauf si une requete reseau est en cours (timeout de connexion 5 s,
        // donc borne a 6 s ici).
        stopThread (6000);
    }

    bool isRegistered() const noexcept { return registered.load(); }

    // Appelee par RegistrationOverlay, arguments deja valides.
    void registerUser (const juce::String& name, const juce::String& email)
    {
        {
            const juce::ScopedLock sl (lock);
            userName  = name.trim();
            userEmail = email.trim();
        }

        save();
        registered = true;
        notify(); // reveille le thread : envoi immediat plutot qu'apres 20 s
    }

private:
    static constexpr const char* productId     = "suppressor"; // voir GUMROAD_PRODUCT_MAP cote Worker
    static constexpr const char* serverBaseUrl = "https://license.sidekey.fr";
    static constexpr int usageIntervalMs       = 5 * 60 * 1000;

    // ----- thread : enregistrement puis temps d'utilisation -----
    void run() override
    {
        wait (20000); // jamais de reseau pour une instance ephemere (scan de plugins)

        while (! threadShouldExit() && ! registered.load())
            wait (5000);

        // Hors-ligne ou serveur indisponible : nouvel essai apres 30 s, 2 min,
        // puis toutes les 5 min (le motif de l echec est dans registration.log).
        for (int attempt = 0; ! threadShouldExit() && ! sendRegistration(); ++attempt)
            wait (attempt == 0 ? 30000 : attempt == 1 ? 120000 : usageIntervalMs);

        lastUsageMs = juce::Time::currentTimeMillis();

        while (! threadShouldExit())
        {
            wait (usageIntervalMs);

            if (threadShouldExit())
                break;

            sendUsage();
        }
    }

    bool sendRegistration()
    {
        juce::String name, email;
        {
            const juce::ScopedLock sl (lock);
            name = userName;
            email = userEmail;
        }

        auto makePayload = [&]
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("machine_id", machineId);
            o->setProperty ("product_id", productId);
            o->setProperty ("machine_name", juce::SystemStats::getComputerName());
            o->setProperty ("plugin_version", ProjectInfo::versionString);
            o->setProperty ("email", email);
            o->setProperty ("customer_name", name);
            return juce::var (o);
        };

        // L'inscription cree une vraie ligne dans la liste PRINCIPALE des licences
        // de /admin (nom, email, machine, derniere utilisation), sans cle a saisir.
        const auto reply = postJson ("/v1/register-free", makePayload());
        const bool ok = reply.isObject();

        if (ok)
            licenseKey = reply.getProperty ("license_key", "").toString();
        else
            postJson ("/v1/trial-ping", makePayload()); // serveur pas encore mis a jour : au moins visible dans "Essais"

        log (juce::String ("/v1/register-free inscription -> ") + (ok ? "ok" : "echec, nouvel essai plus tard"));
        return ok;
    }

    void sendUsage()
    {
        const auto now = juce::Time::currentTimeMillis();
        const int seconds = (int) ((now - lastUsageMs) / 1000);
        lastUsageMs = now;

        if (seconds <= 0)
            return;

        auto* o = new juce::DynamicObject();
        o->setProperty ("machine_id", machineId);
        o->setProperty ("product_id", productId);
        o->setProperty ("seconds", seconds);

        if (licenseKey.isNotEmpty())
            o->setProperty ("license_key", licenseKey); // met a jour le temps d'utilisation de la ligne licence
        postJson ("/v1/usage-ping", juce::var (o));
    }

    // Petit journal de diagnostic (registration.log, a cote de registration.json) :
    // uniquement les echecs reseau et les envois d inscription -- jamais de donnees
    // personnelles. Borne a 64 Ko.
    static void log (const juce::String& message)
    {
        auto file = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                        .getChildFile ("Side Key System").getChildFile ("Suppressor").getChildFile ("registration.log");
        file.getParentDirectory().createDirectory();

        if (file.getSize() > 64 * 1024)
            file.deleteFile();

        file.appendText (juce::Time::getCurrentTime().toISO8601 (true) + "  " + message + "\n");
    }

    // Renvoie la reponse JSON du serveur (objet) si elle est ok, sinon un var vide.
    static juce::var postJson (const juce::String& path, const juce::var& payload)
    {
        juce::URL url = juce::URL (juce::String (serverBaseUrl) + path)
                            .withPOSTData (juce::JSON::toString (payload, true));

        int statusCode = 0;
        auto stream = url.createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders ("Content-Type: application/json")
                .withConnectionTimeoutMs (5000)
                .withStatusCode (&statusCode));

        if (stream == nullptr)
        {
            log (path + " -> aucune connexion (HTTP " + juce::String (statusCode) + ")");
            return {};
        }

        const auto response = stream->readEntireStreamAsString();
        auto json = juce::JSON::parse (response);
        const bool ok = statusCode >= 200 && statusCode < 300 && static_cast<bool> (json.getProperty ("ok", false));

        if (! ok)
            log (path + " -> echec HTTP " + juce::String (statusCode) + " : " + response.substring (0, 200));

        return ok ? json : juce::var();
    }

    // ----- persistance locale -----
    static juce::Array<juce::File> candidateFiles()
    {
        juce::Array<juce::File> files;

        for (auto location : { juce::File::commonApplicationDataDirectory, juce::File::userApplicationDataDirectory })
            files.add (juce::File::getSpecialLocation (location)
                           .getChildFile ("Side Key System").getChildFile ("Suppressor")
                           .getChildFile ("registration.json"));

        return files;
    }

    void load()
    {
        for (auto& file : candidateFiles())
        {
            if (! file.existsAsFile())
                continue;

            auto json = juce::JSON::parse (file);
            auto name  = json.getProperty ("name", "").toString().trim();
            auto email = json.getProperty ("email", "").toString().trim();

            if (name.isNotEmpty() && email.isNotEmpty())
            {
                userName = name;
                userEmail = email;
                registered = true;
                return;
            }
        }
    }

    void save() const
    {
        auto* obj = new juce::DynamicObject();
        {
            const juce::ScopedLock sl (lock);
            obj->setProperty ("name", userName);
            obj->setProperty ("email", userEmail);
        }
        obj->setProperty ("registered_ms", juce::Time::currentTimeMillis());
        const auto text = juce::JSON::toString (juce::var (obj));

        for (auto& file : candidateFiles())
        {
            file.getParentDirectory().createDirectory();

            if (file.replaceWithText (text))
                return; // ProgramData d'abord ; repli AppData si non inscriptible
        }
    }

    juce::CriticalSection lock;
    juce::String machineId, userName, userEmail;
    juce::String licenseKey; // renvoyee par /v1/register-free, utilisee uniquement par le thread
    std::atomic<bool> registered { false };
    juce::int64 lastUsageMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreeRegistration)
};
