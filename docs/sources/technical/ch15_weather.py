from build import arrow, box, c, fig, note, p, rif, table, text, ui, ul, warn

S1 = p("The PC side of the Weather widget is the only place where the YASDE program on the PC opens a connection to the Internet; everything else on the PC stays on the local network with the phone. (The phone itself may use a network service when its own geocoder names a place, see below.) The piece is "
       + c("src/meteo/") + " (contract " + c("meteo.h") + "), and the rules below are written in its header and in " + c("src/shell/CONTRATTI.md") + ".", lead=True) + ul([
    "<b>When.</b> Only while at least one Weather widget is on the desktop (" + c("ph_srv_meteo_osserva") + "). With no such widget no connection is started at all, not even for the place search. "
    "There is no separate consent dialog: adding the widget is the choice.",
    "<b>Whom.</b> " + c("api.open-meteo.com") + " for forecasts and " + c("geocoding-api.open-meteo.com") + " for the search of a place written by the user. Both are served over HTTPS on port 443.",
    "<b>What leaves the PC.</b> The latitude and longitude of the phone rounded to 0.005° (about 500 m), or those of the city the user chose by hand, or the text the user typed in the place search together with " + c("language=en") + " (the language of the shell), "
    "plus the fixed parameters of the request. No identifier, no cookie, a generic " + c("User-Agent") + ": " + c("YASDE/2.1") + ". The servers see the PC's IP address, as with any connection.",
    "<b>Where the rounding happens.</b> On the phone, before anything else: the precise position never leaves the phone. The place name is produced by Android's geocoder on the phone, from the already rounded coordinates; the geocoder may itself use a network service of the phone. On the wire and in " + c("PhDatiPosizione") + " and " + c("ph_meteo_chiedi") + " coordinates "
    "are in thousandths of a degree, always multiples of 5; the city chosen by hand stays in hundredths in " + c("PhMeteoLuogo") + " and in the profile, and is passed to " + c("ph_meteo_chiedi") + " multiplied by 10. "
    "The shell never receives the phone's coordinates: only the forecast and, if the phone knows it, the place name.",
    "<b>Nothing on disk, nothing in logs.</b> The phone's position and place name never go to disk or to any log, on the phone or on the PC, not even in error messages. The one exception on disk is the city chosen by hand "
    "(name, detail and coordinates rounded to 2 decimals in the profile), because it is the user's own choice; not even that, nor the searched text, goes into a log. An error says only its kind (service not responding, "
    "invalid answer, invalid certificate).",
    "<b>Credit.</b> The line " + ui("Weather data by Open-Meteo.com") + " with a link to " + c("https://open-meteo.com/") + " is always visible in the widget, in every size and style. Under the place search list, always: "
    + ui("Places from GeoNames, via Open-Meteo.com") + " linking to " + c("https://www.geonames.org/") + ". The data are under CC BY 4.0; the service is used under Open-Meteo's non-commercial terms.",
])

CHAIN = fig(
    box(20, 40, 130, 54, "Weather widget", "desktop piece", "navy")
    + box(180, 40, 160, 54, "ph_srv_meteo_osserva", "services (GTK thread)", "blue")
    + box(370, 40, 170, 54, "Backend (sv_meteo.c)", "ph_dati_posizione 0x90", "blue")
    + box(570, 40, 140, 54, "ph_meteo_chiedi", "worker of src/meteo", "dark")
    + box(740, 40, 140, 54, "Open-Meteo", "HTTPS GET", "dark")
    + arrow(152, 67, 178, 67) + arrow(342, 67, 368, 67) + arrow(542, 67, 568, 67) + arrow(712, 67, 738, 67)
    + box(180, 130, 160, 54, "PH_SRV_EV_METEO", "ph_servizi_pubblica_meteo_*", "green")
    + arrow(640, 96, 640, 112, dash=True) + arrow(640, 112, 342, 150)
    + arrow(178, 157, 152, 157) + box(20, 130, 130, 54, "Widget redraws", "forecast + attribution", "navy"),
    900, 200, "«FIG» — The chain from the widget to the forecast and back")

S2 = p("The chain is: widget, " + c("ph_srv_meteo_osserva") + ", the real backend (" + c("src/shell/ossatura/sv_meteo.c") + "), which asks the phone for the position (" + c("ph_dati_posizione") + ", message 0x90) and then calls "
       + c("ph_meteo_chiedi") + "; the answer is published with " + c("ph_servizi_pubblica_meteo") + " and " + c("ph_servizi_pubblica_meteo_fonte") + " and reaches the widget as " + c("PH_SRV_EV_METEO") + ".", lead=True) + CHAIN + ul([
    "<b>Rhythm.</b> The backend asks again about every 5 minutes while the widget is there; the piece answers from its cache when a valid answer for the same position is younger than " + c("PH_METEO_INTERVALLO_S") + " (30 minutes), "
    "so in practice the network is used at most once every 30 minutes per position. The cache lives in memory only and holds the last 4 positions.",
    "<b>After a failure</b> a new connection for the same position is not started before 5 minutes, then 15, then 30; meanwhile the caller gets the previous error at once, with no connection. A success resets the count. "
    "Every started connection counts as an attempt, even if interrupted. Between the start of any two connections at least " + c("PH_METEO_AVVII_MIN_MS") + " (5000 ms) pass. No bursts.",
    "<b>The piece does not retry by itself:</b> whoever uses it calls " + c("ph_meteo_chiedi") + " again, and the rules above decide whether a connection is needed.",
    "<b>Position from the phone.</b> The phone answers at once (tens of milliseconds) and never waits for a new position or for its geocoder inside the request. If it starts a background search "
    "(" + c("ricerca_avviata") + "), the backend asks once more after about 15 s without a new permission. The permission to start such a search is given at most once every 6 hours (" + c("sv_prova_meteo_permesso_s") + ").",
    "<b>Outcomes of the position:</b> known, turned off (" + ui("The phone's location is turned off.") + "), denied (" + ui("YASDE is not allowed to use the phone's location.") + "), unknown ("
    + ui("The phone does not know its location yet.") + "). Without a connected phone and without a chosen city the widget says " + ui("No phone connected: without a location there is no forecast.") + ".",
    "<b>The city written by hand</b> (menu of the widget, " + ui("Location…") + "): a dialog with a text field, a " + ui("Search") + " button and the results list, and " + ui("None") + " to remove the choice. "
    "The search starts only with the button or Enter, never at each key. One city per profile (per phone). <b>Precedence:</b> the phone's position always wins; the chosen city is used only when the phone gives no position "
    "(off, denied, none known, phone not connected). " + c("ph_srv_meteo_fonte") + " says where the data come from.",
    "<b>Place name in the widget:</b> from the phone's geocoder when the position is the phone's, from the chosen result when the city is manual. The title is " + ui("Weather — {localita}") + ", or just " + ui("Weather") + " without a name.",
    "<b>Unavailable</b> service: the widget says " + ui("Weather forecast not available right now.") + " with the time of the last data if there is one.",
    "<b>Styles.</b> The widget has seven drawings (now, hours, week, compact, today, curve, no background; the code names are " + c("PH_DSK_METEO_*") + "), chosen from its menu and remembered in the profile.",
])

S3 = p("The piece is written from scratch on top of OpenSSL (already a dependency for ADB): no new library.", lead=True) + table(["Part", "File", "Behavior"], [
    ["HTTPS", c("src/meteo/tls.c"), "TCP socket, SNI (" + c("SSL_set_tlsext_host_name") + "), " + c("SSL_set1_host") + " with " + c("SSL_VERIFY_PEER") + ": chain and host name verified, an invalid certificate is an error and never ignored. "
     "Name resolution with " + c("getaddrinfo_a") + " (cancellable, with a deadline), non-blocking connect with " + c("poll") + "."],
    ["Certificate authorities", c("src/meteo/tls.c"), "The system's: OpenSSL default paths and, if they are not enough, the first existing of " + c("/etc/ssl/certs/ca-certificates.crt") + ", " + c("/etc/pki/tls/certs/ca-bundle.crt")
     + ", " + c("/etc/ssl/ca-bundle.pem"), ],
    ["HTTP/1.1", c("src/meteo/http.c"), c("GET <path> HTTP/1.1") + ", " + c("Host") + ", " + c("User-Agent: YASDE/2.1") + ", " + c("Accept: application/json") + ", " + c("Connection: close") + ". The status line must be 200; headers up to 16 KiB; the body with "
     + c("Content-Length") + " or " + c("Transfer-Encoding: chunked") + " (chunks checked; both together is refused as ambiguous). No redirects, no compression, no connection reuse."],
    ["JSON", c("src/meteo/analisi.c"), c("ph_meteo_analizza") + ": a streaming reader with limited depth (16), checked lengths, numbers read independent of the system language. It takes only " + c("utc_offset_seconds") + ", " + c("current")
     + ", " + c("hourly") + " and " + c("daily") + "; a missing, " + c("null") + " or wrong-typed field means \"not known\", not an error; hours from now on, at most " + c("PH_METEO_ORE_MAX") + " (48); days at most " + c("PH_METEO_GIORNI_MAX") + " (7)."],
    ["Request path", c("src/meteo/meteo.c"), c("ph_meteo_percorso") + ": coordinates in thousandths, limits ±90000 and ±180000 and multiples of 5 (otherwise -1), written with three decimals and always the dot (never printf with a float, so the system language "
     "cannot change it): " + c("41895") + " becomes " + c("41.895") + ", " + c("-5") + " becomes " + c("-0.005") + ". Fixed parameters: current, hourly, daily fields, " + c("timezone=auto") + ", " + c("timeformat=unixtime") + ", " + c("forecast_days=7") + "."],
    ["Place search", c("src/meteo/meteo.c"), c("ph_meteo_cerca") + ": " + c("GET /v1/search?name=&count=&language=&format=json") + ", at most " + c("PH_METEO_LUOGHI_MAX") + " (10) results, text of at most " + c("PH_METEO_RICERCA_TESTO_MAX") + " (64) bytes, "
     "answer at most " + c("PH_METEO_RICERCA_RISPOSTA_MAX") + " (64 KiB), 2 s between searches, at most 10 searches per 10 minutes, the same failed search not before 30 s. Results are rounded to hundredths."],
], "«TAB» — The parts of the weather piece") + ul([
    "<b>Limits.</b> Whole request " + c("PH_METEO_LIMITE_S") + " (15 s), answer at most " + c("PH_METEO_RISPOSTA_MAX") + " (512 KiB). Cancel stops the request within 1 s. No lock is held during network work.",
    "<b>Network data is hostile.</b> The analyzer never reads or writes outside its buffers.",
    "<b>Callbacks.</b> The result function is called from the piece's own thread; inside it only " + c("ph_meteo_chiedi") + " may be called. " + c("ph_meteo_chiudi") + " called inside is refused (nothing happens). "
    "After " + c("ph_meteo_annulla") + " returns no callback arrives any more.",
])

S4 = p("Tests run without Internet (" + c("src/meteo/prova/") + ", " + c("strumenti/prova.sh meteo") + "):", lead=True) + ul([
    "Pure parts: the request path (including with an Italian locale active: the decimal point must stay), the analysis of a saved answer with fake values, missing fields, " + c("null") + ", wrong types, arrays of different "
    "lengths, truncated or too deeply nested or huge JSON, absurd numbers.",
    "A fake local server (a thread listening on 127.0.0.1, TLS with a test certificate generated in the test and a test authority passed to the piece only in test mode) answers with a length, chunked, slowly "
    "(deadline), too large, status 500, and a certificate with the wrong name (must be refused). " + c("src/meteo/prova/tls_finto.c") + " is the fake TLS side.",
    "Cache and frequency with a fake internal clock; cancel and close during a slow request (AddressSanitizer clean, no callback after return). The thread-sanitizer variant is built separately, because the two sanitizers do not mix.",
    "The real backend's weather side is tested over fake lower pieces: " + c("src/shell/ossatura/prova_meteo_veri.c") + ".",
    "The TLS handshake to the real server was tried by hand from the AppImage (handshake and certificate check without sending anything); no real forecast request has been tried yet.",
])

CHAPTER = ("Weather", [
    ("What goes out and when", S1),
    ("From the widget to the forecast", S2),
    ("The weather piece", S3),
    ("How it is tested", S4),
])
