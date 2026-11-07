/*
  ECU-C / ESP32 - v0.8.8 - copie publique a personnaliser
  Reprise de v0.7.1 : Wi-Fi borne, Shelly local, profil monophase A/total.
  Selection MANUELLE 0073 du compteur RESEAU seulement ; PV non envoye.
  Precontrole 0072 des reglages releves + identite ECU 000000000000 / C1.2.27A.
  Ces commandes peuvent relancer main.exe dans le firmware C1.2.26 analyse.
  Retour manuel aux tores depuis la sauvegarde exacte du diagnostic v071.
  Journal NVS avant envoi : aucune retransmission automatique d'une selection.
  Si selection confirmee/incertaine : reprise Shelly/repondeur apres Wi-Fi/reset,
  avec mesure complete recente <15 s, jamais de zero de remplacement.
  Pas de modification 0075 des fonctions de regulation. Pas de retour CT automatique.
  Version utilisee pour les essais valides sur C1.2.27A ; revalider le nouveau site.
  Deux maisons monophasees ; seule la maison locale est integree a cette version.
  v0.8.2 : suivi de cadence, derniere requete et 32 derniers echanges.
  v0.8.3 : un nouvel essai TCP Shelly, et historique des erreurs conserve.
  v0.8.4 : ouverture/envoi Shelly non bloquants, TCP 5 s + HTTP 5 s.
  v0.8.8 : essai temporaire +1000 W dans les seules reponses au compteur.
  v0.8.7 : nom d'hote M01 + ID exact pour la selection APS de C1.2.27A.
  Releve complet toutes les 2 s, un seul RPC en vol ; pas de rejeu de mesure.
  Delais TCP, upgrade WS et RPC bornes ; fraicheur maximale toujours 15 s.
  Connexion persistante + keepalive TCP ; essai MANUEL de reconnexion, une fois
  par demarrage, apres 30 s sans octets et au moins une reponse complete.
  Aucun envoi spontane de mesure, aucune reselection automatique du compteur.
  Priorite au compteur autour du traitement Web ; fragments recus traites
  avant expiration du delai. Profil de mesure inchange ; cadence Shelly 2 s.
  Reglages Wi-Fi dans WifiLocal.h. Moniteur serie 115200 bauds.
*/
#include <Arduino.h>
#include <WiFi.h>
#include "CooperativeWebServer.h"
#include "ShellyWebSocket.h"
#include "Test1000.h"
#include <ESPmDNS.h>
#include <Preferences.h>
#include <mbedtls/aes.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_arduino_version.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <initializer_list>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

struct WifiTrace;
struct WifiRecoveryState;
struct ProfileStreamState; // Declaration avant les prototypes generes par Arduino.
struct MeterTcpEvent;
struct MeterReadCadence;
struct MeterExchange;
struct ShellyIoEvent;
void pauseShellyForEcu();
void stopLiveMeter(const char* reason);
struct MeterSourceSelection;
String shellyNumber(double value, unsigned decimals = 1);

// Reglages Wi-Fi generes par Personnaliser.py ; adapter aussi les identites et le reseau.
#include "WifiLocal.h"

// Port choisi pour le prototype, PAS un port impose par APsystems.
constexpr uint16_t METER_PORT = 9527;
constexpr uint32_t CLIENT_IDLE_MS = 5000;
// 0 = pas de fermeture pour simple silence de l'ECU en mode repondeur.
// Les gardes de fraicheur Shelly et de requete incomplete restent actives.
constexpr uint32_t LIVE_IDLE_TIMEOUT_MS = 0;
constexpr uint32_t PROFILE_PARTIAL_TIMEOUT_MS = 1500;

CooperativeWebServer web(80);
WiFiServer meterServer(METER_PORT);
WiFiClient meterClient;
constexpr size_t CAPTURE_CAPACITY = 512;
uint8_t captureData[CAPTURE_CAPACITY] = {};
size_t captureSize = 0;
bool captureTruncated = false;
String hostName, instanceName, meterId, lastPeer, decodedHex;
bool servicesStarted = false, mdnsReady = false;
bool decodeTried = false, expectedRead = false, selfTestOk = false;
bool benchActive = false;
bool liveEnabled = false, sourceJobActive = false;
Test1000 test1000;
bool switchJobActive = false, switchResumeHeld = false;
uint32_t connectionCount = 0, receivedBytes = 0, lastRxMs = 0;


// BEGIN WIFI_TRACE_HELPERS
struct WifiTrace {
  uint32_t disconnectEvents = 0, connectionLosses = 0, gotIpEvents = 0, lostIpEvents = 0;
  uint32_t lastDisconnectMs = 0, lastGotIpMs = 0;
  uint32_t stationStarts = 0, stationStops = 0, associationEvents = 0;
  bool associated = false;
  uint8_t lastReason = 0;
  bool hasIp = false;
};

void applyWifiEvent(WifiTrace& trace, int event, uint8_t reason, uint32_t now) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      ++trace.stationStarts; trace.associated = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      ++trace.associationEvents; trace.associated = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      ++trace.gotIpEvents; trace.hasIp = true; trace.associated = true; trace.lastGotIpMs = now;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      ++trace.disconnectEvents;
      if (trace.hasIp) ++trace.connectionLosses;
      trace.hasIp = false; trace.associated = false; trace.lastReason = reason; trace.lastDisconnectMs = now;
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      ++trace.lostIpEvents;
      if (trace.hasIp) ++trace.connectionLosses;
      trace.hasIp = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      ++trace.stationStops; trace.associated = false;
      if (trace.hasIp) ++trace.connectionLosses;
      trace.hasIp = false;
      break;
    default: break;
  }
}

const char* wifiReasonLabel(unsigned reason) {
  switch (reason) {
    case 0: return "aucun_evenement";
    case 2: return "AUTH_EXPIRE";
    case 8: return "ASSOC_LEAVE";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default: return "autre_code_voir_documentation_ESP32";
  }
}
// END WIFI_TRACE_HELPERS

// Le callback tourne dans une autre tache : verrou court, aucun acces aux sockets
// ni au serveur Web. Les appels reseau et les logs restent dans loop().
portMUX_TYPE wifiTraceMux = portMUX_INITIALIZER_UNLOCKED;
WifiTrace wifiTrace;
uint32_t wifiHandledLosses = 0;
uint32_t wifiPrintedDisconnects = 0, wifiPrintedGotIp = 0, wifiPrintedLostIp = 0;
uint32_t wifiLastSampleMs = 0, wifiLastLogMs = 0;
uint32_t wifiPrintedStarts = 0, wifiPrintedAssociations = 0;
int32_t wifiLastRssi = 0, wifiMinRssi = 0;
bool wifiHasRssi = false, wifiFirstLog = true;
int wifiResetReason = 0;

void recordWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  const uint32_t now = millis();
  const uint8_t reason = event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED
      ? info.wifi_sta_disconnected.reason : 0;
  portENTER_CRITICAL(&wifiTraceMux);
  applyWifiEvent(wifiTrace, int(event), reason, now);
  portEXIT_CRITICAL(&wifiTraceMux);
}

WifiTrace wifiSnapshot() {
  portENTER_CRITICAL(&wifiTraceMux);
  const WifiTrace snapshot = wifiTrace;
  portEXIT_CRITICAL(&wifiTraceMux);
  return snapshot;
}

const char* resetReasonLabel(int reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

// BEGIN WIFI_RECOVERY_HELPERS
enum { WIFI_CONFIG_ERROR, WIFI_CONNECTING, WIFI_ONLINE, WIFI_RETRY_WAIT, WIFI_STOP_RETRY };
constexpr uint32_t WIFI_ATTEMPT_TIMEOUT_MS = 45000;
constexpr uint32_t WIFI_STOP_RETRY_MS = 5000;

struct WifiRecoveryState {
  int phase = WIFI_CONFIG_ERROR, lastBeginStatus = -1;
  uint32_t sinceMs = 0, waitMs = 0, attempts = 0, radioStops = 0, failures = 0;
  bool lastStopOk = false, configured = false;
  const char* detail = "pas_initialise";
};

const char* validateWifiCredentials(const char* ssid, const char* password) {
  if (!ssid || !*ssid) return "ssid_vide";
  if (!strcmp(ssid, "TON_SSID_WIFI")) return "ssid_a_renseigner";
  if (strlen(ssid) > 32) return "ssid_plus_de_32_octets";
  if (!password || !*password) return "mot_de_passe_vide";
  if (!strcmp(password, "TON_MOT_DE_PASSE_WIFI")) return "mot_de_passe_a_renseigner";
  const size_t n = strlen(password);
  if (n < 8) return "mot_de_passe_moins_de_8_octets";
  if (n > 64) return "mot_de_passe_plus_de_64_octets";
  if (n == 64) for (size_t i = 0; i < n; ++i)
    if (!isxdigit(static_cast<unsigned char>(password[i]))) return "cle_64_octets_non_hexadecimale";
  return "ok";
}

uint32_t wifiRetryDelay(uint32_t failures) {
  if (failures <= 1) return 5000;
  if (failures == 2) return 10000;
  if (failures == 3) return 20000;
  return 30000;
}

const char* wifiPhaseLabel(int phase) {
  switch (phase) {
    case WIFI_CONFIG_ERROR: return "configuration_a_corriger";
    case WIFI_CONNECTING: return "tentative_en_cours";
    case WIFI_ONLINE: return "connecte_avec_ip";
    case WIFI_RETRY_WAIT: return "attente_avant_nouvel_essai";
    case WIFI_STOP_RETRY: return "arret_radio_a_reessayer";
    default: return "inconnu";
  }
}

const char* wifiStatusLabel(int status) {
  switch (status) {
    case WL_IDLE_STATUS: return "attente_connexion_ou_IP";
    case WL_NO_SSID_AVAIL: return "reseau_non_trouve";
    case WL_CONNECTED: return "connecte";
    case WL_CONNECT_FAILED: return "echec_connexion";
    case WL_CONNECTION_LOST: return "connexion_perdue";
    case WL_DISCONNECTED: return "deconnecte";
    case WL_NO_SHIELD: return "station_non_demarree";
    default: return "autre_statut";
  }
}
// END WIFI_RECOVERY_HELPERS

// BEGIN WIFI_RECOVERY_CORE
WifiRecoveryState wifiRecovery;

void stopWifiBeforeRetry(const char* detail, bool countFailure) {
  if (servicesStarted) endServices();
  if (countFailure && wifiRecovery.failures < 1000) ++wifiRecovery.failures;
  ++wifiRecovery.radioStops;
  wifiRecovery.detail = detail;
  // L'arret complet termine l'ancienne tentative. Aucun reconnect() parallele.
  const bool stopped = WiFi.mode(WIFI_OFF);
  wifiRecovery.lastStopOk = stopped && WiFi.getMode() == WIFI_OFF;
  wifiRecovery.sinceMs = millis();
  wifiRecovery.waitMs = wifiRecovery.lastStopOk ? wifiRetryDelay(wifiRecovery.failures) : WIFI_STOP_RETRY_MS;
  wifiRecovery.phase = wifiRecovery.lastStopOk ? WIFI_RETRY_WAIT : WIFI_STOP_RETRY;
  Serial.printf("[WiFi] %s ; arret radio=%s ; attente=%lu s\n", detail,
    wifiRecovery.lastStopOk ? "OK" : "ECHEC", (unsigned long)(wifiRecovery.waitMs/1000));
}

void initializeWifiRecovery() {
  WiFi.persistent(false); // N'ecrit pas les identifiants a chaque nouvelle tentative.
  WiFi.setAutoReconnect(false); // La politique ci-dessous gere les nouvelles tentatives.
  wifiRecovery = WifiRecoveryState{};
  wifiRecovery.detail = validateWifiCredentials(WIFI_SSID, WIFI_PASSWORD);
  if (strcmp(wifiRecovery.detail, "ok")) {
    Serial.printf("[WiFi] CONFIGURATION A CORRIGER : %s. Renseigner WIFI_SSID et WIFI_PASSWORD puis reteleverser.\n", wifiRecovery.detail);
    return;
  }
  wifiRecovery.configured = true;
  wifiRecovery.phase = WIFI_RETRY_WAIT;
  wifiRecovery.sinceMs = millis();
  Serial.printf("[WiFi] Configuration presente : SSID de %u octets ; mot de passe renseigne (non affiche).\n", unsigned(strlen(WIFI_SSID)));
}

void serviceWifiRecovery() {
  if (!wifiRecovery.configured) return;
  const uint32_t now = millis();
  const bool hasIp = WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
  if (hasIp) {
    if (wifiRecovery.phase != WIFI_ONLINE) {
      wifiRecovery.phase = WIFI_ONLINE; wifiRecovery.sinceMs = now;
      wifiRecovery.failures = 0; wifiRecovery.detail = "ip_obtenue";
      Serial.print("[WiFi] Connexion et DHCP reussis : http://"); Serial.println(WiFi.localIP());
    }
    return;
  }
  if (wifiRecovery.phase == WIFI_ONLINE) {
    stopWifiBeforeRetry("connexion_ou_ip_perdue", true); return;
  }
  if (wifiRecovery.phase == WIFI_CONNECTING) {
    if (uint32_t(now - wifiRecovery.sinceMs) >= WIFI_ATTEMPT_TIMEOUT_MS)
      stopWifiBeforeRetry("aucune_IP_apres_45_secondes", true);
    return;
  }
  if (uint32_t(now - wifiRecovery.sinceMs) < wifiRecovery.waitMs) return;
  if (wifiRecovery.phase == WIFI_STOP_RETRY) {
    stopWifiBeforeRetry("arret_radio_precedent_en_echec", false); return;
  }
  if (wifiRecovery.phase != WIFI_RETRY_WAIT) return;
  // Une radio encore active doit d'abord etre arretee avec succes.
  if (WiFi.getMode() != WIFI_OFF) {
    stopWifiBeforeRetry("radio_encore_active_avant_essai", false); return;
  }
  ++wifiRecovery.attempts;
  const bool hostnameOk = WiFi.setHostname(hostName.c_str());
  const bool modeOk = WiFi.mode(WIFI_STA);
  Serial.printf("[WiFi] Tentative %lu : hostname=%s, mode STA=%s\n", (unsigned long)wifiRecovery.attempts,
    hostnameOk ? "OK" : "ECHEC", modeOk ? "OK" : "ECHEC");
  if (!modeOk) { stopWifiBeforeRetry("demarrage_station_en_echec", true); return; }
  const bool sleepOk = WiFi.setSleep(false);
  wifiRecovery.lastBeginStatus = int(WiFi.begin(WIFI_SSID, WIFI_PASSWORD));
  wifiRecovery.phase = WIFI_CONNECTING; wifiRecovery.sinceMs = millis();
  wifiRecovery.detail = "attente_evenements_wifi_et_dhcp";
  Serial.printf("[WiFi] begin retourne %d (%s), economie radio desactivee=%s ; attente IP, maximum 45 s.\n",
    wifiRecovery.lastBeginStatus, wifiStatusLabel(wifiRecovery.lastBeginStatus), sleepOk ? "OK" : "ECHEC");
  // Un statut 6 au retour de begin est possible pendant le demarrage asynchrone.
  // Aucun second begin avant expiration, arret radio confirme et temporisation.
}
// END WIFI_RECOVERY_CORE


void serviceWifiDiagnostics() {
  const WifiTrace trace = wifiSnapshot();
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
  if (trace.stationStarts != wifiPrintedStarts) {
    wifiPrintedStarts = trace.stationStarts;
    Serial.printf("[WiFi] Station demarree, evenement n=%lu\n", (unsigned long)trace.stationStarts);
  }
  if (trace.associationEvents != wifiPrintedAssociations) {
    wifiPrintedAssociations = trace.associationEvents;
    Serial.printf("[WiFi] Association au point d'acces reussie, evenement n=%lu ; IP=%s\n",
      (unsigned long)trace.associationEvents, trace.hasIp ? "obtenue" : "en attente DHCP");
  }
  if (connected && (!wifiHasRssi || uint32_t(now - wifiLastSampleMs) >= 5000)) {
    wifiLastSampleMs = now;
    const int32_t rssi = WiFi.RSSI();
    if (rssi < 0 && rssi >= -127) {
      if (!wifiHasRssi || rssi < wifiMinRssi) wifiMinRssi = rssi;
      wifiLastRssi = rssi; wifiHasRssi = true;
    }
  }
  if (trace.disconnectEvents != wifiPrintedDisconnects) {
    wifiPrintedDisconnects = trace.disconnectEvents;
    Serial.printf("[WiFi] evenement deconnexion n=%lu, dernier a %lu ms, raison=%u (%s), pertes apres IP=%lu\n",
      (unsigned long)trace.disconnectEvents, (unsigned long)trace.lastDisconnectMs,
      unsigned(trace.lastReason), wifiReasonLabel(trace.lastReason), (unsigned long)trace.connectionLosses);
  }
  if (trace.lostIpEvents != wifiPrintedLostIp) {
    wifiPrintedLostIp = trace.lostIpEvents;
    Serial.printf("[WiFi] perte adresse IP, compteur=%lu\n", (unsigned long)trace.lostIpEvents);
  }
  const bool newIp = trace.gotIpEvents != wifiPrintedGotIp;
  wifiPrintedGotIp = trace.gotIpEvents;
  if (wifiFirstLog || newIp || uint32_t(now - wifiLastLogMs) >= 30000) {
    wifiFirstLog = false; wifiLastLogMs = now;
    Serial.printf("[WiFi] uptime=%lu s, statut=%d (%s), IP=%s, phase=%s, essais=%lu, pertes=%lu, heap=%lu\n",
      (unsigned long)(now/1000), int(WiFi.status()), wifiStatusLabel(int(WiFi.status())), WiFi.localIP().toString().c_str(),
      wifiPhaseLabel(wifiRecovery.phase), (unsigned long)wifiRecovery.attempts,
      (unsigned long)trace.connectionLosses, (unsigned long)ESP.getFreeHeap());
    if (connected && wifiHasRssi) Serial.printf("[WiFi] Canal=%ld, RSSI=%ld dBm\n", long(WiFi.channel()), long(wifiLastRssi));
    if (!connected) {
      Serial.printf("[WiFi] Detail=%s ; associations=%lu ; motifs recus=%lu ; dernier=%u (%s)\n",
        wifiRecovery.detail, (unsigned long)trace.associationEvents, (unsigned long)trace.disconnectEvents,
        unsigned(trace.lastReason), wifiReasonLabel(trace.lastReason));
      if (wifiRecovery.phase == WIFI_CONFIG_ERROR)
        Serial.println("[WiFi] Corriger les identifiants dans le sketch et reteleverser ; attendre ne suffit pas.");
    }
  }
}

String wifiJsonFields() {
  const WifiTrace trace = wifiSnapshot();
  String s = ",\"wifi_reconnect_policy\":\"bounded_attempt_radio_restart\",\"wifi_forced_reconnects\":0";
  s += ",\"wifi_arduino_version\":\"" + String(ESP_ARDUINO_VERSION_MAJOR) + "." + String(ESP_ARDUINO_VERSION_MINOR) + "." + String(ESP_ARDUINO_VERSION_PATCH) + "\"";
  s += ",\"wifi_auto_reconnect\":false";
  s += ",\"wifi_recovery_phase\":\"" + String(wifiPhaseLabel(wifiRecovery.phase)) + "\"";
  s += ",\"wifi_recovery_detail\":\"" + String(wifiRecovery.detail) + "\"";
  s += ",\"wifi_configuration_present\":"; s += wifiRecovery.configured ? "true" : "false";
  s += ",\"wifi_connection_attempts\":" + String(wifiRecovery.attempts);
  s += ",\"wifi_radio_stop_attempts\":" + String(wifiRecovery.radioStops);
  s += ",\"wifi_retry_wait_ms\":" + String(wifiRecovery.waitMs);
  s += ",\"wifi_last_begin_status\":" + String(wifiRecovery.lastBeginStatus);
  s += ",\"wifi_station_start_events\":" + String(trace.stationStarts);
  s += ",\"wifi_station_stop_events\":" + String(trace.stationStops);
  s += ",\"wifi_association_events\":" + String(trace.associationEvents);
  s += ",\"wifi_status\":" + String(int(WiFi.status()));
  s += ",\"wifi_sleep_mode\":" + String(int(WiFi.getSleep()));
  s += ",\"wifi_disconnect_events\":" + String(trace.disconnectEvents);
  s += ",\"wifi_connection_losses\":" + String(trace.connectionLosses);
  s += ",\"wifi_got_ip_events\":" + String(trace.gotIpEvents);
  s += ",\"wifi_lost_ip_events\":" + String(trace.lostIpEvents);
  s += ",\"wifi_last_disconnect_reason\":" + String(trace.lastReason);
  s += ",\"wifi_last_disconnect_label\":\"" + String(wifiReasonLabel(trace.lastReason)) + "\"";
  s += ",\"wifi_last_disconnect_uptime_ms\":";
  s += trace.disconnectEvents ? String(trace.lastDisconnectMs) : String("null");
  s += ",\"wifi_last_rssi_dbm\":"; s += wifiHasRssi ? String(wifiLastRssi) : String("null");
  s += ",\"wifi_min_sampled_rssi_dbm\":"; s += wifiHasRssi ? String(wifiMinRssi) : String("null");
  s += ",\"wifi_rssi_sample_age_ms\":";
  s += wifiHasRssi ? String(uint32_t(millis()-wifiLastSampleMs)) : String("null");
  s += ",\"wifi_channel\":" + String(WiFi.channel());
  s += ",\"reset_reason_code\":" + String(wifiResetReason);
  s += ",\"reset_reason_label\":\"" + String(resetReasonLabel(wifiResetReason)) + "\"";
  s += ",\"free_heap_bytes\":" + String(ESP.getFreeHeap());
  return s;
}

String wifiHtml() {
  const WifiTrace trace = wifiSnapshot();
  String html = F("<section><h2>Connexion Wi-Fi</h2>");
  html += "<p>Adresse : <code>http://" + WiFi.localIP().toString() + "/</code><br>";
  html += "Dernier signal : " + (wifiHasRssi ? String(wifiLastRssi) + " dBm" : String("en attente"));
  html += " · pertes de connexion depuis le démarrage : <strong>" + String(trace.connectionLosses) + "</strong></p>";
  if (trace.disconnectEvents) html += "<p>Dernier motif reçu : " + String(trace.lastReason) + " · " + wifiReasonLabel(trace.lastReason) + "</p>";
  html += "<p>Phase : " + String(wifiPhaseLabel(wifiRecovery.phase)) + " · essais : " + String(wifiRecovery.attempts) + "</p>";
  html += F("<p>Correctif Wi-Fi v0.8.2 : tentatives de 45 s maximum et attente progressive avant relance. Le moniteur série à 115200 bauds conserve les motifs des coupures ; les relevés figurent aussi dans le diagnostic.</p>"
    "<p><a href='/diagnostic.json'>Télécharger le diagnostic Wi-Fi et compteur</a> · <a href='/'>Actualiser</a></p></section>");
  return html;
}


// Requete fixe de lecture des informations ; la recherche est construite separement.
static const char ECU_INFO_COMMAND[] = "APS1100160001END";
constexpr uint16_t ECU_PORT = 8899;
constexpr size_t ECU_RESPONSE_CAPACITY = 4096;
constexpr uint32_t ECU_REPLY_TIMEOUT_MS = 12000;
constexpr uint32_t ECU_RETRY_GAP_MS = 60000;
WiFiClient ecuClient;
IPAddress ecuTarget;
String ecuTargetText = "IP_ECU"; // Derniere adresse connue ; editable sur la page.
String formToken;
uint8_t ecuResponse[ECU_RESPONSE_CAPACITY] = {};
size_t ecuResponseSize = 0, ecuCommandBytesSent = 0;
bool ecuPending = false, ecuActive = false, ecuTcpConnected = false;
bool ecuResponseComplete = false;
uint32_t ecuQueryCount = 0, ecuQueryStartedMs = 0;
const char* ecuQueryStatus = "pas_demande";
char ecuUid[13] = {};
char ecuVersionModel[65] = {};

// BEGIN ECU_PROTOCOL_HELPERS : fonctions pures testees sur ordinateur.
int ecuDeclaredLength(const uint8_t* data, size_t size) {
  if (size < 9) return 0;
  if (memcmp(data, "APS", 3) || data[3] != '1' ||
      (data[4] != '1' && data[4] != '2')) return -1;
  int result = 0;
  for (size_t i = 5; i < 9; ++i) {
    if (data[i] < '0' || data[i] > '9') return -1;
    result = result * 10 + data[i] - '0';
  }
  return result >= 16 ? result : -1;
}

bool ecuInfoEnvelopeValid(const uint8_t* data, size_t size) {
  const int declared = ecuDeclaredLength(data, size);
  if (declared < 28 || size != size_t(declared)) return false;
  return !memcmp(data + 9, "0001", 4) &&
         !memcmp(data + size - 3, "END", 3);
}

bool ecuExtractIdentity(const uint8_t* data, size_t size,
                        char* uid, size_t uidCapacity,
                        char* versionModel, size_t versionCapacity) {
  if (uidCapacity) uid[0] = '\0';
  if (versionCapacity) versionModel[0] = '\0';
  if (uidCapacity < 13 || !versionCapacity ||
      !ecuInfoEnvelopeValid(data, size)) return false;
  for (size_t i = 13; i < 25; ++i) {
    if (data[i] < '0' || data[i] > '9') return false;
  }
  memcpy(uid, data + 13, 12);
  uid[12] = '\0';
  // Le champ modele/version est prefixe par sa longueur ASCII sur 3 chiffres.
  // Les octets numeriques precedents peuvent etre binaires : aucun strlen dessus.
  for (size_t i = 28; i + 4 <= size - 3; ++i) {
    if (memcmp(data + i, "ECU", 3) ||
        (data[i + 3] != '-' && data[i + 3] != '_')) continue;
    size_t length = 0;
    bool valid = true;
    for (size_t j = i - 3; j < i; ++j) {
      if (data[j] < '0' || data[j] > '9') { valid = false; break; }
      length = length * 10 + data[j] - '0';
    }
    if (!valid || length < 4 || length >= versionCapacity ||
        length > size - 3 - i) continue;
    for (size_t j = i; j < i + length; ++j) {
      const uint8_t c = data[j];
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) {
        valid = false;
        break;
      }
    }
    if (!valid) continue;
    memcpy(versionModel, data + i, length);
    versionModel[length] = '\0';
    break;
  }
  return true;
}
// END ECU_PROTOCOL_HELPERS

// Valeurs effectivement lues sur cet ECU, dans le diagnostic fourni.
static const char EXPECTED_ECU_UID[] = "000000000000";
static const char EXPECTED_ECU_VERSION[] = "ECU-C-Z_C1.2.27A";
constexpr uint32_t SEARCH_REPLY_TIMEOUT_MS = 20000;
WiFiClient searchClient;
IPAddress searchTarget;
String searchTargetText;
char searchRequest[29] = {};
uint8_t searchResponse[ECU_RESPONSE_CAPACITY] = {};
size_t searchResponseSize = 0, searchCommandBytesSent = 0;
bool searchPending = false, searchActive = false, searchTcpConnected = false;
bool searchResponseComplete = false, ecuIdentityUsable = false;
uint32_t searchQueryCount = 0, searchQueryStartedMs = 0, lastEcuActionMs = 0;
const char* searchQueryStatus = "pas_demande";
char searchReplyCode[3] = {};

// BEGIN SOURCE_READ_STATE
WiFiClient sourceClient;
char sourceRequest[29] = {}, sourceReplyCode[3] = {};
uint8_t sourceResponse[ECU_RESPONSE_CAPACITY] = {};
size_t sourceResponseSize = 0, sourceCommandBytesSent = 0;
bool sourceAwaitIdentity = false, sourceActive = false, sourceProbeAttempted = false;
bool sourceTcpConnected = false, sourceResponseComplete = false, sourceJsonValid = false, sourceTruncated = false;
int sourcePayloadLength = -1;
uint32_t sourceStartedMs = 0, sourceQueryCount = 0;
constexpr uint32_t SOURCE_READ_TIMEOUT_MS = 30000;
const char* sourceStatus = "pas_demande";
String sourceTargetText;
// END SOURCE_READ_STATE

// Une seule lecture 0071/01 par clic, apres identification de l'ECU attendu.
// C1.2.26 audite ; reponse 0071/01 vide (code 00) observee deux fois sur C1.2.27A.
WiFiClient configClient;
char configRequest[31] = {};
uint8_t configResponse[ECU_RESPONSE_CAPACITY] = {};
size_t configResponseSize = 0, configCommandBytesSent = 0;
bool configJobActive = false, configAwaitIdentity = false, configActive = false;
bool configTcpConnected = false, configResponseComplete = false, configJsonValid = false;
bool configResponseTruncated = false;
uint32_t configQueryCount = 0, configQueryStartedMs = 0;
const char* configQueryStatus = "pas_demande";
char configReplyCode[3] = {};
int configPayloadLength = -1, configEspListed = -1;
String configTargetText;

// BEGIN SEARCH_PROTOCOL_HELPERS
bool buildMeterSearchRequest(char* output, size_t capacity, const char* uid) {
  if (capacity) output[0] = '\0';
  if (capacity < 29 || !uid) return false;
  for (size_t i = 0; i < 12; ++i) {
    if (uid[i] < '0' || uid[i] > '9') return false;
  }
  if (uid[12] != '\0') return false;
  memcpy(output, "APS1100280070", 13);
  memcpy(output + 13, uid, 12);
  memcpy(output + 25, "END", 3);
  output[28] = '\0';
  return true;
}

int meterSearchPayloadLength(const uint8_t* data, size_t size) {
  const int declared = ecuDeclaredLength(data, size);
  if (declared < 22 || size != size_t(declared)) return -1;
  if (memcmp(data + 9, "0070", 4) || memcmp(data + size - 3, "END", 3)) return -1;
  for (size_t i = 13; i < 15; ++i) {
    if (data[i] < '0' || data[i] > '9') return -1;
  }
  int length = 0;
  for (size_t i = 15; i < 19; ++i) {
    if (data[i] < '0' || data[i] > '9') return -1;
    length = length * 10 + data[i] - '0';
  }
  return size_t(length) + 22 == size ? length : -1;
}

bool meterSearchEnvelopeValid(const uint8_t* data, size_t size) {
  return meterSearchPayloadLength(data, size) >= 0;
}
// END SEARCH_PROTOCOL_HELPERS

// BEGIN CONFIG_PROTOCOL_HELPERS
bool buildMeterConfigReadRequest(char* output, size_t capacity, const char* uid) {
  if (!output) return false;
  if (capacity) output[0] = '\0';
  if (capacity < 31 || !uid) return false;
  for (size_t i = 0; i < 12; ++i)
    if (uid[i] < '0' || uid[i] > '9') return false;
  if (uid[12] != '\0') return false;
  memcpy(output, "APS1100300071", 13);
  memcpy(output + 13, uid, 12);
  memcpy(output + 25, "01END", 5); // Operation 01 uniquement, jamais un parametre Web.
  output[30] = '\0';
  return true;
}

bool meterConfigReadEnvelopeValid(const uint8_t* data, size_t size) {
  if (!data || size < 20 || ecuDeclaredLength(data, size) != int(size)) return false;
  if (memcmp(data + 9, "007101", 6) || memcmp(data + size - 3, "END", 3)) return false;
  if (data[15] < '0' || data[15] > '9' || data[16] < '0' || data[16] > '9') return false;
  // Une erreur ECU peut omettre le champ longueur ; conserver son code.
  if (size == 20) return memcmp(data + 15, "00", 2) != 0;
  if (size < 24) return false;
  size_t length = 0;
  for (size_t i = 17; i < 21; ++i) {
    if (data[i] < '0' || data[i] > '9') return false;
    length = length * 10 + size_t(data[i] - '0');
  }
  return length + 24 == size;
}

int meterConfigReadPayloadLength(const uint8_t* data, size_t size) {
  if (!meterConfigReadEnvelopeValid(data, size) || size < 24 ||
      memcmp(data + 15, "00", 2)) return -1;
  return int(size - 24);
}
// END CONFIG_PROTOCOL_HELPERS


// Constantes du profil dans le firmware fourni : ce ne sont pas vos identifiants.
static const uint8_t PROFILE_KEY[] = "EjOS0+8dQgJy/xzq";
static const uint8_t PROFILE_IV[] = "JV5c2GbJHOledknW";

uint16_t probeCrc16(const uint8_t* bytes, size_t length) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (unsigned j = 0; j < 8; ++j) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
  }
  return crc;
}

size_t probeUnpad16(const uint8_t* data, size_t length) {
  if (!length || length % 16) return 0;
  const uint8_t pad = data[length - 1];
  if (!pad || pad > 16 || pad > length) return 0;
  for (size_t i = length - pad; i < length; ++i) {
    if (data[i] != pad) return 0;
  }
  return length - pad;
}

bool probeIsExpectedRead(const uint8_t* frame, size_t length) {
  const uint8_t expected[] = {1, 4, 0, 1, 0, 88};
  if (length != 8 || memcmp(frame, expected, sizeof(expected))) return false;
  return probeCrc16(frame, 6) ==
         (uint16_t(frame[6]) | (uint16_t(frame[7]) << 8));
}

String hexOf(const uint8_t* bytes, size_t length) {
  const char* digits = "0123456789ABCDEF";
  String out;
  out.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    out += digits[bytes[i] >> 4];
    out += digits[bytes[i] & 15];
  }
  return out;
}

bool decryptFirstBlock(const uint8_t* input, uint8_t* output) {
  uint8_t iv[16];
  memcpy(iv, PROFILE_IV, 16);
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  int result = mbedtls_aes_setkey_dec(&ctx, PROFILE_KEY, 128);
  if (!result)
    result = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, 16, iv, input, output);
  mbedtls_aes_free(&ctx);
  return result == 0;
}

bool localSelfTest() {
  // Vecteur independant calcule a partir de 01 04 00 01 00 58 + CRC + PKCS#7.
  static const uint8_t encrypted[16] = {
    0xea, 0x46, 0x7a, 0x65, 0xfd, 0x6d, 0xe3, 0x7f,
    0xf2, 0xf5, 0x6f, 0xf7, 0xc3, 0x1f, 0x1c, 0x7f
  };
  uint8_t plain[16];
  return decryptFirstBlock(encrypted, plain) &&
         probeIsExpectedRead(plain, probeUnpad16(plain, sizeof(plain)));
}

void inspectCapture() {
  if (decodeTried || captureSize < 16) return;
  decodeTried = true;
  uint8_t plain[16];
  if (!decryptFirstBlock(captureData, plain)) return;
  const size_t length = probeUnpad16(plain, sizeof(plain));
  if (!length) return;
  decodedHex = hexOf(plain, length);
  expectedRead = probeIsExpectedRead(plain, length);
  Serial.print("Premier bloc dechiffre : "); Serial.println(decodedHex);
  Serial.print("Lecture du profil attendu : "); Serial.println(expectedRead ? "OUI" : "NON");
}


void renewFormToken() {
  char value[17];
  snprintf(value, sizeof(value), "%08lX%08lX",
           (unsigned long)esp_random(), (unsigned long)esp_random());
  formToken = value;
}

uint32_t ecuWaitSeconds() {
  if (!ecuQueryCount && !searchQueryCount) return 0;
  const uint32_t elapsed = uint32_t(millis() - lastEcuActionMs);
  return elapsed >= ECU_RETRY_GAP_MS ? 0 : (ECU_RETRY_GAP_MS - elapsed + 999) / 1000;
}

const char* ecuStatusLabel() {
  if (!strcmp(ecuQueryStatus, "pas_demande")) return "Aucune lecture demandée";
  if (!strcmp(ecuQueryStatus, "en_attente")) return "Lecture en préparation";
  if (!strcmp(ecuQueryStatus, "connexion")) return "Connexion en cours";
  if (!strcmp(ecuQueryStatus, "lecture")) return "Attente de la réponse";
  if (!strcmp(ecuQueryStatus, "reponse_recue")) return "Réponse complète reçue";
  if (!strcmp(ecuQueryStatus, "connexion_impossible")) return "Connexion impossible : vérifier l’IP et le réseau";
  if (!strcmp(ecuQueryStatus, "envoi_incomplet")) return "Envoi incomplet ; aucune relance";
  if (!strcmp(ecuQueryStatus, "delai_depasse")) return "Délai de réponse dépassé";
  if (!strcmp(ecuQueryStatus, "connexion_fermee")) return "Connexion fermée avant une réponse complète";
  if (!strcmp(ecuQueryStatus, "wifi_perdu")) return "Connexion Wi-Fi perdue";
  if (!strcmp(ecuQueryStatus, "format_inattendu")) return "Format inattendu ; conserver le diagnostic";
  if (!strcmp(ecuQueryStatus, "reponse_trop_grande")) return "Réponse trop grande ; capture limitée";
  return "Diagnostic disponible";
}

void finishEcuQuery(const char* result) {
  ecuClient.stop();
  ecuActive = false;
  ecuPending = false;
  ecuResponseComplete = ecuInfoEnvelopeValid(ecuResponse, ecuResponseSize);
  ecuExtractIdentity(ecuResponse, ecuResponseSize,
                     ecuUid, sizeof(ecuUid), ecuVersionModel, sizeof(ecuVersionModel));
  ecuIdentityUsable = ecuResponseComplete &&
      !strcmp(ecuUid, EXPECTED_ECU_UID) && !strcmp(ecuVersionModel, EXPECTED_ECU_VERSION);
  ecuQueryStatus = result;
  Serial.print("Lecture ECU : "); Serial.println(ecuStatusLabel());
  Serial.print("Identifiant : "); Serial.println(ecuUid);
  Serial.print("Modele/version : "); Serial.println(ecuVersionModel);
}

bool ecuAddressIsLocalUnicast(const IPAddress& ip) {
  const IPAddress local = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();
  bool hasHostBits = false, isBroadcast = true, hasMask = false;
  if (ip[0] == 0 || ip[0] == 127 || ip[0] >= 224 || ip == local) return false;
  for (size_t i = 0; i < 4; ++i) {
    if (mask[i]) hasMask = true;
    if ((ip[i] & mask[i]) != (local[i] & mask[i])) return false;
    const uint8_t hostMask = uint8_t(~mask[i]);
    if (ip[i] & hostMask) hasHostBits = true;
    if ((ip[i] & hostMask) != hostMask) isBroadcast = false;
  }
  return hasMask && hasHostBits && !isBroadcast;
}

void startEcuInfoQuery(const IPAddress& target) {
  ecuIdentityUsable = false;
  ecuTarget = target;
  ecuTargetText = target.toString();
  ecuResponseSize = 0;
  ecuCommandBytesSent = 0;
  ecuTcpConnected = false;
  ecuResponseComplete = false;
  ecuUid[0] = '\0';
  ecuVersionModel[0] = '\0';
  ++ecuQueryCount;
  ecuQueryStartedMs = millis();
  lastEcuActionMs = ecuQueryStartedMs;
  ecuQueryStatus = "en_attente";
  pauseShellyForEcu();
  ecuPending = true;
}

void handleEcuInfoRequest() {
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("token") || web.arg("token") != formToken) {
    web.send(400, "text/plain; charset=utf-8", "Recharge la page avant une nouvelle lecture.");
    return;
  }
  if (switchJobActive || liveEnabled || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) {
    web.send(429, "text/plain; charset=utf-8", "Une seule lecture à la fois. Attends une minute entre deux essais.");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    web.send(409, "text/plain; charset=utf-8", "Wi-Fi déconnecté.");
    return;
  }
  IPAddress target;
  String address = web.arg("ecu_ip");
  address.trim();
  if (!target.fromString(address) || !ecuAddressIsLocalUnicast(target)) {
    web.send(400, "text/plain; charset=utf-8", "Indique l’adresse IPv4 de l’ECU sur le même réseau local que l’ESP32.");
    return;
  }
  startEcuInfoQuery(target);
  renewFormToken();
  web.sendHeader("Location", "/");
  web.send(303, "text/plain", "Lecture demandee.");
}

void serviceEcuQuery() {
  if (ecuPending) {
    ecuPending = false;
    ecuActive = true;
    ecuQueryStatus = "connexion";
    ecuClient.stop();
    if (!ecuClient.connect(ecuTarget, ECU_PORT, 1500)) {
      finishEcuQuery("connexion_impossible");
      return;
    }
    ecuTcpConnected = true;
    ecuClient.setTimeout(1); // Delai interne court ; reception geree sans readBytes().
    ecuCommandBytesSent = ecuClient.write(
        reinterpret_cast<const uint8_t*>(ECU_INFO_COMMAND), sizeof(ECU_INFO_COMMAND) - 1);
    if (ecuCommandBytesSent != sizeof(ECU_INFO_COMMAND) - 1) {
      finishEcuQuery("envoi_incomplet");
      return;
    }
    ecuQueryStatus = "lecture";
  }
  if (!ecuActive) return;
  unsigned budget = 1024;
  while (ecuClient.available() && budget--) {
    const int value = ecuClient.read();
    if (value < 0) break;
    if (ecuResponseSize >= ECU_RESPONSE_CAPACITY) {
      finishEcuQuery("reponse_trop_grande");
      return;
    }
    ecuResponse[ecuResponseSize++] = uint8_t(value);
    const int expected = ecuDeclaredLength(ecuResponse, ecuResponseSize);
    if (expected > int(ECU_RESPONSE_CAPACITY)) {
      finishEcuQuery("reponse_trop_grande");
      return;
    }
    if (expected > 0 && ecuResponseSize == size_t(expected)) {
      finishEcuQuery(ecuInfoEnvelopeValid(ecuResponse, ecuResponseSize)
                       ? "reponse_recue" : "format_inattendu");
      return;
    }
  }
  if (!ecuClient.connected() && !ecuClient.available()) {
    finishEcuQuery("connexion_fermee");
    return;
  }
  if (uint32_t(millis() - ecuQueryStartedMs) >= ECU_REPLY_TIMEOUT_MS)
    finishEcuQuery("delai_depasse");
}

String ecuInfoJsonFields() {
  String s = ",\"ecu_query_status\":\"" + String(ecuQueryStatus) + "\"";
  s += ",\"ecu_query_ip\":\"" + ecuTargetText + "\"";
  s += ",\"ecu_query_count\":" + String(ecuQueryCount);
  s += ",\"ecu_tcp_connected\":"; s += ecuTcpConnected ? "true" : "false";
  s += ",\"ecu_command_bytes_sent\":" + String(ecuCommandBytesSent);
  s += ",\"ecu_response_bytes\":" + String(ecuResponseSize);
  s += ",\"ecu_response_declared_length\":" + String(ecuDeclaredLength(ecuResponse, ecuResponseSize));
  s += ",\"ecu_response_complete\":"; s += ecuResponseComplete ? "true" : "false";
  s += ",\"ecu_uid\":\"" + String(ecuUid) + "\"";
  s += ",\"ecu_version_model\":\"" + String(ecuVersionModel) + "\"";
  s += ",\"ecu_response_hex\":\"" + hexOf(ecuResponse, ecuResponseSize) + "\"";
  return s;
}

String ecuInfoHtml() {
  String html = F("<section><h2>Lire les informations de l’ECU</h2>"
    "<p>Une lecture demande l’identifiant et la version complète à l’adresse indiquée. "
    "Elle ne sélectionne aucun compteur et ne change aucun réglage.</p>"
    "<form action='/ecu-info' method='post'><label for='ecu_ip'>Adresse IP de l’ECU</label> "
    "<input id='ecu_ip' name='ecu_ip' inputmode='decimal' required value='");
  html += ecuTargetText + "'><input type='hidden' name='token' value='" + formToken + "'>";
  html += "<p><button type='submit'";
  if (switchJobActive || liveEnabled || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) html += " disabled";
  html += F(">Lire les informations ECU</button></p></form>");
  html += "<p><strong>" + String(ecuStatusLabel()) + "</strong></p>";
  if (ecuWaitSeconds() && !ecuPending && !ecuActive)
    html += "<p>Nouvel essai possible dans " + String(ecuWaitSeconds()) + " s ; actualiser ensuite.</p>";
  html += "<p>Identifiant : <code>" + String(ecuUid[0] ? ecuUid : "Pas encore lu") + "</code></p>";
  html += "<p>Version / modèle : <code>" + String(ecuVersionModel[0] ? ecuVersionModel : "Pas encore extrait") + "</code></p>";
  html += "<p>Tentatives : " + String(ecuQueryCount) + " · Octets reçus : " + String(ecuResponseSize) + "</p>";
  html += F("<p><a href='/diagnostic.json'>Télécharger le diagnostic</a> · <a href='/'>Actualiser</a></p>"
    "<p>Cette lecture vérifie la communication locale. La découverte a réussi lors de l’essai précédent ; la lecture de ses mesures reste à tester.</p></section>");
  return html;
}


const char* searchStatusLabel() {
  if (!strcmp(searchQueryStatus, "pas_demande")) return "Aucune recherche demandée";
  if (!strcmp(searchQueryStatus, "en_attente")) return "Recherche en préparation";
  if (!strcmp(searchQueryStatus, "connexion")) return "Connexion en cours";
  if (!strcmp(searchQueryStatus, "lecture")) return "Attente de la réponse de recherche";
  if (!strcmp(searchQueryStatus, "reponse_recue")) return "Réponse de recherche reçue";
  if (!strcmp(searchQueryStatus, "connexion_impossible")) return "Connexion impossible";
  if (!strcmp(searchQueryStatus, "envoi_incomplet")) return "Envoi incomplet ; aucune relance";
  if (!strcmp(searchQueryStatus, "delai_depasse")) return "Délai dépassé ; conserver le diagnostic";
  if (!strcmp(searchQueryStatus, "connexion_fermee")) return "Connexion fermée avant une réponse complète";
  if (!strcmp(searchQueryStatus, "wifi_perdu")) return "Connexion Wi-Fi perdue";
  if (!strcmp(searchQueryStatus, "format_inattendu")) return "Format inattendu ; conserver le diagnostic";
  if (!strcmp(searchQueryStatus, "reponse_trop_grande")) return "Réponse trop grande ; capture limitée";
  return "Diagnostic disponible";
}

void finishMeterSearch(const char* result) {
  searchClient.stop();
  searchActive = false;
  searchPending = false;
  searchResponseComplete = meterSearchEnvelopeValid(searchResponse, searchResponseSize);
  searchReplyCode[0] = '\0';
  if (searchResponseComplete) {
    memcpy(searchReplyCode, searchResponse + 13, 2);
    searchReplyCode[2] = '\0';
  }
  searchQueryStatus = result;
  Serial.print("Recherche ECU : "); Serial.println(searchStatusLabel());
}

void handleMeterSearchRequest() {
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("token") || web.arg("token") != formToken) {
    web.send(400, "text/plain; charset=utf-8", "Recharge la page avant une nouvelle recherche.");
    return;
  }
  if (switchJobActive || liveEnabled || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) {
    web.send(429, "text/plain; charset=utf-8", "Une seule opération à la fois, espacée d’une minute.");
    return;
  }
  if (WiFi.status() != WL_CONNECTED || !mdnsReady || !ecuIdentityUsable) {
    web.send(409, "text/plain; charset=utf-8", "Lis d’abord les informations de l’ECU. L’identifiant et la version doivent correspondre au diagnostic précédent, et le service mDNS doit être actif.");
    return;
  }
  if (!ecuAddressIsLocalUnicast(ecuTarget) ||
      !buildMeterSearchRequest(searchRequest, sizeof(searchRequest), ecuUid)) {
    web.send(400, "text/plain; charset=utf-8", "Adresse ou identifiant ECU invalide.");
    return;
  }
  searchTarget = ecuTarget;
  searchTargetText = ecuTargetText;
  searchResponseSize = 0;
  searchCommandBytesSent = 0;
  searchTcpConnected = false;
  searchResponseComplete = false;
  searchReplyCode[0] = '\0';
  ++searchQueryCount;
  searchQueryStartedMs = millis();
  lastEcuActionMs = searchQueryStartedMs;
  searchQueryStatus = "en_attente";
  pauseShellyForEcu();
  searchPending = true;
  renewFormToken();
  web.sendHeader("Location", "/");
  web.send(303, "text/plain", "Recherche demandee.");
}

void serviceMeterSearch() {
  if (searchPending) {
    searchPending = false;
    searchActive = true;
    searchQueryStatus = "connexion";
    searchClient.stop();
    if (!searchClient.connect(searchTarget, ECU_PORT, 1500)) {
      finishMeterSearch("connexion_impossible");
      return;
    }
    searchTcpConnected = true;
    searchClient.setTimeout(1); // Delai interne court ; reception geree sans readBytes().
    searchCommandBytesSent = searchClient.write(
        reinterpret_cast<const uint8_t*>(searchRequest), sizeof(searchRequest) - 1);
    if (searchCommandBytesSent != sizeof(searchRequest) - 1) {
      finishMeterSearch("envoi_incomplet");
      return;
    }
    searchQueryStatus = "lecture";
  }
  if (!searchActive) return;
  unsigned budget = 1024;
  while (searchClient.available() && budget--) {
    const int value = searchClient.read();
    if (value < 0) break;
    if (searchResponseSize >= ECU_RESPONSE_CAPACITY) {
      finishMeterSearch("reponse_trop_grande");
      return;
    }
    searchResponse[searchResponseSize++] = uint8_t(value);
    const int expected = ecuDeclaredLength(searchResponse, searchResponseSize);
    if (expected > int(ECU_RESPONSE_CAPACITY)) {
      finishMeterSearch("reponse_trop_grande");
      return;
    }
    if (expected > 0 && searchResponseSize == size_t(expected)) {
      finishMeterSearch(meterSearchEnvelopeValid(searchResponse, searchResponseSize)
                       ? "reponse_recue" : "format_inattendu");
      return;
    }
  }
  if (!searchClient.connected() && !searchClient.available()) {
    finishMeterSearch("connexion_fermee");
    return;
  }
  if (uint32_t(millis() - searchQueryStartedMs) >= SEARCH_REPLY_TIMEOUT_MS)
    finishMeterSearch("delai_depasse");
}

String searchJsonFields() {
  String s = ",\"search_query_status\":\"" + String(searchQueryStatus) + "\"";
  s += ",\"search_query_ip\":\"" + searchTargetText + "\"";
  s += ",\"search_query_count\":" + String(searchQueryCount);
  s += ",\"search_tcp_connected\":"; s += searchTcpConnected ? "true" : "false";
  s += ",\"search_command_bytes_sent\":" + String(searchCommandBytesSent);
  s += ",\"search_request_ascii\":\"" + String(searchRequest) + "\"";
  s += ",\"search_response_bytes\":" + String(searchResponseSize);
  s += ",\"search_response_declared_length\":" + String(ecuDeclaredLength(searchResponse, searchResponseSize));
  s += ",\"search_response_complete\":"; s += searchResponseComplete ? "true" : "false";
  s += ",\"search_reply_code\":\"" + String(searchReplyCode) + "\"";
  s += ",\"search_payload_bytes\":" + String(meterSearchPayloadLength(searchResponse, searchResponseSize));
  s += ",\"search_response_hex\":\"" + hexOf(searchResponse, searchResponseSize) + "\"";
  return s;
}

String meterSearchPayloadHtml() {
  const int length = meterSearchPayloadLength(searchResponse, searchResponseSize);
  if (length < 0) return "Réponse absente, incomplète ou de format non reconnu.";
  if (!length) return "L’ECU n’a joint aucune liste à sa réponse.";
  String html;
  for (size_t i = 19; i < 19 + size_t(length); ++i) {
    const uint8_t c = searchResponse[i];
    if (c == '&') html += "&amp;";
    else if (c == '<') html += "&lt;";
    else if (c == '>') html += "&gt;";
    else if (c == '"') html += "&quot;";
    else if (c == '\'') html += "&#39;";
    else if (c < 32 && c != '\n' && c != '\t' && c != '\r') html += '.';
    else html += char(c);
  }
  return html;
}

String meterSearchHtml() {
  String html = F("<section><h2>Rechercher le compteur ESP32</h2>"
    "<p>La découverte de cet ESP32 a été confirmée sur ton ECU C1.2.27A. "
    "Ce bouton permet de la répéter sur demande.</p>");
  if (!ecuIdentityUsable)
    html += F("<p>Commence par <strong>Lire les informations ECU</strong>. La cible attendue est l’ECU identifié lors du dernier essai.</p>");
  else
    html += "<p>Cible identifiée : <code>" + ecuTargetText + "</code> · <code>" + String(ecuUid) + "</code></p>";
  html += "<form action='/meter-search' method='post'><input type='hidden' name='token' value='" + formToken + "'>";
  html += "<button type='submit'";
  if (configJobActive || !ecuIdentityUsable || !mdnsReady || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds())
    html += " disabled";
  html += F(">Lancer une recherche</button></form>");
  if (ecuWaitSeconds()) html += "<p>Prochaine opération possible dans " + String(ecuWaitSeconds()) + " s. La page s’actualise automatiquement.</p>";
  html += "<p><strong>" + String(searchStatusLabel()) + "</strong></p>";
  html += "<p>Recherches demandées : " + String(searchQueryCount) + " · Octets reçus : " + String(searchResponseSize) + "</p>";
  if (searchReplyCode[0]) html += "<p>Code renvoyé par l’ECU : <code>" + String(searchReplyCode) + "</code></p>";
  if (searchResponseSize) html += "<pre>" + meterSearchPayloadHtml() + "</pre>";
  html += F("<p><a href='/diagnostic.json'>Télécharger le diagnostic</a></p>"
    "<p>Garde les tores configurés. Le prototype ne fournit pas encore de mesures de régulation.</p></section>");
  return html;
}

// Une connexion WS locale persistante ; un seul instantane complet en vol.
constexpr uint32_t SHELLY_POLL_MS = 2000;
constexpr uint32_t SHELLY_TIMEOUT_MS = 5000;
constexpr uint32_t SHELLY_MAX_AGE_MS = 15000;
static const char SHELLY_MAC[] = "000000000000";
static const char SHELLY_IP_TEXT[] = "IP_SHELLY";
static const char SHELLY_RPC_SOURCE[] = "acc-esp32";
constexpr unsigned SHELLY_CHANNEL = 1;
ShellyWebSocket shellyWs;
bool shellyEnabled=false, shellyActive=false, shellyDueNow=false;
bool shellyReadOk=false, shellyHasReading=false, shellySessionOpen=false;
bool shellyAuthRequired=false;
uint32_t shellyLastAttemptMs=0, shellyLastGoodMs=0;
uint32_t shellyQueryCount=0, shellySuccessCount=0, shellyQueryFailures=0, shellyQueryCancelled=0;
uint32_t shellyRpcId=0, shellyPendingId=0;
uint32_t shellyConnectAttempts=0, shellyConnectFailures=0, shellyWsSessions=0;
uint32_t shellyWsNotifications=0, shellyUnexpectedReplies=0, shellyLastConnectMs=0;
uint32_t shellyRetryStartedMs=0, shellyRetryWaitMs=0, shellyConnectionStartedMs=0;
unsigned shellyFailureStreak=0, shellyConnectAttempt=0;
int shellySocketError=0, shellyHttpCode=0;
size_t shellyBodySize=0;
const char* shellyStatus="pas_demande";
double shellyPowerW=0, shellyVoltageV=0, shellyCurrentA=0, shellyFrequencyHz=0;
double shellyApparentVA=NAN, shellyPowerFactor=NAN, shellyImportWh=NAN, shellyExportWh=NAN;
uint32_t shellyMaxServiceMs=0, shellyMaxServiceGapMs=0, shellyServiceLastMs=0;
bool shellyServiceSeen=false;
uint32_t loopMaxBodyMs=0, loopMaxGapMs=0, loopLastMs=0;
bool loopSeen=false;

struct ShellyIoEvent {
  uint32_t atMs = 0, query = 0, durationMs = 0;
  unsigned attempt = 0;
  int httpCode = 0, socketError = 0;
  size_t bodyBytes = 0;
  const char* stage = "aucune";
  const char* result = "aucun";
};
constexpr unsigned SHELLY_EVENT_CAPACITY = 16;
ShellyIoEvent shellyEvents[SHELLY_EVENT_CAPACITY], shellyLastError;
unsigned shellyEventNext = 0, shellyEventCount = 0;
uint32_t shellyEventsDropped = 0;
bool shellyHasError = false;

void recordShellyEvent(const char* stage, const char* result, uint32_t durationMs, bool error) {
  ShellyIoEvent event;
  event.atMs = millis(); event.query = shellyQueryCount;
  event.durationMs = durationMs; event.attempt = shellyConnectAttempt;
  event.httpCode = shellyHttpCode; event.bodyBytes = shellyBodySize;
  event.socketError = shellySocketError;
  event.stage = stage; event.result = result;
  shellyEvents[shellyEventNext] = event;
  shellyEventNext = (shellyEventNext + 1) % SHELLY_EVENT_CAPACITY;
  if (shellyEventCount < SHELLY_EVENT_CAPACITY) ++shellyEventCount;
  else ++shellyEventsDropped;
  if (error) { shellyLastError = event; shellyHasError = true; }
}

// Parseur JSON borne, sans allocation ni dependance a ArduinoJson.
// Il valide le document entier avant de lire les champs. Les champs ambigus
// (cles dupliquees), null, non numeriques, non finis ou manquants sont rejetes.
class ShellyJson {
public:
  struct Value { const char* first; const char* last; };
  Value root;
  ShellyJson(const char* data, size_t length): root{data, data + length} {}
  static void spaces(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
  }
  static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  static bool stringToken(const char*& p, const char* end) {
    if (p == end || *p++ != '"') return false;
    while (p < end) {
      const unsigned char c = static_cast<unsigned char>(*p++);
      if (c == '"') return true;
      if (c < 32) return false;
      if (c != '\\') continue;
      if (p == end) return false;
      const char escape = *p++;
      if (escape == 'u') {
        for (unsigned i = 0; i < 4; ++i) {
          if (p == end || hexDigit(*p++) < 0) return false;
        }
      } else if (!escape || !strchr("\"\\/bfnrt", escape)) return false;
    }
    return false;
  }
  static bool digit(char c) { return c >= '0' && c <= '9'; }
  static bool numberToken(const char*& p, const char* end) {
    if (p < end && *p == '-') ++p;
    if (p == end) return false;
    if (*p == '0') ++p;
    else {
      if (*p < '1' || *p > '9') return false;
      while (p < end && digit(*p)) ++p;
    }
    if (p < end && *p == '.') {
      ++p;
      if (p == end || !digit(*p)) return false;
      while (p < end && digit(*p)) ++p;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
      ++p;
      if (p < end && (*p == '+' || *p == '-')) ++p;
      if (p == end || !digit(*p)) return false;
      while (p < end && digit(*p)) ++p;
    }
    return true;
  }
  static bool skip(const char*& p, const char* end, unsigned depth = 0) {
    spaces(p, end);
    if (p == end || depth > 16) return false;
    if (*p == '"') return stringToken(p, end);
    if (*p == '{' || *p == '[') {
      const bool object = *p++ == '{';
      const char close = object ? '}' : ']';
      spaces(p, end);
      if (p < end && *p == close) { ++p; return true; }
      for (;;) {
        spaces(p, end);
        if (object) {
          if (!stringToken(p, end)) return false;
          spaces(p, end);
          if (p == end || *p++ != ':') return false;
        }
        if (!skip(p, end, depth + 1)) return false;
        spaces(p, end);
        if (p == end) return false;
        if (*p == close) { ++p; return true; }
        if (*p++ != ',') return false;
      }
    }
    for (const char* literal : {"true", "false", "null"}) {
      const size_t n = strlen(literal);
      if (size_t(end - p) >= n && !memcmp(p, literal, n)) { p += n; return true; }
    }
    return numberToken(p, end);
  }
  bool valid() const {
    const char* p = root.first;
    spaces(p, root.last);
    if (p == root.last || *p != '{' || !skip(p, root.last)) return false;
    spaces(p, root.last);
    return p == root.last;
  }
  static bool stringEquals(Value value, const char* expected, bool ignoreCase = false) {
    const char* p = value.first;
    if (p == value.last || *p++ != '"') return false;
    while (p < value.last) {
      unsigned c = static_cast<unsigned char>(*p++);
      if (c == '"') return p == value.last && !*expected;
      if (c == '\\') {
        if (p == value.last) return false;
        c = static_cast<unsigned char>(*p++);
        if (c == 'u') {
          c = 0;
          for (unsigned i = 0; i < 4; ++i) {
            if (p == value.last) return false;
            const int h = hexDigit(*p++);
            if (h < 0) return false;
            c = 16 * c + unsigned(h);
          }
        } else {
          if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
          else if (c == 'n') c = '\n'; else if (c == 'r') c = '\r';
          else if (c == 't') c = '\t';
          else if (c != '"' && c != '\\' && c != '/') return false;
        }
      }
      if (!*expected || c > 127) return false; // Cles et MAC attendues en ASCII.
      unsigned e = static_cast<unsigned char>(*expected++);
      if (ignoreCase) { c = unsigned(tolower(int(c))); e = unsigned(tolower(int(e))); }
      if (c != e) return false;
    }
    return false;
  }
  // Retour : 0 absent, 1 unique, -1 invalide ou duplique.
  static int field(Value object, const char* name, Value& out) {
    const char* p = object.first;
    spaces(p, object.last);
    if (p == object.last || *p++ != '{') return -1;
    int found = 0;
    spaces(p, object.last);
    if (p < object.last && *p == '}') return 0;
    while (p < object.last) {
      spaces(p, object.last);
      Value key{p, p};
      if (!stringToken(p, object.last)) return -1;
      key.last = p;
      spaces(p, object.last);
      if (p == object.last || *p++ != ':') return -1;
      spaces(p, object.last);
      Value value{p, p};
      if (!skip(p, object.last)) return -1;
      value.last = p;
      if (stringEquals(key, name)) { if (found) return -1; out = value; found = 1; }
      spaces(p, object.last);
      if (p == object.last) return -1;
      if (*p++ == '}') return found;
      if (p[-1] != ',') return -1;
    }
    return -1;
  }
  static bool number(Value object, const char* name, double& result) {
    Value value{};
    if (field(object, name, value) != 1) return false;
    const size_t n = size_t(value.last - value.first);
    if (!n || n >= 64) return false;
    const char* p = value.first;
    if (!numberToken(p, value.last) || p != value.last) return false;
    char buffer[64]; memcpy(buffer, value.first, n); buffer[n] = '\0';
    char* end = nullptr;
    const double d = strtod(buffer, &end);
    if (end != buffer + n || !isfinite(d)) return false;
    result = d;
    return true;
  }
  static bool emptyArray(Value value) {
    const char* p = value.first;
    if (p == value.last || *p++ != '[') return false;
    spaces(p, value.last);
    if (p == value.last || *p++ != ']') return false;
    return p == value.last;
  }
};

const char* decodeShellyStatus(const char* data, size_t size) {
  shellyApparentVA = shellyPowerFactor = shellyImportWh = shellyExportWh = NAN;
  ShellyJson json(data, size);
  ShellyJson::Value system{}, mac{}, channel{}, errors{};
  if (!json.valid()) return "json_invalide";
  if (ShellyJson::field(json.root, "sys", system) != 1 ||
      ShellyJson::field(system, "mac", mac) != 1 ||
      !ShellyJson::stringEquals(mac, SHELLY_MAC, true)) return "identite_inattendue";
  if (ShellyJson::field(json.root, "em1:1", channel) != 1) return "canal_absent";
  const int errorField = ShellyJson::field(channel, "errors", errors);
  if (errorField < 0 || (errorField == 1 && !ShellyJson::emptyArray(errors)))
    return "erreur_mesure_shelly";
  double id, power, voltage, current, frequency;
  if (!ShellyJson::number(channel, "id", id) || id != SHELLY_CHANNEL ||
      !ShellyJson::number(channel, "act_power", power) ||
      !ShellyJson::number(channel, "voltage", voltage) ||
      !ShellyJson::number(channel, "current", current) ||
      !ShellyJson::number(channel, "freq", frequency) ||
      voltage <= 0 || current < 0 || frequency <= 0) return "mesure_invalide";
  // Bornes larges de coherence pour cet appareil 50 A, pas de correction des mesures.
  if (voltage > 300 || current > 100 || frequency > 100 || fabs(power) > 30000)
    return "mesure_hors_plage";
  shellyPowerW = power; shellyVoltageV = voltage;
  shellyCurrentA = current; shellyFrequencyHz = frequency;
  // Champs optionnels : leur absence ne remet pas en cause la puissance valide.
  double extra;
  if (ShellyJson::number(channel, "aprt_power", extra) && extra >= 0 && extra <= 30000)
    shellyApparentVA = extra;
  if (ShellyJson::number(channel, "pf", extra) && fabs(extra) <= 1)
    shellyPowerFactor = extra;
  ShellyJson::Value energy{}, energyErrors{};
  if (ShellyJson::field(json.root, "em1data:1", energy) == 1 &&
      ShellyJson::number(energy, "id", extra) && extra == SHELLY_CHANNEL) {
    const int e = ShellyJson::field(energy, "errors", energyErrors);
    if (e == 0 || (e == 1 && ShellyJson::emptyArray(energyErrors))) {
      if (ShellyJson::number(energy, "total_act_energy", extra) && extra >= 0 && extra <= 1e12)
        shellyImportWh = extra;
      if (ShellyJson::number(energy, "total_act_ret_energy", extra) && extra >= 0 && extra <= 1e12)
        shellyExportWh = extra;
    }
  }
  return "reponse_recue";
}

bool shellyMeasurementValid() {
  return shellyReadOk && WiFi.status() == WL_CONNECTED &&
      uint32_t(millis() - shellyLastGoodMs) < SHELLY_MAX_AGE_MS;
}

const char* shellyStatusLabel() {
  if (!strcmp(shellyStatus,"pas_demande")) return "Lecture pas encore démarrée";
  if (!strcmp(shellyStatus,"en_attente")) return "Connexion au Shelly en préparation";
  if (!strcmp(shellyStatus,"connexion")) return "Ouverture de la connexion au Shelly";
  if (!strcmp(shellyStatus,"lecture")) return "Mise à jour des mesures";
  if (!strcmp(shellyStatus,"reponse_recue")) return "Mesure reçue · connexion maintenue";
  if (!strcmp(shellyStatus,"arrete")) return "Lecture arrêtée";
  if (!strcmp(shellyStatus,"wifi_perdu")) return "Wi-Fi perdu ; redémarrer la lecture après reconnexion";
  if (!strcmp(shellyStatus,"auth_requise")) return "Le Shelly demande une authentification";
  if (!strcmp(shellyStatus,"identite_inattendue")) return "L’appareil ne correspond pas au Shelly attendu";
  if (!strcmp(shellyStatus,"erreur_mesure_shelly")) return "Le Shelly signale une erreur de mesure";
  if (!strcmp(shellyStatus,"pause_ecu")) return "Lecture suspendue pendant l’opération ECU";
  if (!strcmp(shellyStatus,"delai_depasse")) return "Réponse du Shelly trop lente ; reconnexion prévue";
  return "Connexion interrompue ; nouvel essai automatique prévu";
}

void finishShellyQuery(const char* result) {
  const bool wasActive=shellyActive;
  const bool hadTransport=shellyWs.phase!=ShellyWebSocket::IDLE;
  const bool deliberate=!strcmp(result,"arrete")||!strcmp(result,"pause_ecu");
  shellySocketError=shellyWs.socketError;
  shellyHttpCode=shellyWs.httpCode;
  shellyWs.stop(); shellyActive=false; shellyReadOk=false;
  shellyStatus=result;
  if (wasActive) {
    if (deliberate) ++shellyQueryCancelled; else ++shellyQueryFailures;
    recordShellyEvent("lecture",result,uint32_t(millis()-shellyLastAttemptMs),!deliberate);
  } else if (hadTransport) {
    if (!shellySessionOpen && !deliberate) ++shellyConnectFailures;
    recordShellyEvent("connexion",result,uint32_t(millis()-shellyConnectionStartedMs),!deliberate);
  }
  shellySessionOpen=false;
  shellyAuthRequired=!strcmp(result,"auth_requise");
  shellyRetryStartedMs=millis();
  if (deliberate) shellyRetryWaitMs=0;
  else {
    const unsigned shift=shellyFailureStreak<3?shellyFailureStreak:3;
    shellyRetryWaitMs=(2000u<<shift)+(esp_random()%501u);
    if (shellyFailureStreak<3) ++shellyFailureStreak;
  }
}

void pauseShellyForEcu() {
  if (!liveEnabled && shellyWs.phase!=ShellyWebSocket::IDLE) finishShellyQuery("pause_ecu");
}

void handleShellyWsMessage(const char* data, size_t size) {
  ShellyJson json(data,size);
  if (!json.valid()) { finishShellyQuery("json_invalide"); return; }
  ShellyJson::Value method{},destination{},result{},error{};
  const int methodField=ShellyJson::field(json.root,"method",method);
  if (methodField<0) { finishShellyQuery("rpc_invalide"); return; }
  if (methodField==1) {
    // Une notification peut ne contenir que la tension, le Wi-Fi ou un
    // compteur. Elle ne rend JAMAIS une ancienne puissance plus recente.
    ++shellyWsNotifications; return;
  }
  double id=0;
  if (!ShellyJson::number(json.root,"id",id) || !shellyActive || id!=shellyPendingId) {
    ++shellyUnexpectedReplies; return;
  }
  const int dst=ShellyJson::field(json.root,"dst",destination);
  if (dst<0 || (dst==1&&!ShellyJson::stringEquals(destination,SHELLY_RPC_SOURCE))) {
    finishShellyQuery("rpc_destination_invalide"); return;
  }
  const int errorField=ShellyJson::field(json.root,"error",error);
  const int resultField=ShellyJson::field(json.root,"result",result);
  if (errorField<0 || resultField<0 || (errorField==1&&resultField==1)) {
    finishShellyQuery("rpc_invalide"); return;
  }
  if (errorField==1) {
    double code=0;
    const bool auth=ShellyJson::number(error,"code",code)&&(code==401||code==403);
    finishShellyQuery(auth?"auth_requise":"rpc_erreur"); return;
  }
  if (resultField!=1) { finishShellyQuery("rpc_resultat_absent"); return; }
  // Rejeter aussi une reponse arrivee apres une longue pause de loop().
  if (uint32_t(millis()-shellyLastAttemptMs)>=SHELLY_TIMEOUT_MS) {
    finishShellyQuery("delai_depasse"); return;
  }
  shellyBodySize=size_t(result.last-result.first);
  const char* decoded=decodeShellyStatus(result.first,shellyBodySize);
  if (strcmp(decoded,"reponse_recue")) { finishShellyQuery(decoded); return; }
  shellyReadOk=true; shellyHasReading=true; shellyLastGoodMs=millis();
  shellyStatus="reponse_recue"; shellyActive=false;
  ++shellySuccessCount; shellyFailureStreak=0; shellySocketError=0;
  recordShellyEvent("lecture","reponse_recue",uint32_t(millis()-shellyLastAttemptMs),false);
}

void serviceShellyQueryCore() {
  if (WiFi.status()!=WL_CONNECTED) {
    if (shellyEnabled||shellyActive||shellyWs.phase!=ShellyWebSocket::IDLE) finishShellyQuery("wifi_perdu");
    shellyEnabled=false; shellyDueNow=false; return;
  }
  if (!shellyEnabled) return;
  const bool ecuBusy=!liveEnabled&&(sourceActive||switchJobActive||benchActive||configActive||ecuPending||ecuActive||searchPending||searchActive);
  if (ecuBusy&&!switchCanPollShelly()) { pauseShellyForEcu(); return; }
  if (shellyWs.phase==ShellyWebSocket::IDLE) {
    if (shellyAuthRequired&&!shellyDueNow) return;
    if (!shellyDueNow&&uint32_t(millis()-shellyRetryStartedMs)<shellyRetryWaitMs) return;
    shellyDueNow=false; shellyAuthRequired=false;
    const IPAddress target(0,0,0,0);
    if (!ecuAddressIsLocalUnicast(target)) { finishShellyQuery("adresse_hors_reseau"); return; }
    shellyConnectionStartedMs=millis(); shellySessionOpen=false;
    ++shellyConnectAttempts; shellyConnectAttempt=1; shellySocketError=0;
    shellyHttpCode=0; shellyStatus="connexion";
    shellyWs.begin(target,handleShellyWsMessage);
  }
  if (shellyActive&&uint32_t(millis()-shellyLastAttemptMs)>=SHELLY_TIMEOUT_MS) {
    finishShellyQuery("delai_depasse"); return;
  }
  shellyWs.service();
  if (shellyWs.phase==ShellyWebSocket::FAILED) { finishShellyQuery(shellyWs.error); return; }
  if (shellyWs.phase==ShellyWebSocket::CLOSING) shellyReadOk=false;
  if (shellyWs.phase!=ShellyWebSocket::OPEN) return;
  if (!shellySessionOpen) {
    shellySessionOpen=true; ++shellyWsSessions;
    shellyLastConnectMs=shellyWs.connectDurationMs; shellyHttpCode=shellyWs.httpCode;
    recordShellyEvent("connexion","ws_connecte",uint32_t(millis()-shellyConnectionStartedMs),false);
    shellyDueNow=true;
  }
  if (!shellyActive&&(shellyDueNow||uint32_t(millis()-shellyLastAttemptMs)>=SHELLY_POLL_MS)) {
    char request[128];
    if (!++shellyRpcId) ++shellyRpcId;
    const int n=snprintf(request,sizeof(request),"{\"id\":%lu,\"src\":\"%s\",\"method\":\"Shelly.GetStatus\"}",
      static_cast<unsigned long>(shellyRpcId),SHELLY_RPC_SOURCE);
    if (n<0||size_t(n)>=sizeof(request)||!shellyWs.sendText(request,size_t(n))) {
      finishShellyQuery("ws_file_envoi_pleine"); return;
    }
    shellyDueNow=false; shellyActive=true; shellyPendingId=shellyRpcId;
    shellyLastAttemptMs=millis(); ++shellyQueryCount;
    shellyBodySize=0; shellyStatus="lecture";
  }
}

void serviceShellyQuery() {
  const uint32_t at=millis();
  if (shellyServiceSeen) {
    const uint32_t gap=uint32_t(at-shellyServiceLastMs);
    if (gap>shellyMaxServiceGapMs) shellyMaxServiceGapMs=gap;
  }
  shellyServiceSeen=true; shellyServiceLastMs=at;
  serviceShellyQueryCore();
  const uint32_t duration=uint32_t(millis()-at);
  if (duration>shellyMaxServiceMs) shellyMaxServiceMs=duration;
}

void handleShellyRequest() {
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("token") || web.arg("token") != formToken) {
    web.send(400, "text/plain; charset=utf-8", "Recharge la page avant une nouvelle action."); return;
  }
  const String action = web.arg("action");
  if (action == "stop") {
    switchResumeHeld = true;
    stopLiveMeter("lecture_shelly_arretee");
    shellyEnabled = false; shellyDueNow = false; finishShellyQuery("arrete");
  } else if (action == "start") {
    if (WiFi.status() != WL_CONNECTED) {
      web.send(409, "text/plain; charset=utf-8", "Wi-Fi déconnecté."); return;
    }
    if (!shellyEnabled) {
      shellyEnabled = true; shellyDueNow = true; shellyReadOk = false;
      shellyStatus = "en_attente";
    }
  } else {
    web.send(400, "text/plain; charset=utf-8", "Action inconnue."); return;
  }
  renewFormToken();
  web.sendHeader("Location", "/");
  web.send(303, "text/plain", "Lecture Shelly mise a jour.");
}

String shellyNumber(double value, unsigned decimals) {
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%.*f", int(decimals), value);
  return String(buffer);
}

// BEGIN PROFILE_RESPONSE_HELPERS : format reconstitue dans libmetertools.so C1.2.26.
constexpr size_t PROFILE_FLOAT_COUNT = 44;
constexpr size_t PROFILE_REPLY_BYTES = 181; // adresse + fonction + taille + 176 donnees + CRC
constexpr size_t PROFILE_CIPHER_BYTES = 192; // PKCS#7 : 11 octets de bourrage
constexpr size_t PROFILE_PA_INDEX = 9;
constexpr size_t PROFILE_PB_INDEX = 10;
constexpr size_t PROFILE_PC_INDEX = 11;
constexpr size_t PROFILE_PT_INDEX = 18;

static_assert(sizeof(float) == 4, "Le profil requiert des float32");

void profilePutFloat(uint8_t* output, float value) {
  uint32_t bits;
  memcpy(&bits, &value, 4);
  output[0] = uint8_t(bits >> 24); output[1] = uint8_t(bits >> 16);
  output[2] = uint8_t(bits >> 8); output[3] = uint8_t(bits);
}

float profileGetFloat(const uint8_t* input) {
  const uint32_t bits = (uint32_t(input[0]) << 24) | (uint32_t(input[1]) << 16) |
                        (uint32_t(input[2]) << 8) | uint32_t(input[3]);
  float value;
  memcpy(&value, &bits, 4);
  return value;
}

size_t profileEncodeReply(const float* values, size_t count, uint8_t* output, size_t capacity) {
  if (!values || count != PROFILE_FLOAT_COUNT || !output || capacity < PROFILE_REPLY_BYTES) return 0;
  output[0] = 1; output[1] = 4; output[2] = 176;
  for (size_t i = 0; i < count; ++i) profilePutFloat(output + 3 + 4*i, values[i]);
  const uint16_t crc = probeCrc16(output, PROFILE_REPLY_BYTES-2);
  output[179] = uint8_t(crc); output[180] = uint8_t(crc >> 8);
  return PROFILE_REPLY_BYTES;
}

bool profileReplyValid(const uint8_t* input, size_t size) {
  return input && size == PROFILE_REPLY_BYTES && input[0] == 1 && input[1] == 4 &&
      input[2] == 176 && probeCrc16(input, 179) == (uint16_t(input[179]) | uint16_t(input[180]) << 8);
}

bool profilePreviewValues(bool measurementValid, double power, double voltage, double current,
                          double frequency, double apparent, double pf, double importWh,
                          double exportWh, float* values, size_t count) {
  if (!values || count != PROFILE_FLOAT_COUNT) return false;
  // Valeur indisponible explicite. Aucun zero invente pour les champs inconnus.
  // +Inf est le marqueur observe dans les adaptateurs MDNS de C1.2.26.
  // Son acceptation via ce profil par C1.2.27A reste a tester.
  for (size_t i = 0; i < count; ++i) values[i] = INFINITY;
  if (!measurementValid || !isfinite(power) || !isfinite(voltage) || !isfinite(current) ||
      !isfinite(frequency) || fabs(power) > 30000 || voltage <= 0 || voltage > 300 ||
      current < 0 || current > 100 || frequency <= 0 || frequency > 100 ||
      !isfinite(apparent) || apparent < 0 || apparent > 30000 ||
      !isfinite(pf) || fabs(pf) > 1 || !isfinite(importWh) || !isfinite(exportWh) ||
      importWh < 0 || importWh > 1e12 || exportWh < 0 || exportWh > 1e12) return false;
  values[0] = float(voltage); // mUa, registre 1
  values[6] = float(current); // mIa, registre 13
  values[PROFILE_PA_INDEX] = float(power); // mPa, registre 19
  // Modele de banc explicite : flux B/C absents, pas de tensions/PF inventes.
  for (size_t i : {7u,8u,10u,11u,13u,14u,16u,17u,22u,23u,25u,26u}) values[i] = 0;
  values[PROFILE_PT_INDEX] = float(power); // mPt, registre 37
  values[15] = values[20] = float(apparent); // apparent A / total, VA
  values[21] = values[27] = float(importWh / 1000.0); // energie import A / total, kWh
  values[24] = values[28] = float(exportWh / 1000.0); // energie export A / total, kWh
  values[29] = values[32] = float(pf); // PF A / total, signe Shelly conserve
  values[33] = float(frequency); // mFreq, registre 67
  return true;
}
// END PROFILE_RESPONSE_HELPERS

bool profilePreviewAvailable = false, profilePreviewRoundtripOk = false;
uint8_t profilePlain[PROFILE_REPLY_BYTES] = {};
uint8_t profileCipher[PROFILE_CIPHER_BYTES] = {};
double profilePreviewPowerW = 0;
uint32_t profilePreviewReadingMs = 0;

bool buildProfileForPower(double outputW) {
  profilePreviewAvailable = false;
  profilePreviewRoundtripOk = false;
  memset(profilePlain, 0, sizeof(profilePlain));
  memset(profileCipher, 0, sizeof(profileCipher));
  float values[PROFILE_FLOAT_COUNT];
  if (!profilePreviewValues(selfTestOk && shellyMeasurementValid(), outputW, shellyVoltageV,
                            shellyCurrentA, shellyFrequencyHz, shellyApparentVA, shellyPowerFactor,
                            shellyImportWh, shellyExportWh, values, PROFILE_FLOAT_COUNT)) return false;
  if (profileEncodeReply(values, PROFILE_FLOAT_COUNT, profilePlain, sizeof(profilePlain)) != PROFILE_REPLY_BYTES)
    return false;
  uint8_t padded[PROFILE_CIPHER_BYTES], decoded[PROFILE_CIPHER_BYTES], iv[16];
  memcpy(padded, profilePlain, PROFILE_REPLY_BYTES);
  memset(padded + PROFILE_REPLY_BYTES, PROFILE_CIPHER_BYTES-PROFILE_REPLY_BYTES,
         PROFILE_CIPHER_BYTES-PROFILE_REPLY_BYTES);
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  memcpy(iv, PROFILE_IV, 16);
  int result = mbedtls_aes_setkey_enc(&ctx, PROFILE_KEY, 128);
  if (!result) result = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,
                     PROFILE_CIPHER_BYTES, iv, padded, profileCipher);
  if (!result) result = mbedtls_aes_setkey_dec(&ctx, PROFILE_KEY, 128);
  memcpy(iv, PROFILE_IV, 16);
  if (!result) result = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT,
                     PROFILE_CIPHER_BYTES, iv, profileCipher, decoded);
  mbedtls_aes_free(&ctx);
  profilePreviewRoundtripOk = !result && probeUnpad16(decoded, sizeof(decoded)) == PROFILE_REPLY_BYTES &&
      !memcmp(decoded, profilePlain, PROFILE_REPLY_BYTES) && profileReplyValid(decoded, PROFILE_REPLY_BYTES);
  profilePreviewAvailable = profilePreviewRoundtripOk;
  profilePreviewPowerW = outputW;
  profilePreviewReadingMs = shellyLastGoodMs;
  return profilePreviewAvailable;
}

bool buildProfilePreview() {
  return buildProfileForPower(shellyPowerW);
}

String profilePreviewJsonFields() {
  buildProfilePreview();
  String s = ",\"profile_preview_available\":"; s += profilePreviewAvailable ? "true" : "false";
  s += ",\"profile_preview_partial\":true,\"profile_preview_transmitted\":false";
  s += ",\"profile_candidate\":\"monophase_inf_kwh_v06\"";
  s += ",\"profile_layout_source\":\"C1.2.26_libmetertools\"";
  s += ",\"profile_ecu_acceptance_validated\":false";
  s += ",\"profile_preview_roundtrip_ok\":"; s += profilePreviewRoundtripOk ? "true" : "false";
  s += ",\"profile_preview_monophase_assumption\":true";
  s += ",\"profile_preview_unavailable_float_count\":19,\"profile_unavailable_encoding\":\"positive_infinity\"";
  s += ",\"profile_energy_unit\":\"kWh\",\"profile_zero_assumption_count\":12";
  s += ",\"profile_preview_pa_w\":"; s += profilePreviewAvailable ? shellyNumber(profilePreviewPowerW) : String("null");
  s += ",\"profile_preview_pt_w\":"; s += profilePreviewAvailable ? shellyNumber(profilePreviewPowerW) : String("null");
  s += ",\"profile_preview_shelly_age_ms\":";
  s += profilePreviewAvailable ? String(uint32_t(millis()-profilePreviewReadingMs)) : String("null");
  s += ",\"profile_preview_plain_bytes\":" + String(profilePreviewAvailable ? PROFILE_REPLY_BYTES : 0);
  s += ",\"profile_preview_cipher_bytes\":" + String(profilePreviewAvailable ? PROFILE_CIPHER_BYTES : 0);
  s += ",\"profile_preview_plain_hex\":\"";
  if (profilePreviewAvailable) s += hexOf(profilePlain, sizeof(profilePlain));
  s += "\",\"profile_preview_cipher_hex\":\"";
  if (profilePreviewAvailable) s += hexOf(profileCipher, sizeof(profileCipher));
  s += "\"";
  return s;
}

String profilePreviewHtml() {
  buildProfilePreview();
  String html = F("<section><h2>Réponse du compteur · contrôle local</h2>"
    "<p>Profil candidat reconstitué dans C1.2.26 : mesures Shelly en phase A et en total, énergies converties en kWh. Acceptation par l’ECU à vérifier.</p>");
  if (profilePreviewAvailable) {
    html += "<p>Puissance phase A : <strong>" + shellyNumber(profilePreviewPowerW) + " W</strong><br>";
    html += "Puissance totale : <strong>" + shellyNumber(profilePreviewPowerW) + " W</strong></p>";
    html += F("<p>Réponse RTU : 181 octets · réponse chiffrée : 192 octets.<br>"
      "Contrôle CRC et chiffrement / déchiffrement local : <strong>réussi</strong>.</p>");
  } else html += F("<p><strong>Aperçu indisponible</strong> : il faut une lecture Shelly récente avec puissance apparente, facteur de puissance et énergies, ainsi qu’un contrôle du chiffrement réussi.</p>");
  html += F("<p>Hypothèse du banc : phase A seule ; 12 flux B/C nuls. Les 19 champs indisponibles sont marqués +Inf, notamment la puissance réactive A/total et 10 champs non identifiés.</p>"
    "<p>Cet aperçu local peut différer de la dernière réponse réellement envoyée. Celle-ci est conservée séparément dans le diagnostic. L’acceptation par l’ECU reste à valider.</p>"
    "<p><a href='/diagnostic.json'>Télécharger la trame et le diagnostic</a></p></section>");
  return html;
}

// BEGIN TCP_PROFILE_HELPERS
struct ProfileStreamState {
  uint8_t request[16] = {};
  size_t used = 0;
  uint32_t lastByteMs = 0;
  unsigned accepted = 0, rejected = 0, bytesSent = 0;
  // 0 actif ; 1 requete invalide ; 2 source indisponible ; 3 delai ; 4 envoi partiel.
  unsigned fault = 0;
};

template<class Stream>
void serviceProfileStream(Stream& client, ProfileStreamState& state, bool fresh,
                          uint32_t now, const uint8_t* response, uint32_t idleTimeoutMs = 1500,
                          void (*onRequest)() = nullptr) {
  if (state.fault) return;
  // Un delai nul desactive uniquement le delai ENTRE requetes completes.
  // Un fragment abandonne reste borne, meme sur une connexion persistante.
  const uint32_t timeoutMs = state.used ? PROFILE_PARTIAL_TIMEOUT_MS : idleTimeoutMs;
  if (!client.available() && timeoutMs && uint32_t(now - state.lastByteMs) >= timeoutMs) {
    state.fault = 3; client.stop(); return;
  }
  // TCP est un flux : aucune hypothese sur la taille des paquets recus.
  unsigned budget = 64;
  while (client.available() && budget--) {
    const int byte = client.read();
    if (byte < 0) break;
    state.request[state.used++] = uint8_t(byte);
    state.lastByteMs = now;
    if (state.used != sizeof(state.request)) continue;
    state.used = 0;
    uint8_t plain[16];
    if (!decryptFirstBlock(state.request, plain) ||
        !probeIsExpectedRead(plain, probeUnpad16(plain, sizeof(plain)))) {
      ++state.rejected; state.fault = 1; client.stop(); return;
    }
    // La disponibilite du Shelly ne ferme pas une connexion ECU au repos.
    // Compter d'abord la demande valide, meme si aucune mesure ne peut etre envoyee.
    if (onRequest) onRequest();
    if (!fresh || !response) { state.fault = 2; client.stop(); return; }
    const size_t sent = client.write(response, PROFILE_CIPHER_BYTES);
    state.bytesSent += sent;
    if (sent != PROFILE_CIPHER_BYTES) { state.fault = 4; client.stop(); return; }
    ++state.accepted;
  }
}
// END TCP_PROFILE_HELPERS

// BEGIN TCP_BENCH_CORE
constexpr uint16_t BENCH_PORT = 9528;
// Lie exclusivement a la boucle locale, jamais annonce par mDNS.
WiFiServer benchServer(IPAddress(127, 0, 0, 1), BENCH_PORT, 1);
WiFiClient benchPeer, benchProbe;
ProfileStreamState benchStream;
const uint8_t BENCH_REQUEST[16] = {
  0xea,0x46,0x7a,0x65,0xfd,0x6d,0xe3,0x7f,0xf2,0xf5,0x6f,0xf7,0xc3,0x1f,0x1c,0x7f
};
uint8_t benchCipher[PROFILE_CIPHER_BYTES] = {};
uint8_t benchPlain[PROFILE_REPLY_BYTES] = {};
uint8_t benchCaptured[PROFILE_CIPHER_BYTES] = {};
uint8_t benchBadRequest[16] = {};
double benchSource[8] = {}; // P, U, I, f, S, PF, energie import/export Wh.
uint32_t benchStartedMs = 0, benchReadingMs = 0, benchStepMs = 0;
uint32_t benchAgeAtStart = 0, benchRuns = 0, benchDurationMs = 0;
unsigned benchPhase = 0, benchReceived = 0, benchStageBytes = 0;
bool benchAccepted = false, benchFragmentOk = false, benchSequenceOk = false, benchRejectOk = false;
const char* benchStatus = "pas_demande";

void finishTcpBench(const char* status) {
  benchProbe.stop(); benchPeer.stop(); benchServer.end();
  benchDurationMs = uint32_t(millis() - benchStartedMs);
  benchActive = false; benchStatus = status;
}

bool beginTcpBench() {
  if (benchActive || !shellyEnabled || !buildProfilePreview()) return false;
  benchProbe.stop(); benchPeer.stop(); benchServer.end();
  benchStartedMs = millis(); benchReadingMs = profilePreviewReadingMs;
  benchAgeAtStart = uint32_t(benchStartedMs - benchReadingMs);
  benchDurationMs = 0; ++benchRuns;
  benchStream = ProfileStreamState{}; benchStream.lastByteMs = benchStartedMs;
  benchPhase = benchReceived = benchStageBytes = 0;
  benchAccepted = benchFragmentOk = benchSequenceOk = benchRejectOk = false;
  memcpy(benchCipher, profileCipher, sizeof(benchCipher));
  memcpy(benchPlain, profilePlain, sizeof(benchPlain));
  const double source[] = {shellyPowerW, shellyVoltageV, shellyCurrentA, shellyFrequencyHz,
    shellyApparentVA, shellyPowerFactor, shellyImportWh, shellyExportWh};
  memcpy(benchSource, source, sizeof(benchSource));
  memset(benchCaptured, 0, sizeof(benchCaptured));
  // Padding valide, CRC RTU volontairement faux : le rejet doit venir du protocole.
  const uint8_t malformed[16] = {1,4,0,1,0,88,0xa1,0x30,8,8,8,8,8,8,8,8};
  uint8_t iv[16]; memcpy(iv, PROFILE_IV, 16);
  mbedtls_aes_context ctx; mbedtls_aes_init(&ctx);
  int result = mbedtls_aes_setkey_enc(&ctx, PROFILE_KEY, 128);
  if (!result) result = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, 16, iv, malformed, benchBadRequest);
  mbedtls_aes_free(&ctx);
  if (result) { finishTcpBench("erreur_chiffrement"); return false; }
  benchServer.begin(); benchServer.setNoDelay(true);
  benchActive = true; benchStatus = "en_cours";
  return true;
}

bool benchSend(const uint8_t* data, size_t count) {
  if (benchProbe.write(data, count) == count) return true;
  finishTcpBench("envoi_requete_incomplet"); return false;
}

void serviceTcpBench() {
  if (!benchActive) return;
  const uint32_t now = millis();
  const bool fresh = shellyEnabled && shellyMeasurementValid() &&
      uint32_t(now - benchReadingMs) < SHELLY_MAX_AGE_MS;
  if (!fresh) { finishTcpBench("mesure_indisponible"); return; }
  if (uint32_t(now - benchStartedMs) >= 5000) { finishTcpBench("delai_depasse"); return; }
  if (benchPhase == 0) {
    if (!benchProbe.connect(IPAddress(127,0,0,1), BENCH_PORT, 250)) {
      finishTcpBench("connexion_locale_impossible"); return;
    }
    benchProbe.setNoDelay(true); benchProbe.setTimeout(1);
    benchPhase = 1;
  }
  if (!benchAccepted) {
    WiFiClient incoming = benchServer.accept();
    if (incoming) {
      if (incoming.remoteIP() != IPAddress(127,0,0,1)) {
        incoming.stop(); finishTcpBench("adresse_locale_inattendue"); return;
      }
      benchPeer = incoming; benchPeer.setTimeout(1);
      benchAccepted = true; benchStream.lastByteMs = now;
    }
  }
  if (benchAccepted) serviceProfileStream(benchPeer, benchStream, fresh, now, benchCipher);
  if (benchStream.fault && !(benchPhase == 8 && benchStream.fault == 1)) {
    finishTcpBench("erreur_serveur"); return;
  }
  unsigned budget = 512;
  while (benchProbe.available() && budget--) {
    const int byte = benchProbe.read();
    if (byte < 0) break;
    if ((benchPhase != 4 && benchPhase != 6) ||
        uint8_t(byte) != benchCipher[benchStageBytes % PROFILE_CIPHER_BYTES]) {
      finishTcpBench("reponse_inattendue"); return;
    }
    if (benchPhase == 4 && benchStageBytes < PROFILE_CIPHER_BYTES)
      benchCaptured[benchStageBytes] = uint8_t(byte);
    ++benchStageBytes; ++benchReceived;
    const unsigned expected = benchPhase == 4 ? PROFILE_CIPHER_BYTES : 2*PROFILE_CIPHER_BYTES;
    if (benchStageBytes > expected) { finishTcpBench("reponse_trop_longue"); return; }
  }
  if (benchPhase == 1 && benchAccepted) {
    if (!benchSend(BENCH_REQUEST, 3)) return;
    benchPhase = 2; benchStepMs = now;
  } else if (benchPhase == 2 && uint32_t(now-benchStepMs) >= 50 && benchStream.used == 3) {
    if (benchStream.accepted || benchReceived) { finishTcpBench("reponse_prematuree"); return; }
    if (!benchSend(BENCH_REQUEST+3, 5)) return;
    benchPhase = 3; benchStepMs = now;
  } else if (benchPhase == 3 && uint32_t(now-benchStepMs) >= 50 && benchStream.used == 8) {
    if (benchStream.accepted || benchReceived) { finishTcpBench("reponse_prematuree"); return; }
    if (!benchSend(BENCH_REQUEST+8, 8)) return;
    benchPhase = 4; benchStageBytes = 0;
  } else if (benchPhase == 4 && benchStageBytes == PROFILE_CIPHER_BYTES) {
    if (benchStream.accepted != 1) { finishTcpBench("compte_reponses_invalide"); return; }
    benchFragmentOk = true;
    uint8_t pair[32]; memcpy(pair, BENCH_REQUEST, 16); memcpy(pair+16, BENCH_REQUEST, 16);
    if (!benchSend(pair, sizeof(pair))) return;
    benchPhase = 6; benchStageBytes = 0;
  } else if (benchPhase == 6 && benchStageBytes == 2*PROFILE_CIPHER_BYTES) {
    if (benchStream.accepted != 3) { finishTcpBench("compte_reponses_invalide"); return; }
    benchSequenceOk = true;
    if (!benchSend(benchBadRequest, sizeof(benchBadRequest))) return;
    benchPhase = 8;
  } else if (benchPhase == 8 && benchStream.fault == 1 && !benchProbe.connected()) {
    benchRejectOk = benchStream.rejected == 1 && benchStream.accepted == 3 &&
        benchStream.bytesSent == 3*PROFILE_CIPHER_BYTES && benchReceived == 3*PROFILE_CIPHER_BYTES;
    finishTcpBench(benchRejectOk ? "reussi" : "rejet_non_confirme");
  }
}
// END TCP_BENCH_CORE

void handleTcpBenchRequest() {
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("token") || web.arg("token") != formToken) {
    web.send(400, "text/plain; charset=utf-8", "Recharge la page avant un nouveau test."); return;
  }
  if (switchJobActive || liveEnabled || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || shellyActive) {
    web.send(409, "text/plain; charset=utf-8", "Une opération est en cours. Réessaie après sa fin."); return;
  }
  if (!beginTcpBench()) {
    web.send(409, "text/plain; charset=utf-8", "Démarre la lecture Shelly et attends une mesure complète et récente."); return;
  }
  renewFormToken(); web.sendHeader("Location", "/");
  web.send(303, "text/plain", "Test du dialogue lance.");
}

String tcpBenchJsonFields() {
  String s = ",\"local_tcp_status\":\"" + String(benchStatus) + "\"";
  s += ",\"local_tcp_running\":"; s += benchActive ? "true" : "false";
  s += ",\"local_tcp_endpoint\":\"127.0.0.1:9528\",\"local_tcp_runs\":" + String(benchRuns);
  s += ",\"local_tcp_duration_ms\":" + String(benchActive ? uint32_t(millis()-benchStartedMs) : benchDurationMs);
  s += ",\"local_tcp_snapshot_age_at_start_ms\":" + String(benchAgeAtStart);
  s += ",\"local_tcp_fragment_ok\":"; s += benchFragmentOk ? "true" : "false";
  s += ",\"local_tcp_sequence_ok\":"; s += benchSequenceOk ? "true" : "false";
  s += ",\"local_tcp_bad_crc_rejected\":"; s += benchRejectOk ? "true" : "false";
  s += ",\"local_tcp_replies\":" + String(benchStream.accepted);
  s += ",\"local_tcp_rejected_requests\":" + String(benchStream.rejected);
  s += ",\"local_tcp_bytes_sent\":" + String(benchStream.bytesSent);
  s += ",\"local_tcp_bytes_received\":" + String(benchReceived);
  s += ",\"local_tcp_server_fault\":" + String(benchStream.fault);
  s += ",\"local_tcp_snapshot_shelly\":";
  if (benchRuns) {
    const char* names[] = {"power_w", "voltage_v", "current_a", "frequency_hz", "apparent_va",
      "power_factor", "import_wh", "export_wh"};
    s += "{";
    for (unsigned i = 0; i < 8; ++i) {
      if (i) s += ",";
      s += "\"" + String(names[i]) + "\":" + shellyNumber(benchSource[i], 6);
    }
    s += "}";
  } else s += "null";
  s += ",\"local_tcp_snapshot_plain_hex\":\"";
  if (benchRuns) s += hexOf(benchPlain, sizeof(benchPlain));
  s += "\",\"local_tcp_first_reply_cipher_hex\":\"";
  if (benchFragmentOk) s += hexOf(benchCaptured, sizeof(benchCaptured));
  s += "\"";
  return s;
}

String tcpBenchHtml() {
  buildProfilePreview();
  String html = F("<section><h2>Tester le dialogue compteur</h2>"
    "<p>Le banc v0.6 a déjà réussi sur ta carte. Ce contrôle local reste disponible ; il n’est pas nécessaire de le recommencer.</p>");
  html += "<p>Résultat : <strong>" + String(benchStatus) + "</strong><br>";
  html += "Réponses émises : " + String(benchStream.accepted) + " / 3 · octets reçus : " + String(benchReceived) + "<br>";
  html += "Requête corrompue rejetée : " + String(benchRejectOk ? "oui" : "pas encore validé") + "</p>";
  html += "<form method='post' action='/tcp-bench'><input type='hidden' name='token' value='" + formToken + "'>";
  html += "<button";
  if (switchJobActive || liveEnabled || sourceJobActive || benchActive || !shellyEnabled || !profilePreviewAvailable || configJobActive || ecuPending || ecuActive || searchPending || searchActive) html += " disabled";
  html += F(">Tester le dialogue compteur</button></form>"
    "<p>Test local de 5 secondes maximum. Il ne valide pas encore la lecture par l’ECU ni la régulation. Garde les tores configurés.</p>"
    "<p><a href='/diagnostic.json'>Télécharger le diagnostic v0.8.8</a></p></section>");
  return html;
}


String shellyIoEventJson(const ShellyIoEvent& event) {
  String s = "{\"uptime_ms\":" + String(event.atMs);
  s += ",\"query\":" + String(event.query) + ",\"attempt\":" + String(event.attempt);
  s += ",\"stage\":\"" + String(event.stage) + "\",\"result\":\"" + String(event.result) + "\"";
  s += ",\"duration_ms\":" + String(event.durationMs);
  s += ",\"socket_errno\":" + String(event.socketError);
  s += ",\"http_code\":" + String(event.httpCode) + ",\"body_bytes\":" + String(event.bodyBytes) + "}";
  return s;
}

String shellyConnectionJsonFields() {
  String s=",\"shelly_transport\":\"websocket\",\"shelly_connect_nonblocking\":true";
  s+=",\"shelly_ws_connected\":"; s+=shellyWs.phase==ShellyWebSocket::OPEN?"true":"false";
  s+=",\"shelly_query_running\":"; s+=shellyActive?"true":"false";
  s+=",\"shelly_poll_ms\":"+String(SHELLY_POLL_MS);
  s+=",\"shelly_rpc_timeout_ms\":"+String(SHELLY_TIMEOUT_MS);
  s+=",\"shelly_connect_attempts\":"+String(shellyConnectAttempts);
  s+=",\"shelly_connect_failures\":"+String(shellyConnectFailures);
  s+=",\"shelly_ws_sessions\":"+String(shellyWsSessions);
  s+=",\"shelly_query_failures\":"+String(shellyQueryFailures);
  s+=",\"shelly_query_cancelled\":"+String(shellyQueryCancelled);
  s+=",\"shelly_ws_notifications\":"+String(shellyWsNotifications);
  s+=",\"shelly_partial_notifications_refresh_measurement\":false";
  s+=",\"shelly_unexpected_replies\":"+String(shellyUnexpectedReplies);
  s+=",\"shelly_reconnect_wait_ms\":"+String(shellyRetryWaitMs);
  s+=",\"shelly_last_connect_duration_ms\":"+String(shellyLastConnectMs);
  s+=",\"shelly_max_service_ms\":"+String(shellyMaxServiceMs);
  s+=",\"shelly_max_service_gap_ms\":"+String(shellyMaxServiceGapMs);
  s+=",\"shelly_last_error\":";
  s+=shellyHasError?shellyIoEventJson(shellyLastError):String("null");
  s+=",\"shelly_events_dropped\":"+String(shellyEventsDropped);
  s+=",\"shelly_events\":[";
  for (unsigned i=0;i<shellyEventCount;++i) {
    const unsigned index=(shellyEventNext+SHELLY_EVENT_CAPACITY-shellyEventCount+i)%SHELLY_EVENT_CAPACITY;
    if (i) s+=",";
    s+=shellyIoEventJson(shellyEvents[index]);
  }
  s+="]"; return s;
}

String shellyJsonFields() {
  const bool valid = shellyMeasurementValid();
  String s = ",\"shelly_ip\":\"" + String(SHELLY_IP_TEXT) + "\"";
  s += ",\"shelly_expected_mac\":\"" + String(SHELLY_MAC) + "\"";
  s += ",\"shelly_channel\":" + String(SHELLY_CHANNEL);
  s += ",\"shelly_enabled\":"; s += shellyEnabled ? "true" : "false";
  s += ",\"shelly_query_status\":\"" + String(shellyStatus) + "\"";
  s += ",\"shelly_query_count\":" + String(shellyQueryCount);
  s += ",\"shelly_success_count\":" + String(shellySuccessCount);
  s += ",\"shelly_http_code\":" + String(shellyHttpCode);
  s += ",\"shelly_response_bytes\":" + String(shellyBodySize);
  s += ",\"shelly_measurement_valid\":"; s += valid ? "true" : "false";
  s += ",\"shelly_age_ms\":";
  s += shellyHasReading ? String(uint32_t(millis() - shellyLastGoodMs)) : String("null");
  s += ",\"shelly_power_w\":"; s += valid ? shellyNumber(shellyPowerW) : String("null");
  s += ",\"shelly_voltage_v\":"; s += valid ? shellyNumber(shellyVoltageV) : String("null");
  s += ",\"shelly_current_a\":"; s += valid ? shellyNumber(shellyCurrentA, 3) : String("null");
  s += ",\"shelly_frequency_hz\":"; s += valid ? shellyNumber(shellyFrequencyHz) : String("null");
  s += ",\"shelly_apparent_power_va\":";
  s += valid && isfinite(shellyApparentVA) ? shellyNumber(shellyApparentVA, 2) : String("null");
  s += ",\"shelly_power_factor\":";
  s += valid && isfinite(shellyPowerFactor) ? shellyNumber(shellyPowerFactor, 3) : String("null");
  s += ",\"shelly_import_energy_wh\":";
  s += valid && isfinite(shellyImportWh) ? shellyNumber(shellyImportWh, 3) : String("null");
  s += ",\"shelly_export_energy_wh\":";
  s += valid && isfinite(shellyExportWh) ? shellyNumber(shellyExportWh, 3) : String("null");
  s += ",\"shelly_direction\":\"";
  s += !valid ? "indisponible" : (shellyPowerW < 0 ? "injection" : (shellyPowerW > 0 ? "soutirage" : "nul"));
  s += "\",\"shelly_sign_convention\":\"negatif_injection_positif_soutirage\"";
  s += ",\"remote_meter_configured\":false,\"aggregate_power_w\":null";
  s += shellyConnectionJsonFields();
  return s;
}

String shellyHtml() {
  const bool valid = shellyMeasurementValid();
  String html = F("<section><h2>Mesure réseau de la maison</h2>"
    "<p>Shelly Pro EM 50 · <code>IP_SHELLY</code> · canal API <code>1</code></p>"
    "<p>Lecture toutes les 5 secondes après démarrage. Négatif = injection ; positif = soutirage.</p>");
  html += "<form action='/shelly-read' method='post'><input type='hidden' name='token' value='" + formToken + "'>";
  html += "<input type='hidden' name='action' value='" + String(shellyEnabled ? "stop" : "start") + "'>";
  html += "<button type='submit'>" + String(shellyEnabled ? "Arrêter la lecture Shelly" : "Démarrer la lecture Shelly") + "</button></form>";
  html += "<p>" + String(shellyStatusLabel()) + "</p>";
  if (valid) {
    html += "<p style='font-size:28px'><strong>" + shellyNumber(shellyPowerW) + " W</strong></p>";
    html += "<p>" + String(shellyPowerW < 0 ? "Injection" : (shellyPowerW > 0 ? "Soutirage" : "Puissance nulle"));
    html += " · " + shellyNumber(shellyVoltageV) + " V · " + shellyNumber(shellyCurrentA, 3) + " A</p>";
  } else html += F("<p><strong>Mesure indisponible</strong></p>");
  html += F("<p>Valeurs complémentaires : ");
  html += valid && isfinite(shellyApparentVA) ? shellyNumber(shellyApparentVA) + " VA" : String("VA indisponibles");
  html += " · facteur de puissance ";
  html += valid && isfinite(shellyPowerFactor) ? shellyNumber(shellyPowerFactor, 3) : String("indisponible");
  html += "<br>Énergie soutirée : ";
  html += valid && isfinite(shellyImportWh) ? shellyNumber(shellyImportWh, 3) + " Wh" : String("indisponible");
  html += " · injectée : ";
  html += valid && isfinite(shellyExportWh) ? shellyNumber(shellyExportWh, 3) + " Wh" : String("indisponible");
  html += "</p>";
  if (shellyHasReading)
    html += "<p>Dernière mesure valide reçue il y a " + String(uint32_t(millis()-shellyLastGoodMs)/1000) + " s.</p>";
  html += "<p>Lectures réussies : " + String(shellySuccessCount) + " / " + String(shellyQueryCount) + "</p>";
  html += "<p>Connexion permanente : " + String(shellyWs.phase==ShellyWebSocket::OPEN ? "ouverte" : "en attente") +
    " · ouvertures réussies : " + String(shellyWsSessions) + ".</p>";
  if (shellyHasError) {
    html += "<p>Dernière erreur conservée : <code>" + String(shellyLastError.result) +
      "</code>, il y a " + String(uint32_t(millis()-shellyLastError.atMs)/1000) + " s.</p>";
  }
  html += F("<p>Un relevé complet est demandé toutes les deux secondes sur la même connexion. Le diagnostic garde les erreurs. Une mesure âgée de 15 secondes ou une lecture échouée reste indisponible.</p>");
  html += F("<p>Autre maison : mesure pas encore configurée. Aucune somme disponible.</p>"
    "<p>Le répondeur réseau ci-dessous peut utiliser ces mesures après activation. La sélection dans l’ECU reste à valider.</p>"
    "<p><a href='/diagnostic.json'>Télécharger le diagnostic</a></p></section>");
  return html;
}


// BEGIN CONFIG_JSON_HELPERS
// -1 : schema inexploitable ; 0/1 : identifiant absent/present dans une liste valide.
int meterConfigContainsMeter(const char* data, size_t size, const char* wantedId) {
  if (!data || !wantedId || !*wantedId) return -1;
  ShellyJson json(data, size);
  ShellyJson::Value meters{};
  if (!json.valid() || ShellyJson::field(json.root, "meterInfo", meters) != 1) return -1;
  const char* p = meters.first;
  if (p == meters.last || *p++ != '[') return -1;
  ShellyJson::spaces(p, meters.last);
  if (p < meters.last && *p == ']') return 0;
  int matches = 0;
  while (p < meters.last) {
    ShellyJson::spaces(p, meters.last);
    ShellyJson::Value item{p, p}, mdns{}, id{};
    if (!ShellyJson::skip(p, meters.last)) return -1;
    item.last = p;
    if (ShellyJson::field(item, "mdnsMeter", mdns) != 1 ||
        ShellyJson::field(mdns, "id", id) != 1 ||
        id.last - id.first < 3 || *id.first != '"') return -1;
    if (ShellyJson::stringEquals(id, wantedId) && ++matches > 1) return -1;
    ShellyJson::spaces(p, meters.last);
    if (p == meters.last) return -1;
    if (*p++ == ']') return matches;
    if (p[-1] != ',') return -1;
  }
  return -1;
}
// END CONFIG_JSON_HELPERS

const char* configStatusLabel() {
  if (!strcmp(configQueryStatus, "pas_demande")) return "Lecture pas encore demandée";
  if (!strcmp(configQueryStatus, "identification")) return "Vérification de l’identité et de la version de l’ECU";
  if (!strcmp(configQueryStatus, "temporisation")) return "ECU reconnu ; attente avant la lecture de la liste";
  if (!strcmp(configQueryStatus, "lecture")) return "Lecture de la liste en cours";
  if (!strcmp(configQueryStatus, "reponse_recue")) return "Liste reçue et analysée";
  if (!strcmp(configQueryStatus, "liste_non_retournee")) return "L’ECU n’a joint aucune liste à sa réponse";
  if (!strcmp(configQueryStatus, "schema_inconnu")) return "Liste reçue ; contenu à examiner dans le diagnostic";
  if (!strcmp(configQueryStatus, "identite_inattendue")) return "Identifiant ou version différent ; lecture de la liste non envoyée";
  if (!strcmp(configQueryStatus, "identification_echouee")) return "Identification impossible ; lecture de la liste non envoyée";
  if (!strcmp(configQueryStatus, "connexion_impossible")) return "Connexion à l’ECU impossible";
  if (!strcmp(configQueryStatus, "envoi_incomplet")) return "Envoi incomplet ; aucune relance automatique";
  if (!strcmp(configQueryStatus, "connexion_fermee")) return "Connexion fermée avant la fin de la réponse";
  if (!strcmp(configQueryStatus, "delai_depasse")) return "Délai dépassé ; conserver le diagnostic";
  if (!strcmp(configQueryStatus, "wifi_perdu")) return "Wi-Fi perdu ; opération arrêtée";
  if (!strcmp(configQueryStatus, "reponse_trop_grande")) return "Réponse trop grande ; capture limitée";
  if (!strcmp(configQueryStatus, "refus_ecu")) return "L’ECU a renvoyé un code d’erreur";
  if (!strcmp(configQueryStatus, "json_invalide")) return "Réponse complète mais liste illisible";
  return "Format inattendu ; conserver le diagnostic";
}

// BEGIN CONFIG_SERVICE
void finishMeterConfigRead(const char* result) {
  configClient.stop();
  configActive = configAwaitIdentity = configJobActive = false;
  configResponseComplete = meterConfigReadEnvelopeValid(configResponse, configResponseSize);
  configPayloadLength = meterConfigReadPayloadLength(configResponse, configResponseSize);
  configReplyCode[0] = '\0';
  configJsonValid = false;
  configEspListed = -1;
  if (configResponseComplete) {
    memcpy(configReplyCode, configResponse + 15, 2);
    configReplyCode[2] = '\0';
    if (configPayloadLength > 0) {
      const char* payload = reinterpret_cast<const char*>(configResponse + 21);
      configJsonValid = ShellyJson(payload, size_t(configPayloadLength)).valid();
      if (configJsonValid)
        configEspListed = meterConfigContainsMeter(payload, size_t(configPayloadLength), meterId.c_str());
    }
  }
  configQueryStatus = result;
  if (!strcmp(result, "reponse_recue")) {
    if (strcmp(configReplyCode, "00")) configQueryStatus = "refus_ecu";
    else if (configPayloadLength == 0) configQueryStatus = "liste_non_retournee";
    else if (!configJsonValid) configQueryStatus = "json_invalide";
    else if (configEspListed < 0) configQueryStatus = "schema_inconnu";
  }
  Serial.print("Liste des compteurs : "); Serial.println(configStatusLabel());
}

void serviceMeterConfigRead() {
  if (!configJobActive) return;
  if (WiFi.status() != WL_CONNECTED) { finishMeterConfigRead("wifi_perdu"); return; }
  if (configAwaitIdentity) {
    if (ecuPending || ecuActive) return;
    if (strcmp(ecuQueryStatus, "reponse_recue")) {
      finishMeterConfigRead("identification_echouee"); return;
    }
    if (!ecuIdentityUsable) { finishMeterConfigRead("identite_inattendue"); return; }
    configAwaitIdentity = false;
    configQueryStatus = "temporisation";
  }
  if (!configActive) {
    if (ecuWaitSeconds()) return;
    if (!ecuIdentityUsable || !ecuAddressIsLocalUnicast(ecuTarget) ||
        !buildMeterConfigReadRequest(configRequest, sizeof(configRequest), ecuUid)) {
      finishMeterConfigRead("identite_inattendue"); return;
    }
    pauseShellyForEcu();
    configActive = true;
    configQueryStartedMs = millis();
    lastEcuActionMs = configQueryStartedMs;
    configQueryStatus = "lecture";
    configClient.stop();
    if (!configClient.connect(ecuTarget, ECU_PORT, 1500)) {
      finishMeterConfigRead("connexion_impossible"); return;
    }
    configTcpConnected = true;
    configClient.setTimeout(1);
    configCommandBytesSent = configClient.write(
        reinterpret_cast<const uint8_t*>(configRequest), 30);
    if (configCommandBytesSent != 30) { finishMeterConfigRead("envoi_incomplet"); return; }
  }
  unsigned budget = 1024;
  while (configClient.available() && budget--) {
    const int value = configClient.read();
    if (value < 0) break;
    if (configResponseSize == ECU_RESPONSE_CAPACITY) {
      configResponseTruncated = true;
      finishMeterConfigRead("reponse_trop_grande"); return;
    }
    configResponse[configResponseSize++] = uint8_t(value);
    const int expected = ecuDeclaredLength(configResponse, configResponseSize);
    if (expected < 0) { finishMeterConfigRead("format_inattendu"); return; }
    if (expected > int(ECU_RESPONSE_CAPACITY)) {
      configResponseTruncated = true;
      finishMeterConfigRead("reponse_trop_grande"); return;
    }
    if (expected > 0 && configResponseSize == size_t(expected)) {
      finishMeterConfigRead(meterConfigReadEnvelopeValid(configResponse, configResponseSize)
          ? "reponse_recue" : "format_inattendu");
      return;
    }
  }
  if (!configClient.connected() && !configClient.available()) {
    finishMeterConfigRead("connexion_fermee"); return;
  }
  if (uint32_t(millis() - configQueryStartedMs) >= ECU_REPLY_TIMEOUT_MS)
    finishMeterConfigRead("delai_depasse");
}
// END CONFIG_SERVICE

void handleMeterConfigReadRequest() {
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("token") || web.arg("token") != formToken) {
    web.send(400, "text/plain; charset=utf-8", "Recharge la page avant une nouvelle lecture."); return;
  }
  if (switchJobActive || liveEnabled || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) {
    web.send(429, "text/plain; charset=utf-8", "Une opération est déjà en cours, ou le délai entre deux essais n’est pas terminé."); return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    web.send(409, "text/plain; charset=utf-8", "Wi-Fi déconnecté."); return;
  }
  IPAddress target;
  String address = web.arg("ecu_ip");
  address.trim();
  if (!target.fromString(address) || !ecuAddressIsLocalUnicast(target)) {
    web.send(400, "text/plain; charset=utf-8", "Indique l’adresse IPv4 locale de l’ECU."); return;
  }
  configClient.stop();
  configRequest[0] = configReplyCode[0] = '\0';
  configResponseSize = configCommandBytesSent = 0;
  configTcpConnected = configResponseComplete = configJsonValid = configResponseTruncated = false;
  configPayloadLength = configEspListed = -1;
  configActive = false;
  configTargetText = target.toString();
  ++configQueryCount;
  configQueryStatus = "identification";
  configJobActive = configAwaitIdentity = true;
  startEcuInfoQuery(target);
  renewFormToken();
  web.sendHeader("Location", "/");
  web.send(303, "text/plain", "Lecture de la liste demandee.");
}

String meterConfigJsonFields() {
  String s = ",\"config_read_command\":\"0071\",\"config_read_operation\":\"01\"";
  s += ",\"config_read_status\":\"" + String(configQueryStatus) + "\"";
  s += ",\"config_read_ip\":\"" + configTargetText + "\"";
  s += ",\"config_read_count\":" + String(configQueryCount);
  s += ",\"config_read_running\":"; s += configJobActive ? "true" : "false";
  s += ",\"config_tcp_connected\":"; s += configTcpConnected ? "true" : "false";
  s += ",\"config_request_ascii\":\"" + String(configRequest) + "\"";
  s += ",\"config_command_bytes_sent\":" + String(configCommandBytesSent);
  s += ",\"config_response_bytes\":" + String(configResponseSize);
  s += ",\"config_response_declared_length\":" + String(ecuDeclaredLength(configResponse, configResponseSize));
  s += ",\"config_response_complete\":"; s += configResponseComplete ? "true" : "false";
  s += ",\"config_response_truncated\":"; s += configResponseTruncated ? "true" : "false";
  s += ",\"config_reply_code\":\"" + String(configReplyCode) + "\"";
  s += ",\"config_payload_bytes\":" + String(configPayloadLength);
  s += ",\"config_json_valid\":"; s += configJsonValid ? "true" : "false";
  s += ",\"config_esp32_listed\":";
  s += configEspListed < 0 ? "null" : (configEspListed ? "true" : "false");
  s += ",\"config_response_hex\":\"" + hexOf(configResponse, configResponseSize) + "\"";
  s += ",\"config_payload_json\":";
  if (configJsonValid) {
    for (int i = 0; i < configPayloadLength; ++i) s += char(configResponse[21 + i]);
  } else s += "null";
  s += ",\"ecu_configuration_write_sent\":false";
  return s;
}

String meterConfigHtml() {
  String html = F("<section><h2>Lire les compteurs enregistrés</h2>"
    "<p>Cette lecture vérifie si l’ESP32 figure dans la liste enregistrée par l’ECU. "
    "La découverte réseau a déjà réussi.</p>"
    "<p>Un clic lance l’identification, attend une minute, puis lit la liste. "
    "Garde cette page ouverte jusqu’au résultat.</p>"
    "<form action='/meter-config-read' method='post'><label for='config_ip'>Adresse IP de l’ECU</label> "
    "<input id='config_ip' name='ecu_ip' inputmode='decimal' required value='");
  html += ecuTargetText + "'><input type='hidden' name='token' value='" + formToken + "'>";
  html += "<p><button type='submit'";
  if (switchJobActive || liveEnabled || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) html += " disabled";
  html += F(">Lire les compteurs enregistrés</button></p></form>");
  html += "<p><strong>" + String(configStatusLabel()) + "</strong></p>";
  if (configJobActive && !configAwaitIdentity && !configActive)
    html += "<p>Lecture automatique dans " + String(ecuWaitSeconds()) + " s.</p>";
  if (!configJobActive && ecuWaitSeconds())
    html += "<p>Nouvel essai possible dans " + String(ecuWaitSeconds()) + " s.</p>";
  if (configEspListed >= 0)
    html += "<p>ESP32 présent dans la liste reçue : <strong>" + String(configEspListed ? "oui" : "non") + "</strong>.</p>";
  if (configPayloadLength == 0)
    html += F("<p>Une réponse sans liste peut correspondre à un fichier absent, vide ou illisible. Elle ne suffit pas à conclure.</p>");
  if (configReplyCode[0])
    html += "<p>Code ECU : <code>" + String(configReplyCode) + "</code> · Réponse : " + String(configResponseSize) + " octets.</p>";
  html += F("<p>Un compteur présent dans cette liste n’est pas nécessairement utilisé pour la régulation.</p>"
    "<p><a href='/diagnostic.json'>Télécharger le diagnostic v0.8.8</a></p></section>");
  return html;
}

// BEGIN SOURCE_READ_HELPERS
bool buildSourceReadRequest(char* output, size_t capacity, const char* uid) {
  if (!output) return false;
  if (capacity) output[0] = '\0';
  if (!uid || capacity < 29 || strlen(uid) != 12) return false;
  for (size_t i = 0; i < 12; ++i) if (uid[i] < '0' || uid[i] > '9') return false;
  memcpy(output, "APS1100280072", 13);
  memcpy(output + 13, uid, 12);
  memcpy(output + 25, "END", 3); output[28] = '\0';
  return true;
}

int sourceReadPayloadLength(const uint8_t* data, size_t size) {
  if (!data || size < 22 || ecuDeclaredLength(data, size) != int(size) ||
      memcmp(data + 9, "0072", 4) || memcmp(data + size - 3, "END", 3)) return -1;
  for (size_t i = 13; i < 19; ++i) if (data[i] < '0' || data[i] > '9') return -1;
  size_t n = 0;
  for (size_t i = 15; i < 19; ++i) n = n * 10 + size_t(data[i] - '0');
  return n + 22 == size ? int(n) : -1;
}

struct MeterSourceSelection {
  // -1 signifie inconnu, jamais "desactive" ou "absent" par defaut.
  int enabled = -1, type = -1, espId = -1;
};

MeterSourceSelection parseSourceSelection(const char* data, size_t size,
                                         const char* role, const char* wantedId) {
  MeterSourceSelection out;
  if (!data || !role || !wantedId) return out;
  ShellyJson json(data, size);
  ShellyJson::Value meter{}, value{}, info{}, id{};
  if (!json.valid() || ShellyJson::field(json.root, role, meter) != 1) return out;
  if (ShellyJson::field(meter, "meterEnable", value) == 1) {
    if (ShellyJson::stringEquals(value, "0")) out.enabled = 0;
    if (ShellyJson::stringEquals(value, "1")) out.enabled = 1;
  }
  if (ShellyJson::field(meter, "meterType", value) == 1) {
    for (int t = 1; t <= 4; ++t) {
      char code[] = {char('0' + t), '\0'};
      if (ShellyJson::stringEquals(value, code)) out.type = t;
    }
  }
  if (out.type == 3 && ShellyJson::field(meter, "meterInfo3", info) == 1 &&
      ShellyJson::field(info, "id", id) == 1 && id.last > id.first && *id.first == '"')
    out.espId = ShellyJson::stringEquals(id, wantedId) ? 1 : 0;
  return out;
}
// END SOURCE_READ_HELPERS

MeterSourceSelection sourceGrid, sourcePV;

// BEGIN SOURCE_READ_SERVICE
void finishSourceRead(const char* result) {
  sourceClient.stop();
  sourceActive = sourceAwaitIdentity = sourceJobActive = false;
  sourcePayloadLength = sourceReadPayloadLength(sourceResponse, sourceResponseSize);
  sourceResponseComplete = sourcePayloadLength >= 0;
  sourceJsonValid = false; sourceReplyCode[0] = '\0';
  sourceGrid = MeterSourceSelection{}; sourcePV = MeterSourceSelection{};
  if (sourceResponseComplete) {
    memcpy(sourceReplyCode, sourceResponse + 13, 2); sourceReplyCode[2] = '\0';
    const char* payload = reinterpret_cast<const char*>(sourceResponse + 19);
    sourceJsonValid = sourcePayloadLength > 0 && ShellyJson(payload, size_t(sourcePayloadLength)).valid();
    if (sourceJsonValid && !strcmp(sourceReplyCode, "00")) {
      sourceGrid = parseSourceSelection(payload, size_t(sourcePayloadLength), "meterGrid", meterId.c_str());
      sourcePV = parseSourceSelection(payload, size_t(sourcePayloadLength), "meterPV", meterId.c_str());
      reconcileSourceSwitch(payload, size_t(sourcePayloadLength));
    }
  }
  sourceStatus = result;
  if (!strcmp(result, "reponse_recue")) {
    if (strcmp(sourceReplyCode, "00")) sourceStatus = "refus_ecu";
    else if (!sourceJsonValid) sourceStatus = "json_absent_ou_invalide";
  }
  Serial.print("Choix de compteur 0072 : "); Serial.println(sourceStatus);
}

void serviceSourceRead() {
  if (!sourceJobActive) return;
  if (WiFi.status() != WL_CONNECTED) { finishSourceRead("wifi_perdu"); return; }
  if (sourceAwaitIdentity) {
    if (ecuPending || ecuActive) return;
    if (strcmp(ecuQueryStatus, "reponse_recue")) { finishSourceRead("identification_echouee"); return; }
    if (!ecuIdentityUsable) { finishSourceRead("identite_inattendue"); return; }
    sourceAwaitIdentity = false; sourceStatus = "temporisation";
  }
  if (!sourceActive) {
    if (ecuWaitSeconds()) return;
    if (sourceProbeAttempted) { finishSourceRead("deja_tente"); return; }
    if (!ecuIdentityUsable || !ecuAddressIsLocalUnicast(ecuTarget) ||
        !buildSourceReadRequest(sourceRequest, sizeof(sourceRequest), ecuUid)) {
      finishSourceRead("identite_inattendue"); return;
    }
    sourceStartedMs = millis(); lastEcuActionMs = sourceStartedMs;
    sourceActive = true; sourceStatus = "lecture";
    pauseShellyForEcu();
    sourceClient.stop();
    if (!sourceClient.connect(ecuTarget, ECU_PORT, 1500)) {
      finishSourceRead("connexion_impossible"); return;
    }
    sourceTcpConnected = true; sourceProbeAttempted = true;
    sourceClient.setTimeout(1);
    sourceCommandBytesSent = sourceClient.write(reinterpret_cast<const uint8_t*>(sourceRequest), 28);
    if (sourceCommandBytesSent != 28) { finishSourceRead("envoi_incomplet"); return; }
  }
  unsigned budget = 1024;
  while (sourceClient.available() && budget--) {
    const int value = sourceClient.read();
    if (value < 0) break;
    if (sourceResponseSize == ECU_RESPONSE_CAPACITY) {
      sourceTruncated = true; finishSourceRead("reponse_trop_grande"); return;
    }
    sourceResponse[sourceResponseSize++] = uint8_t(value);
    const int expected = ecuDeclaredLength(sourceResponse, sourceResponseSize);
    if (expected < 0) { finishSourceRead("format_inattendu"); return; }
    if (expected > int(ECU_RESPONSE_CAPACITY)) {
      sourceTruncated = true; finishSourceRead("reponse_trop_grande"); return;
    }
    if (expected > 0 && sourceResponseSize == size_t(expected)) {
      finishSourceRead(sourceReadPayloadLength(sourceResponse, sourceResponseSize) >= 0
          ? "reponse_recue" : "format_inattendu"); return;
    }
  }
  if (!sourceClient.connected() && !sourceClient.available()) { finishSourceRead("connexion_fermee"); return; }
  if (uint32_t(millis() - sourceStartedMs) >= SOURCE_READ_TIMEOUT_MS) finishSourceRead("delai_depasse");
}
// END SOURCE_READ_SERVICE

void handleSourceReadRequest() {
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("token") || web.arg("token") != formToken) {
    web.send(400, "text/plain; charset=utf-8", "Recharge la page avant une nouvelle action."); return;
  }
  if (switchJobActive || (liveEnabled && !switchMayUseEsp()) || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) {
    web.send(429, "text/plain; charset=utf-8", "Arrête le répondeur et attends la fin de l’opération en cours."); return;
  }
  if (WiFi.status() != WL_CONNECTED) { web.send(409, "text/plain; charset=utf-8", "Wi-Fi déconnecté."); return; }
  IPAddress target; String address = web.arg("ecu_ip"); address.trim();
  if (!target.fromString(address) || !ecuAddressIsLocalUnicast(target)) {
    web.send(400, "text/plain; charset=utf-8", "Indique l’adresse locale de l’ECU."); return;
  }
  sourceProbeAttempted = false;
  sourceClient.stop(); sourceRequest[0] = sourceReplyCode[0] = '\0';
  sourceResponseSize = sourceCommandBytesSent = 0;
  sourceTcpConnected = sourceResponseComplete = sourceJsonValid = sourceTruncated = sourceActive = false;
  sourcePayloadLength = -1; sourceGrid = MeterSourceSelection{}; sourcePV = MeterSourceSelection{};
  sourceTargetText = target.toString(); ++sourceQueryCount;
  sourceJobActive = sourceAwaitIdentity = true; sourceStatus = "identification";
  startEcuInfoQuery(target);
  renewFormToken(); web.sendHeader("Location", "/"); web.send(303, "text/plain", "Releve du choix de compteur demande.");
}

String sourceReadJsonFields() {
  String s = ",\"source_read_command\":\"0072\",\"source_read_may_restart_ecu\":true";
  s += ",\"source_read_status\":\"" + String(sourceStatus) + "\"";
  s += ",\"source_read_ip\":\"" + sourceTargetText + "\"";
  s += ",\"source_read_count\":" + String(sourceQueryCount);
  s += ",\"source_read_running\":"; s += sourceJobActive ? "true" : "false";
  s += ",\"source_read_attempted\":"; s += sourceProbeAttempted ? "true" : "false";
  s += ",\"source_tcp_connected\":"; s += sourceTcpConnected ? "true" : "false";
  s += ",\"source_request_ascii\":\"" + String(sourceRequest) + "\"";
  s += ",\"source_command_bytes_sent\":" + String(sourceCommandBytesSent);
  s += ",\"source_response_bytes\":" + String(sourceResponseSize);
  s += ",\"source_response_complete\":"; s += sourceResponseComplete ? "true" : "false";
  s += ",\"source_response_truncated\":"; s += sourceTruncated ? "true" : "false";
  s += ",\"source_reply_code\":\"" + String(sourceReplyCode) + "\"";
  s += ",\"source_payload_bytes\":" + String(sourcePayloadLength);
  s += ",\"source_json_valid\":"; s += sourceJsonValid ? "true" : "false";
  s += ",\"source_grid_enable_code\":" + String(sourceGrid.enabled);
  s += ",\"source_grid_type_code\":" + String(sourceGrid.type);
  s += ",\"source_grid_esp32_id_matches\":"; s += sourceGrid.espId < 0 ? "null" : (sourceGrid.espId ? "true" : "false");
  s += ",\"source_pv_enable_code\":" + String(sourcePV.enabled);
  s += ",\"source_pv_type_code\":" + String(sourcePV.type);
  s += ",\"source_response_hex\":\"" + hexOf(sourceResponse, sourceResponseSize) + "\"";
  s += ",\"source_payload_json\":";
  if (sourceJsonValid) for (int i = 0; i < sourcePayloadLength; ++i) s += char(sourceResponse[19+i]);
  else s += "null";
  
  return s;
}

String sourceReadHtml() {
  String html = F("<section><h2>Vérifier le choix de compteur dans l’ECU</h2>"
    "<p>À utiliser après un changement sans réponse, pour connaître le réglage annoncé par l’ECU. Une configuration reconnue met à jour l’état local.</p>"
    "<p><strong>Ce relevé peut redémarrer le programme principal de l’ECU.</strong> Dans le firmware analysé, il lance aussi une détection des compteurs et actualise des fichiers. À lancer pendant un essai surveillé, sans autre commande dans EMA Manager.</p>"
    "<p>Un clic vérifie l’identité, attend une minute, puis envoie une seule lecture. Aucune relance.</p>");
  html += "<form method='post' action='/source-read'><label>IP ECU <input name='ecu_ip' required value='" + ecuTargetText + "'></label>";
  html += "<input type='hidden' name='token' value='" + formToken + "'><p><button";
  if (switchJobActive || (liveEnabled && !switchMayUseEsp()) || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) html += " disabled";
  html += F(">Relever le choix de compteur</button></p></form>");
  html += "<p>État : <strong>" + String(sourceStatus) + "</strong></p>";
  if (sourceJobActive && !sourceAwaitIdentity && !sourceActive)
    html += "<p>Relevé dans " + String(ecuWaitSeconds()) + " s.</p>";
  if (sourceResponseComplete) html += "<p>Code ECU : " + String(sourceReplyCode) + " · " + String(sourceResponseSize) + " octets reçus.</p>";
  if (sourceJsonValid && !strcmp(sourceReplyCode, "00")) {
    html += "<p>Rôle réseau : activation <strong>" + (sourceGrid.enabled < 0 ? String("inconnue") : String(sourceGrid.enabled)) + "</strong> · type <strong>";
    html += sourceGrid.type < 0 ? String("inconnu") : String(sourceGrid.type);
    html += F("</strong>. Ces valeurs décrivent le réglage reçu, pas l’utilisation effective par la régulation.</p>");
  }
  if (sourceProbeAttempted) html += F("<p>Relevé terminé ou tenté : conserve le diagnostic avant toute suite. Un échec ne déclenche aucun nouvel envoi.</p>");
  html += F("<p><a href='/diagnostic.json'>Télécharger le diagnostic v0.8.8</a></p></section>");
  return html;
}


// BEGIN CONTINUITY_HELPERS : compteurs passifs ; aucune commande ECU.
struct MeterReadCadence {
  uint32_t requests = 0, replies = 0, samples = 0, repeatedSamples = 0;
  uint32_t firstRequestMs = 0, lastRequestMs = 0, lastReplyMs = 0, lastSampleMs = 0;
  uint32_t lastGapMs = 0, minGapMs = 0, maxGapMs = 0, maxWriteMs = 0, maxServiceMs = 0;
  uint64_t gapSumMs = 0;
};

void meterNoteRequest(MeterReadCadence& state, uint32_t now) {
  if (state.requests) {
    const uint32_t gap = uint32_t(now - state.lastRequestMs);
    state.lastGapMs = gap; state.gapSumMs += gap;
    if (state.requests == 1 || gap < state.minGapMs) state.minGapMs = gap;
    if (gap > state.maxGapMs) state.maxGapMs = gap;
  } else state.firstRequestMs = now;
  ++state.requests; state.lastRequestMs = now;
}

void meterNoteReply(MeterReadCadence& state, uint32_t now, uint32_t sampleAt,
                    uint32_t writeMs, uint32_t serviceMs) {
  if (!state.replies || state.lastSampleMs != sampleAt) ++state.samples;
  else ++state.repeatedSamples;
  ++state.replies; state.lastReplyMs = now; state.lastSampleMs = sampleAt;
  if (writeMs > state.maxWriteMs) state.maxWriteMs = writeMs;
  if (serviceMs > state.maxServiceMs) state.maxServiceMs = serviceMs;
}

const char* meterCadenceLabel(const MeterReadCadence& state, uint32_t now) {
  if (!state.requests) return "aucune_requete_valide";
  if (uint32_t(now - state.lastRequestMs) >= 30000)
    return state.requests == 1 ? "une_seule_requete_puis_silence" : "requetes_repetees_puis_silence";
  return state.requests == 1 ? "premiere_requete_recue" : "requetes_repetees_recentes";
}

bool meterReconnectAllowed(bool used, bool live, bool connected, uint32_t replies,
                           size_t partialBytes, uint32_t silenceMs) {
  return !used && live && connected && replies > 0 && partialBytes == 0 && silenceMs >= 30000;
}
// END CONTINUITY_HELPERS

// BEGIN LIVE_METER_CORE
ProfileStreamState liveStream;
bool meterClientActive = false;
uint32_t liveReplies = 0, liveBytesSent = 0, liveRejected = 0, livePeersRejected = 0;
uint32_t liveLastTxMs = 0, liveLastAgeMs = 0, liveLastReadingMs = 0;
unsigned liveLastFault = 0;
const char* liveStatus = "observation";
uint8_t liveLastCipher[PROFILE_CIPHER_BYTES] = {}, liveLastPlain[PROFILE_REPLY_BYTES] = {};
double liveLastSource[8] = {};

// Journal borne en RAM, sans ecriture NVS ni commande supplementaire vers l'ECU.
// Les raisons sont exclusivement des chaines statiques internes au programme.
struct MeterTcpEvent {
  uint32_t atMs = 0, connection = 0, rxBytes = 0, replies = 0;
  const char* reason = "";
  bool live = false;
};
constexpr unsigned METER_TCP_EVENT_CAPACITY = 32;
MeterTcpEvent meterTcpEvents[METER_TCP_EVENT_CAPACITY];
unsigned meterTcpEventNext = 0, meterTcpEventCount = 0;
uint32_t meterTcpEventsDropped = 0;
uint32_t meterConnectionId = 0, meterOpenedMs = 0, meterClosedMs = 0;
uint32_t meterLastClosedConnectionId = 0;
String meterConnectionPeer;
uint32_t meterConnectionRx = 0, meterConnectionRequests = 0, meterConnectionReplies = 0;
uint32_t meterFirstRequestMs = 0, meterLastRequestMs = 0, meterMaxRequestGapMs = 0;
uint32_t meterLocalCloses = 0, meterTransportCloses = 0;
const char* meterCloseReason = "aucune";
const char* meterCloseOrigin = "aucune";
bool meterSilenceLogged = false;
MeterReadCadence meterCadence;
constexpr unsigned METER_EXCHANGE_CAPACITY = 32;
struct MeterExchange {
  uint32_t atMs = 0, connection = 0, gapMs = 0, sampleAgeMs = 0;
  uint32_t writeMs = 0, serviceMs = 0;
  uint16_t sentBytes = 0;
  float powerW = 0, rawPowerW = 0;
  uint16_t offsetW = 0;
  char testId[33] = {};
  bool sourceValid = false;
};
MeterExchange meterExchanges[METER_EXCHANGE_CAPACITY];
unsigned meterExchangeNext = 0, meterExchangeCount = 0;
uint32_t meterExchangesDropped = 0;
uint8_t meterLastRequestCipher[16] = {}, meterLastRequestPlain[16] = {};
uint8_t meterLastRejectedCipher[16] = {};
uint32_t meterLastRejectedMs = 0, meterRequestFirstByteMs = 0;
uint32_t meterServiceLastMs = 0, meterServiceMaxGapMs = 0;
bool meterServiceTracking = false, meterKeepaliveOk = false;
constexpr uint32_t METER_SILENCE_NOTICE_MS = 30000;
constexpr int METER_KEEPIDLE_S = 30, METER_KEEPINTERVAL_S = 10, METER_KEEPCOUNT = 3;
bool meterReconnectUsed = false, meterReconnectCancelled = false;
bool meterReconnectConnected = false, meterReconnectRead = false, meterReconnectReplied = false;
uint32_t meterReconnectAtMs = 0, meterReconnectFromConnection = 0;
uint32_t meterReconnectConnectedAtMs = 0, meterReconnectReadAtMs = 0;
uint32_t meterReconnectRequestsBefore = 0, meterReconnectRepliesBefore = 0;
uint32_t meterReconnectSilenceBeforeMs = 0;
const char* meterReconnectCancelReason = "aucune";

void configureMeterSocket() {
  meterClient.setTimeout(1);
  meterClient.setNoDelay(true);
  // Keepalive teste le transport, pas l'acceptation d'une mesure par l'ECU.
  const int on = 1;
  const int a = meterClient.setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
  const int b = meterClient.setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, &METER_KEEPIDLE_S, sizeof(int));
  const int c = meterClient.setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL, &METER_KEEPINTERVAL_S, sizeof(int));
  const int d = meterClient.setSocketOption(IPPROTO_TCP, TCP_KEEPCNT, &METER_KEEPCOUNT, sizeof(int));
  meterKeepaliveOk = a == 0 && b == 0 && c == 0 && d == 0;
}

const char* meterReconnectLabel() {
  if (!meterReconnectUsed) return "pas_demande";
  if (meterReconnectReplied) return "reponse_envoyee_apres_reconnexion";
  if (meterReconnectCancelled) return "essai_interrompu";
  if (meterReconnectRead) return "requete_recue_sans_reponse_complete";
  if (meterReconnectConnected) return "reconnecte_attente_requete";
  return "attente_reconnexion_ecu";
}

bool meterReconnectEligible() {
  return meterReconnectAllowed(meterReconnectUsed, liveEnabled, meterClientActive,
    meterConnectionReplies, liveStream.used, uint32_t(millis() - lastRxMs)) &&
    meterClient.connected() && !meterClient.available() &&
    livePeerAllowed(meterClient.remoteIP()) && selfTestOk &&
    shellyEnabled && shellyMeasurementValid() &&
    !switchJobActive && !sourceJobActive && !benchActive && !configJobActive &&
    !ecuPending && !ecuActive && !searchPending && !searchActive && !ecuWaitSeconds();
}


void recordMeterTcpEvent(const char* reason) {
  MeterTcpEvent& event = meterTcpEvents[meterTcpEventNext];
  event.atMs = millis(); event.connection = meterConnectionId;
  event.rxBytes = meterConnectionRx; event.replies = meterConnectionReplies;
  event.reason = reason; event.live = liveEnabled;
  meterTcpEventNext = (meterTcpEventNext + 1) % METER_TCP_EVENT_CAPACITY;
  if (meterTcpEventCount < METER_TCP_EVENT_CAPACITY) ++meterTcpEventCount;
  else ++meterTcpEventsDropped;
  Serial.printf("[Compteur TCP] t=%lu ms connexion=%lu evenement=%s rx=%lu reponses=%lu\n",
    (unsigned long)event.atMs, (unsigned long)event.connection, reason,
    (unsigned long)event.rxBytes, (unsigned long)event.replies);
}

void closeMeterConnection(const char* reason, bool local) {
  if (meterClientActive) {
    meterClosedMs = millis(); meterCloseReason = reason;
    meterLastClosedConnectionId = meterConnectionId;
    meterCloseOrigin = local ? "esp32" : "transport";
    if (local) ++meterLocalCloses; else ++meterTransportCloses;
    recordMeterTcpEvent(reason);
  }
  meterClient.stop(); meterClientActive = false;
}

void stopLiveMeter(const char* reason) {
  test1000.stop();
  if (meterReconnectUsed && !meterReconnectReplied && !meterReconnectCancelled) {
    meterReconnectCancelled = true; meterReconnectCancelReason = reason;
  }
  closeMeterConnection(reason, true);
  liveEnabled = false; liveStatus = reason;
}

// Filtre d'adresse seulement ; ce protocole n'authentifie pas cryptographiquement l'ECU.
bool livePeerAllowed(const IPAddress& peer) {
  return (ecuIdentityUsable && ecuAddressIsLocalUnicast(ecuTarget) && peer == ecuTarget) || switchPeerAllowed(peer);
}

void noteMeterRequest() {
  const uint32_t requestAt = millis();
  if (!meterConnectionRequests) meterFirstRequestMs = requestAt;
  else {
    const uint32_t gap = uint32_t(requestAt - meterLastRequestMs);
    if (gap > meterMaxRequestGapMs) meterMaxRequestGapMs = gap;
  }
  ++meterConnectionRequests; meterLastRequestMs = requestAt;
  meterNoteRequest(meterCadence, requestAt);
  memcpy(meterLastRequestCipher, liveStream.request, sizeof(meterLastRequestCipher));
  decryptFirstBlock(meterLastRequestCipher, meterLastRequestPlain);
  if (meterReconnectUsed && !meterReconnectCancelled && !meterReconnectRead &&
      meterConnectionId != meterReconnectFromConnection) {
    meterReconnectRead = true; meterReconnectReadAtMs = requestAt;
    recordMeterTcpEvent("requete_apres_reconnexion");
  }
}

// L'adaptateur conserve les octets effectivement lus et la derniere reponse complete.
// Le repondeur utilise toujours les mesures courantes, jamais l'instantane du banc.
class MeterCaptureStream {
public:
  int available() { return meterClient.available(); }
  int read() {
    const int value = meterClient.read();
    if (value < 0) return value;
    if (liveEnabled && !liveStream.used) meterRequestFirstByteMs = millis();
    if (captureSize < CAPTURE_CAPACITY) captureData[captureSize++] = uint8_t(value);
    else captureTruncated = true;
    ++receivedBytes; ++meterConnectionRx; lastRxMs = millis(); meterSilenceLogged = false;
    if (meterConnectionRx == 1) recordMeterTcpEvent("premiers_octets");
    return value;
  }
  size_t write(const uint8_t* bytes, size_t length) {
    // Appele uniquement apres validation AES, commande et CRC de la requete.
    (void)bytes; // La reponse est reconstruite a la demande sur la mesure reelle.
    const uint32_t requestAt = meterLastRequestMs;
    MeterExchange exchange;
    exchange.atMs = requestAt; exchange.connection = meterConnectionId;
    exchange.gapMs = meterCadence.lastGapMs;
    const uint16_t offsetW = test1000.remaining(millis()) ? 1000 : 0;
    // Ne pas laisser un journal serie bloquant retarder l'envoi de la reponse.
    const bool valid = length == PROFILE_CIPHER_BYTES && liveEnabled && shellyEnabled &&
      buildProfileForPower(shellyPowerW + offsetW) && profilePreviewReadingMs == shellyLastGoodMs &&
      uint32_t(millis() - profilePreviewReadingMs) < SHELLY_MAX_AGE_MS;
    size_t sent = 0;
    if (valid) {
      exchange.sourceValid = true;
      exchange.rawPowerW = float(shellyPowerW);
      exchange.powerW = profileGetFloat(profilePlain + 3 + 4 * PROFILE_PT_INDEX);
      exchange.offsetW = offsetW;
      memcpy(exchange.testId, test1000.id, sizeof(exchange.testId));
      const uint32_t sentAt = millis();
      exchange.sampleAgeMs = uint32_t(sentAt - profilePreviewReadingMs);
      sent = meterClient.write(profileCipher, PROFILE_CIPHER_BYTES);
      const uint32_t doneAt = millis();
      exchange.writeMs = uint32_t(doneAt - sentAt);
      exchange.serviceMs = uint32_t(doneAt - meterRequestFirstByteMs);
      exchange.sentBytes = uint16_t(sent);
      if (sent == PROFILE_CIPHER_BYTES) {
        memcpy(liveLastCipher, profileCipher, sizeof(liveLastCipher));
        memcpy(liveLastPlain, profilePlain, sizeof(liveLastPlain));
        const double readings[] = {shellyPowerW, shellyVoltageV, shellyCurrentA, shellyFrequencyHz,
          shellyApparentVA, shellyPowerFactor, shellyImportWh, shellyExportWh};
        memcpy(liveLastSource, readings, sizeof(readings));
        liveLastTxMs = sentAt; liveLastReadingMs = profilePreviewReadingMs;
        liveLastAgeMs = exchange.sampleAgeMs;
        ++meterConnectionReplies;
        meterNoteReply(meterCadence, doneAt, profilePreviewReadingMs, exchange.writeMs, exchange.serviceMs);
        if (meterReconnectUsed && !meterReconnectCancelled && meterConnectionId != meterReconnectFromConnection)
          meterReconnectReplied = true;
      }
    }
    meterExchanges[meterExchangeNext] = exchange;
    meterExchangeNext = (meterExchangeNext + 1) % METER_EXCHANGE_CAPACITY;
    if (meterExchangeCount < METER_EXCHANGE_CAPACITY) ++meterExchangeCount;
    else ++meterExchangesDropped;
    recordMeterTcpEvent(!valid ? "mesure_refusee_avant_envoi" :
      (sent == PROFILE_CIPHER_BYTES ? "requete_valide_reponse_complete" : "requete_valide_envoi_incomplet"));
    return sent;
  }
  void stop() { meterClient.stop(); }
};

void serviceMeterTcp() {
  const uint32_t serviceAt = millis();
  if (liveEnabled && meterClientActive) {
    if (meterServiceTracking) {
      const uint32_t gap = uint32_t(serviceAt - meterServiceLastMs);
      if (gap > meterServiceMaxGapMs) meterServiceMaxGapMs = gap;
    }
    meterServiceLastMs = serviceAt; meterServiceTracking = true;
  } else meterServiceTracking = false;
  if (meterClientActive && !meterClient.connected() && !meterClient.available()) {
    // L'API constate une fermeture/perte ; elle n'identifie pas FIN, RST ou panne reseau.
    closeMeterConnection("connexion_transport_fermee_ou_perdue", false);
    if (liveEnabled) liveStatus = "connexion_transport_fermee_ou_perdue";
  }
  // Accepter aussi une nouvelle connexion de l'ECU si l'ancienne semble encore
  // ouverte apres un redemarrage distant. Aucun delai de silence arbitraire.
  WiFiClient incoming = meterServer.accept();
  if (incoming) {
    ++connectionCount; lastPeer = incoming.remoteIP().toString();
    if (liveEnabled && !livePeerAllowed(incoming.remoteIP())) {
      ++livePeersRejected; incoming.stop();
    } else {
      closeMeterConnection("remplacement_par_nouvelle_connexion", true);
      meterClient = incoming; meterClientActive = true;
      configureMeterSocket();
      captureSize = 0; captureTruncated = decodeTried = expectedRead = false; decodedHex = "";
      lastRxMs = millis(); liveStream = ProfileStreamState{}; liveStream.lastByteMs = lastRxMs;
      meterConnectionId = connectionCount; meterOpenedMs = lastRxMs;
      meterConnectionPeer = lastPeer;
      meterConnectionRx = meterConnectionRequests = meterConnectionReplies = 0;
      meterFirstRequestMs = meterLastRequestMs = meterMaxRequestGapMs = 0;
      meterSilenceLogged = false;
      recordMeterTcpEvent("connexion_acceptee");
      if (meterReconnectUsed && !meterReconnectCancelled && !meterReconnectConnected &&
          liveEnabled && livePeerAllowed(meterClient.remoteIP())) {
        meterReconnectConnected = true; meterReconnectConnectedAtMs = millis();
        recordMeterTcpEvent("reconnexion_apres_essai");
      }
      if (liveEnabled) liveStatus = "attente_requete_ecu";
    }
  }
  if (!meterClientActive) return;
  MeterCaptureStream captured;
  if (!liveEnabled) {
    unsigned budget = 512;
    while (captured.available() && budget--) if (captured.read() < 0) break;
    inspectCapture();
    if (uint32_t(millis() - lastRxMs) >= CLIENT_IDLE_MS) {
      closeMeterConnection("observation_delai_5s", true);
    }
    return;
  }
  if (!livePeerAllowed(meterClient.remoteIP())) {
    ++livePeersRejected; closeMeterConnection("adresse_non_autorisee", true); return;
  }
  bool fresh = shellyEnabled && selfTestOk && shellyMeasurementValid();
  // La reponse est construite dans MeterCaptureStream::write, une fois la
  // requete complete validee. Aucun chiffrement repete pour chaque fragment.
  const unsigned previousAccepted = liveStream.accepted, previousRejected = liveStream.rejected;
  const unsigned previousBytes = liveStream.bytesSent;
  serviceProfileStream(captured, liveStream, fresh, millis(), profileCipher,
                       LIVE_IDLE_TIMEOUT_MS, noteMeterRequest);
  liveReplies += liveStream.accepted - previousAccepted;
  liveRejected += liveStream.rejected - previousRejected;
  liveBytesSent += liveStream.bytesSent - previousBytes;
  inspectCapture();
  if (liveStream.accepted != previousAccepted) liveStatus = "reponse_envoyee";
  if (liveStream.fault) {
    liveLastFault = liveStream.fault;
    if (liveStream.fault == 1) {
      memcpy(meterLastRejectedCipher, liveStream.request, sizeof(meterLastRejectedCipher));
      meterLastRejectedMs = millis();
    }
    switch (liveStream.fault) {
      case 1: liveStatus = "requete_rejetee"; break;
      case 2: liveStatus = "source_indisponible"; break;
      case 3: liveStatus = "requete_incomplete_delai_1500ms"; break;
      default: liveStatus = "envoi_incomplet"; break;
    }
    closeMeterConnection(liveStatus, true);
  } else if (!liveStream.used && !meterSilenceLogged && uint32_t(millis() - lastRxMs) >= CLIENT_IDLE_MS) {
    meterSilenceLogged = true;
    liveStatus = "connexion_ouverte_attente_requete";
    recordMeterTcpEvent("silence_5s_connexion_conservee");
  }
}
// END LIVE_METER_CORE

void handleLiveMeterRequest() {
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("token") || web.arg("token") != formToken) {
    web.send(400, "text/plain; charset=utf-8", "Recharge la page avant une nouvelle action."); return;
  }
  const String action = web.arg("action");
  if (action == "stop") { switchResumeHeld = true; stopLiveMeter("arrete_manuellement"); }
  else if (action == "start") {
    if (switchJobActive || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) {
      web.send(409, "text/plain; charset=utf-8", "Attends la fin de l’opération ECU et de la temporisation."); return;
    }
    if (WiFi.status() != WL_CONNECTED || (!ecuIdentityUsable && !switchPeerAllowed(ecuTarget)) || !ecuAddressIsLocalUnicast(ecuTarget)) {
      web.send(409, "text/plain; charset=utf-8", "Il faut d’abord une identification valide de cet ECU sur ce réseau."); return;
    }
    if (!shellyEnabled || !buildProfilePreview()) {
      web.send(409, "text/plain; charset=utf-8", "Démarre la lecture Shelly et attends une mesure complète et récente."); return;
    }
    closeMeterConnection("reactivation_manuelle", true);
    switchResumeHeld = false; liveEnabled = true; liveStatus = "attente_requete_ecu";
  } else { web.send(400, "text/plain; charset=utf-8", "Action inconnue."); return; }
  renewFormToken(); web.sendHeader("Location", "/"); web.send(303, "text/plain", "Repondeur mis a jour.");
}

void handleMeterReconnectRequest() {
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("token") || web.arg("token") != formToken) {
    web.send(400, "text/plain; charset=utf-8", "Recharge la page avant cet essai."); return;
  }
  if (!meterReconnectEligible()) {
    web.send(409, "text/plain; charset=utf-8",
      "Essai indisponible : il faut une connexion ECU ouverte, au moins une réponse, "
      "30 secondes sans octets, une mesure Shelly valide et aucune opération en cours. "
      "Un seul essai est permis par démarrage."); return;
  }
  meterReconnectUsed = true; meterReconnectAtMs = millis();
  meterReconnectFromConnection = meterConnectionId;
  meterReconnectRequestsBefore = meterCadence.requests;
  meterReconnectRepliesBefore = meterCadence.replies;
  meterReconnectSilenceBeforeMs = uint32_t(meterReconnectAtMs - lastRxMs);
  // Ferme une seule connexion compteur. Aucun appel 0072, 0073, 0075,
  // aucune commande applicative vers l'ECU et aucune ecriture NVS.
  closeMeterConnection("essai_reconnexion_unique_manuel", true);
  liveStatus = "essai_attente_reconnexion_ecu";
  renewFormToken(); web.sendHeader("Location", "/");
  web.send(303, "text/plain; charset=utf-8", "Connexion compteur fermée une fois. Attente de l’ECU.");
}

String meterContinuityJsonFields() {
  const uint32_t now = millis();
  String s = ",\"meter_continuity_status\":\"" + String(meterCadenceLabel(meterCadence, now)) + "\"";
  s += ",\"meter_valid_requests_total\":" + String(meterCadence.requests);
  s += ",\"meter_complete_replies_total\":" + String(meterCadence.replies);
  s += ",\"meter_distinct_shelly_samples_sent\":" + String(meterCadence.samples);
  s += ",\"meter_replies_reusing_sample\":" + String(meterCadence.repeatedSamples);
  s += ",\"meter_read_age_ms\":";
  s += meterCadence.requests ? String(uint32_t(now - meterCadence.lastRequestMs)) : String("null");
  s += ",\"meter_read_span_ms\":";
  s += meterCadence.requests ? String(uint32_t(meterCadence.lastRequestMs - meterCadence.firstRequestMs)) : String("null");
  s += ",\"meter_read_last_gap_ms\":";
  s += meterCadence.requests > 1 ? String(meterCadence.lastGapMs) : String("null");
  s += ",\"meter_read_min_gap_ms\":";
  s += meterCadence.requests > 1 ? String(meterCadence.minGapMs) : String("null");
  s += ",\"meter_read_max_gap_ms\":";
  s += meterCadence.requests > 1 ? String(meterCadence.maxGapMs) : String("null");
  s += ",\"meter_read_mean_gap_ms\":";
  s += meterCadence.requests > 1 ? shellyNumber(double(meterCadence.gapSumMs) / (meterCadence.requests - 1)) : String("null");
  s += ",\"meter_max_write_ms\":" + String(meterCadence.maxWriteMs);
  s += ",\"meter_max_response_service_ms\":" + String(meterCadence.maxServiceMs);
  s += ",\"meter_max_service_gap_ms\":" + String(meterServiceMaxGapMs);
  s += ",\"meter_tcp_keepalive_configured\":"; s += meterKeepaliveOk ? "true" : "false";
  s += ",\"meter_tcp_keepidle_s\":" + String(METER_KEEPIDLE_S);
  s += ",\"meter_tcp_keepinterval_s\":" + String(METER_KEEPINTERVAL_S);
  s += ",\"meter_tcp_keepcount\":" + String(METER_KEEPCOUNT);
  s += ",\"meter_unsolicited_measurements\":false,\"meter_measurement_acceptance_confirmed\":false";
  s += ",\"meter_last_request_cipher_hex\":\"";
  if (meterCadence.requests) s += hexOf(meterLastRequestCipher, sizeof(meterLastRequestCipher));
  s += "\",\"meter_last_request_plain_padded_hex\":\"";
  if (meterCadence.requests) s += hexOf(meterLastRequestPlain, sizeof(meterLastRequestPlain));
  s += "\",\"meter_last_rejected_cipher_hex\":\"";
  if (liveRejected) s += hexOf(meterLastRejectedCipher, sizeof(meterLastRejectedCipher));
  s += "\",\"meter_last_rejected_uptime_ms\":";
  s += liveRejected ? String(meterLastRejectedMs) : String("null");
  s += ",\"meter_exchanges_dropped\":" + String(meterExchangesDropped);
  s += ",\"meter_exchanges\":[";
  for (unsigned i = 0; i < meterExchangeCount; ++i) {
    const unsigned index = (meterExchangeNext + METER_EXCHANGE_CAPACITY - meterExchangeCount + i) % METER_EXCHANGE_CAPACITY;
    const MeterExchange& e = meterExchanges[index];
    if (i) s += ",";
    s += "{\"uptime_ms\":" + String(e.atMs) + ",\"connection\":" + String(e.connection);
    s += ",\"gap_ms\":" + String(e.gapMs) + ",\"sent_bytes\":" + String(e.sentBytes);
    s += ",\"sample_age_ms\":"; s += e.sourceValid ? String(e.sampleAgeMs) : String("null");
    s += ",\"power_w\":"; s += e.sourceValid ? shellyNumber(e.powerW, 6) : String("null");
    s += ",\"shelly_sample_w\":"; s += e.sourceValid ? shellyNumber(e.rawPowerW, 6) : String("null");
    s += ",\"offset_w\":"; s += e.sourceValid ? String(e.offsetW) : String("null");
    s += ",\"test_id\":\"" + String(e.testId) + "\"";
    s += ",\"write_ms\":" + String(e.writeMs) + ",\"service_ms\":" + String(e.serviceMs) + "}";
  }
  s += "],\"meter_reconnect_trial\":{\"used\":"; s += meterReconnectUsed ? "true" : "false";
  s += ",\"status\":\"" + String(meterReconnectLabel()) + "\",\"at_ms\":";
  s += meterReconnectUsed ? String(meterReconnectAtMs) : String("null");
  s += ",\"from_connection\":" + String(meterReconnectFromConnection);
  s += ",\"requests_before\":" + String(meterReconnectRequestsBefore);
  s += ",\"replies_before\":" + String(meterReconnectRepliesBefore);
  s += ",\"silence_before_ms\":" + String(meterReconnectSilenceBeforeMs);
  s += ",\"reconnected\":"; s += meterReconnectConnected ? "true" : "false";
  s += ",\"reconnect_delay_ms\":";
  s += meterReconnectConnected ? String(uint32_t(meterReconnectConnectedAtMs - meterReconnectAtMs)) : String("null");
  s += ",\"first_query_delay_ms\":";
  s += meterReconnectRead ? String(uint32_t(meterReconnectReadAtMs - meterReconnectAtMs)) : String("null");
  s += ",\"reply_sent_after\":"; s += meterReconnectReplied ? "true" : "false";
  s += ",\"cancelled\":"; s += meterReconnectCancelled ? "true" : "false";
  s += ",\"cancel_reason\":\"" + String(meterReconnectCancelReason) + "\"}";
  return s;
}

String meterContinuityHtml() {
  const uint32_t now = millis();
  String h = F("<section><h2>Lectures continues de l’ECU</h2>");
  if (!liveEnabled) h += F("<p>Répondeur arrêté. Les compteurs ci-dessous conservent l’historique de ce démarrage.</p>");
  if (!meterCadence.requests) h += F("<p><strong>Aucune demande de mesure valide reçue.</strong></p>");
  else if (meterCadence.requests == 1) h += F("<p><strong>Une seule demande reçue.</strong> La lecture répétée n’est pas encore observée.</p>");
  else h += "<p><strong>" + String(meterCadence.requests) + " demandes reçues</strong>, sur " +
    String(uint32_t(meterCadence.lastRequestMs - meterCadence.firstRequestMs) / 1000) + " s.</p>";
  h += "<p>Réponses complètes : <strong>" + String(meterCadence.replies) + "</strong><br>";
  h += "Mesures Shelly successives envoyées : <strong>" + String(meterCadence.samples) + "</strong><br>";
  h += "Réponses réutilisant la même mesure : " + String(meterCadence.repeatedSamples) + ".</p>";
  if (meterCadence.requests) {
    h += "<p>Dernière demande : il y a <strong>" + String(uint32_t(now - meterCadence.lastRequestMs) / 1000) + " s</strong>.";
    if (uint32_t(now - meterCadence.lastRequestMs) >= METER_SILENCE_NOTICE_MS)
      h += F(" <strong>Aucune nouvelle demande depuis au moins 30 s.</strong>");
    h += "</p>";
  }
  if (meterCadence.requests > 1) {
    h += "<p>Intervalle entre demandes : dernier <strong>" + String(meterCadence.lastGapMs) +
      " ms</strong>, min. " + String(meterCadence.minGapMs) + " ms, max. " + String(meterCadence.maxGapMs) + " ms.</p>";
  }
  h += F("<p>Ces compteurs suivent les échanges depuis le démarrage, y compris après reconnexion. "
    "Une lecture Shelly est prévue toutes les 2 secondes. Des demandes plus rapprochées peuvent recevoir la même mesure récente.</p>"
    "<p>Des demandes répétées confirment la continuité des échanges. L’acceptation des valeurs et leur utilisation pour réguler restent à vérifier côté ECU.</p>");
  h += "<p><a href='/diagnostic.json'>Télécharger le diagnostic de cet essai</a></p>";
  h += F("<details><summary>Essai de reprise après une demande unique</summary>"
    "<p>Si l’ECU reste silencieux, ce bouton ferme une seule fois la connexion compteur. "
    "L’ESP32 attend ensuite que l’ECU se reconnecte. Le résultat dira si les demandes reprennent.</p>"
    "<p>L’essai est disponible après une réponse complète et 30 secondes sans octets reçus. "
    "Il peut interrompre les mesures pendant la reconnexion. Il ne renvoie ni la sélection du compteur ni la consigne de bridage.</p>");
  h += "<form method='post' action='/meter-reconnect'><input type='hidden' name='token' value='" + formToken + "'><button";
  if (!meterReconnectEligible()) h += " disabled";
  h += ">Tester une reconnexion TCP (une fois)</button></form>";
  h += "<p>État : <strong>" + String(meterReconnectLabel()) + "</strong>.</p>";
  h += F("<p>Le journal conserve les résultats avant et après cette fermeture. Le bouton Retour aux tores réseau reste disponible.</p></details></section>");
  return h;
}

String meterTcpJsonFields() {
  const uint32_t now = millis();
  String s = ",\"live_tcp_idle_timeout_ms\":" + String(LIVE_IDLE_TIMEOUT_MS);
  s += ",\"live_tcp_partial_timeout_ms\":" + String(PROFILE_PARTIAL_TIMEOUT_MS);
  s += ",\"meter_tcp_connection_active\":"; s += meterClientActive ? "true" : "false";
  s += ",\"meter_tcp_connected\":"; s += (meterClientActive && meterClient.connected()) ? "true" : "false";
  s += ",\"meter_tcp_connection_id\":" + String(meterConnectionId);
  s += ",\"meter_tcp_connection_peer\":\"" + meterConnectionPeer + "\"";
  s += ",\"meter_tcp_connection_rx_bytes\":" + String(meterConnectionRx);
  s += ",\"meter_tcp_connection_requests\":" + String(meterConnectionRequests);
  s += ",\"meter_tcp_connection_replies\":" + String(meterConnectionReplies);
  s += ",\"meter_tcp_partial_request_bytes\":" + String(liveStream.used);
  s += ",\"meter_tcp_opened_uptime_ms\":"; s += meterConnectionId ? String(meterOpenedMs) : String("null");
  s += ",\"meter_tcp_connection_duration_ms\":";
  s += meterConnectionId ? String(uint32_t((meterClientActive ? now : meterClosedMs) - meterOpenedMs)) : String("null");
  s += ",\"meter_tcp_current_silence_ms\":";
  s += meterClientActive ? String(uint32_t(now - lastRxMs)) : String("null");
  s += ",\"meter_tcp_first_request_uptime_ms\":";
  s += meterConnectionRequests ? String(meterFirstRequestMs) : String("null");
  s += ",\"meter_tcp_last_request_uptime_ms\":";
  s += meterConnectionRequests ? String(meterLastRequestMs) : String("null");
  s += ",\"meter_tcp_last_request_age_ms\":";
  s += meterConnectionRequests ? String(uint32_t(now - meterLastRequestMs)) : String("null");
  s += ",\"meter_tcp_max_request_gap_ms\":";
  s += meterConnectionRequests > 1 ? String(meterMaxRequestGapMs) : String("null");
  s += ",\"meter_tcp_local_closes\":" + String(meterLocalCloses);
  s += ",\"meter_tcp_transport_closes\":" + String(meterTransportCloses);
  s += ",\"meter_tcp_last_close_connection_id\":" + String(meterLastClosedConnectionId);
  s += ",\"meter_tcp_last_close_uptime_ms\":";
  s += meterLastClosedConnectionId ? String(meterClosedMs) : String("null");
  s += ",\"meter_tcp_last_close_origin\":\"" + String(meterCloseOrigin) + "\"";
  s += ",\"meter_tcp_last_close_reason\":\"" + String(meterCloseReason) + "\"";
  s += ",\"meter_tcp_events_dropped\":" + String(meterTcpEventsDropped);
  s += ",\"meter_tcp_events\":[";
  for (unsigned i = 0; i < meterTcpEventCount; ++i) {
    const unsigned index = (meterTcpEventNext + METER_TCP_EVENT_CAPACITY - meterTcpEventCount + i) % METER_TCP_EVENT_CAPACITY;
    const MeterTcpEvent& event = meterTcpEvents[index];
    if (i) s += ",";
    s += "{\"uptime_ms\":" + String(event.atMs) + ",\"connection\":" + String(event.connection);
    s += ",\"event\":\"" + String(event.reason) + "\",\"live\":";
    s += event.live ? "true" : "false";
    s += ",\"rx_bytes\":" + String(event.rxBytes) + ",\"replies\":" + String(event.replies) + "}";
  }
  s += "]";
  return s;
}

String liveMeterJsonFields() {
  String s = ",\"live_meter_enabled\":"; s += liveEnabled ? "true" : "false";
  s += ",\"live_meter_status\":\"" + String(liveStatus) + "\"";
  s += ",\"live_meter_allowed_peer\":\"" + liveAllowedPeerText() + "\"";
  s += ",\"live_meter_replies\":" + String(liveReplies);
  s += ",\"live_meter_bytes_sent\":" + String(liveBytesSent);
  s += ",\"live_meter_rejected_requests\":" + String(liveRejected);
  s += ",\"live_meter_rejected_peers\":" + String(livePeersRejected);
  s += ",\"live_meter_last_fault\":" + String(liveLastFault);
  s += ",\"meter_measurements_sent\":"; s += liveReplies ? "true" : "false";
  s += ",\"meter_bytes_transmitted\":"; s += liveBytesSent ? "true" : "false";
  s += ",\"live_last_tx_uptime_ms\":"; s += liveReplies ? String(liveLastTxMs) : String("null");
  s += ",\"live_last_sample_uptime_ms\":"; s += liveReplies ? String(liveLastReadingMs) : String("null");
  s += ",\"live_last_sample_age_at_send_ms\":"; s += liveReplies ? String(liveLastAgeMs) : String("null");
  s += ",\"live_last_source\":";
  if (liveReplies) {
    const char* names[] = {"power_w", "voltage_v", "current_a", "frequency_hz", "apparent_va", "power_factor", "import_wh", "export_wh"};
    s += "{";
    for (unsigned i = 0; i < 8; ++i) {
      if (i) s += ",";
      s += "\"" + String(names[i]) + "\":" + shellyNumber(liveLastSource[i], 6);
    }
    s += "}";
  } else s += "null";
  s += ",\"live_last_plain_hex\":\"";
  if (liveReplies) s += hexOf(liveLastPlain, sizeof(liveLastPlain));
  s += "\",\"live_last_cipher_hex\":\"";
  if (liveReplies) s += hexOf(liveLastCipher, sizeof(liveLastCipher));
  s += "\",\"live_ecu_consumption_verified\":false";
  s += meterTcpJsonFields();
  s += meterContinuityJsonFields();
  return s;
}

String liveMeterHtml() {
  String html = F("<section><h2>2. Répondeur réseau Shelly → ECU</h2>"
    "<p>Le serveur peut maintenant répondre aux requêtes de l’ECU identifié sur le port 9527. Il conserve le signe de la puissance et refuse une source invalide ou âgée de 15 secondes.</p>");
  html += "<p>Répondeur : <strong>" + String(liveEnabled ? "activé" : "arrêté / observation") + "</strong><br>";
  html += "État : " + String(liveStatus) + "<br>Réponses complètes envoyées : <strong>" + String(liveReplies) + "</strong> · " + String(liveBytesSent) + " octets</p>";
  if (liveReplies) html += "<p>Dernière puissance envoyée : <strong>" + shellyNumber(liveLastSource[0]) + " W</strong> · âge à l’envoi : " + String(liveLastAgeMs) + " ms.</p>";
  html += "<p>Connexion compteur : <strong>" + String((meterClientActive && meterClient.connected()) ? "ouverte" : "fermée / en attente") + "</strong>";
  if (meterClientActive) html += " · silence depuis " + String(uint32_t(millis() - lastRxMs) / 1000) + " s";
  html += ".<br>Réponses sur la dernière connexion : <strong>" + String(meterConnectionReplies) + "</strong>.</p>";
  html += F("<p>La connexion reste ouverte entre les demandes de l’ECU. Une mesure périmée reste refusée. Le diagnostic conserve les 32 derniers événements.</p>");
  html += "<form method='post' action='/live-meter'><input type='hidden' name='token' value='" + formToken + "'>";
  html += "<input type='hidden' name='action' value='" + String(liveEnabled ? "stop" : "start") + "'><button";
  if (!liveEnabled && ((!ecuIdentityUsable && !switchPeerAllowed(ecuTarget)) || !shellyEnabled || !shellyMeasurementValid() || switchJobActive || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds())) html += " disabled";
  html += ">" + String(liveEnabled ? "Arrêter le répondeur" : "Activer les réponses Shelly à l’ECU") + "</button></form>";
  if (!ecuIdentityUsable && !switchPeerAllowed(ecuTarget)) {
    html += "<p>Identité ECU à vérifier, notamment après une coupure Wi-Fi.</p><form method='post' action='/ecu-info'>";
    html += "<input name='ecu_ip' required value='" + ecuTargetText + "'><input type='hidden' name='token' value='" + formToken + "'><button";
    if (switchJobActive || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) html += " disabled";
    html += ">Vérifier l’identité ECU</button></form>";
  }
  html += F("<p>Ce bouton ne sélectionne pas le compteur dans l’ECU. Zéro réponse reste possible tant que l’ECU ne l’interroge pas. L’émission de trames ne prouve pas leur utilisation par la régulation.</p>"
    "<p>Pour revenir aux tores, utilise Retour aux tores réseau. Arrêter le répondeur ne change pas la sélection dans l’ECU. Si l’ESP32 a été sélectionné, le Shelly et le répondeur reprennent après une coupure Wi-Fi ou un redémarrage, avec des mesures valides uniquement.</p>"
    "<p><a href='/diagnostic.json'>Télécharger le diagnostic et la dernière réponse</a></p></section>");
  return html;
}


bool switchOnlyKeys(ShellyJson::Value obj, const char* const* names, unsigned count);
bool switchStringField(ShellyJson::Value object, const char* key, const char* expected);

// BEGIN SOURCE_SWITCH_HELPERS
// EXEMPLE de sauvegarde : verifier les vrais reglages Grid/PV du site avant selection.
// Le champ d'etat "status" du getter n'est pas un reglage et n'est pas renvoye.
static const char CT_GRID_BACKUP[] =
  "{\"meterGrid\":{\"meterType\":\"4\",\"meterEnable\":\"1\",\"meterInfo4\":{"
  "\"PH\":\"3\",\"ratioA\":\"200:0.0666\",\"ratioB\":\"200:0.0666\",\"ratioC\":\"200:0.0666\","
  "\"reverseA\":\"0\",\"reverseB\":\"0\",\"reverseC\":\"0\"}}}";
static const char EXPECTED_METER_ID[] = "LAB000000000000";
constexpr size_t SWITCH_REQUEST_CAPACITY = 640;
enum { SWITCH_INITIAL, SWITCH_SELECT_UNCERTAIN, SWITCH_ESP_REPORTED,
       SWITCH_RESTORE_UNCERTAIN, SWITCH_CT_REPORTED, SWITCH_UNKNOWN };
enum { SWITCH_IDLE, SWITCH_IDENTITY, SWITCH_PREFLIGHT_WAIT,
       SWITCH_PREFLIGHT_READ, SWITCH_WRITE_WAIT, SWITCH_WRITE_READ };

bool switchAsciiName(const char* value, size_t maxLength) {
  if (!value || !*value || strlen(value) > maxLength) return false;
  for (const char* p = value; *p; ++p)
    if (!isalnum(static_cast<unsigned char>(*p)) && *p != '-' && *p != '.' && *p != '_') return false;
  return true;
}

// Champs admis et uniques. Evite d'ecraser un reglage inattendu dans le precontrole.
bool switchOnlyKeys(ShellyJson::Value obj, const char* const* names, unsigned count) {
  const char* p = obj.first;
  ShellyJson::spaces(p, obj.last);
  if (p == obj.last || *p++ != '{' || count > 31) return false;
  uint32_t seen = 0;
  for (;;) {
    ShellyJson::spaces(p, obj.last);
    if (p < obj.last && *p == '}') { ++p; ShellyJson::spaces(p, obj.last); return p == obj.last; }
    ShellyJson::Value key{p,p};
    if (!ShellyJson::stringToken(p, obj.last)) return false;
    key.last = p;
    unsigned i = 0;
    for (; i < count && !ShellyJson::stringEquals(key,names[i]); ++i) {}
    if (i == count || (seen & (1UL << i))) return false;
    seen |= 1UL << i;
    ShellyJson::spaces(p, obj.last);
    if (p == obj.last || *p++ != ':' || !ShellyJson::skip(p,obj.last)) return false;
    ShellyJson::spaces(p, obj.last);
    if (p == obj.last) return false;
    if (*p == '}') continue;
    if (*p++ != ',') return false;
    // La validite JSON du document entier est verifiee par l'appelant.
  }
}

bool switchStringField(ShellyJson::Value object, const char* key, const char* expected) {
  ShellyJson::Value v{};
  return ShellyJson::field(object,key,v) == 1 && ShellyJson::stringEquals(v,expected);
}

bool switchCtRoleMatches(const char* json, size_t n, const char* role) {
  ShellyJson doc(json,n);
  ShellyJson::Value m{}, info{};
  const char* const outer[] = {"meterType","meterEnable","meterInfo4","status"};
  const char* const keys[] = {"PH","ratioA","ratioB","ratioC","reverseA","reverseB","reverseC","status"};
  if (!doc.valid() || ShellyJson::field(doc.root,role,m) != 1 || !switchOnlyKeys(m,outer,4) ||
      !switchStringField(m,"meterType","4") || !switchStringField(m,"meterEnable","1") ||
      ShellyJson::field(m,"meterInfo4",info) != 1 || !switchOnlyKeys(info,keys,8)) return false;
  for (unsigned i = 0; i < 7; ++i)
    if (!switchStringField(info,keys[i],i == 0 ? "3" : (i < 4 ? "200:0.0666" : "0"))) return false;
  return true;
}

size_t buildSwitchPayload(char* out, size_t capacity, bool restore,
                        const char* id, const char* host, const char* instance, const char* ip) {
  if (!out || !capacity) return 0;
  out[0] = '\0';
  if (restore) {
    if (capacity < sizeof(CT_GRID_BACKUP)) return 0;
    memcpy(out,CT_GRID_BACKUP,sizeof(CT_GRID_BACKUP)); return sizeof(CT_GRID_BACKUP)-1;
  }
  if (!switchAsciiName(id,30) || !switchAsciiName(host,62) ||
      !switchAsciiName(instance,62) || !switchAsciiName(ip,15)) return 0;
  // IMPORTANT : port doit etre une CHAINE pour setMeterConfigNew C1.2.26.
  // Le serveur le transforme en nombre pour meterManager. "enable" est obligatoire.
  const int n = snprintf(out,capacity,
    "{\"meterGrid\":{\"meterType\":\"3\",\"meterEnable\":\"1\",\"meterInfo3\":{"
    "\"id\":\"%s\",\"hostName\":\"%s\",\"instanceName\":\"%s\",\"ip\":\"%s\","
    "\"port\":\"9527\",\"type\":\"3\",\"company\":\"prototype\",\"enable\":\"1\"}}}",
    id,host,instance,ip);
  if (n < 0 || size_t(n) >= capacity) { out[0] = '\0'; return 0; }
  return size_t(n);
}

size_t buildSourceWriteRequest(char* out, size_t capacity, const char* uid,
                              const char* payload, size_t n) {
  if (!out || !capacity) return 0;
  out[0] = '\0';
  if (!uid || strlen(uid) != 12 || !payload || !n || n > 9999-32 || capacity < n+33) return 0;
  for (unsigned i=0;i<12;++i) if (uid[i] < '0' || uid[i] > '9') return 0;
  ShellyJson doc(payload,n); ShellyJson::Value grid{};
  const char* const keys[] = {"meterGrid"};
  if (!doc.valid() || !switchOnlyKeys(doc.root,keys,1) ||
      ShellyJson::field(doc.root,"meterGrid",grid) != 1) return 0;
  // END termine l'entete de 28 octets, AVANT longueur JSON + JSON. Pas de END final.
  const int head = snprintf(out,capacity,"APS11%04u0073%sEND%04u",unsigned(32+n),uid,unsigned(n));
  if (head != 32) { out[0] = '\0'; return 0; }
  memcpy(out+32,payload,n); out[32+n] = '\0'; return 32+n;
}

int switchPayloadLength(const uint8_t* frame, size_t n, const char* command) {
  if (!frame || n < 22 || ecuDeclaredLength(frame,n) != int(n) ||
      memcmp(frame+9,command,4) || memcmp(frame+n-3,"END",3)) return -1;
  size_t size = 0;
  for (size_t i=13;i<19;++i) {
    if (frame[i] < '0' || frame[i] > '9') return -1;
    if (i>=15) size = 10*size + size_t(frame[i]-'0');
  }
  return size+22 == n ? int(size) : -1;
}

bool switchEspRoleMatches(const char* json, size_t n, const char* id,
                          const char* host, const char* instance, const char* ip) {
  ShellyJson doc(json,n); ShellyJson::Value m{}, info{}, port{};
  const char* const outer[] = {"meterType","meterEnable","meterInfo3","status"};
  const char* const keys[] = {"id","hostName","instanceName","ip","port","type","company","enable","status"};
  if (!doc.valid() || ShellyJson::field(doc.root,"meterGrid",m) != 1 || !switchOnlyKeys(m,outer,4) ||
      !switchStringField(m,"meterType","3") || !switchStringField(m,"meterEnable","1") ||
      ShellyJson::field(m,"meterInfo3",info) != 1 || !switchOnlyKeys(info,keys,9)) return false;
  const char* expected[] = {id,host,instance,ip,nullptr,"3","prototype","1"};
  for (unsigned i=0;i<8;++i) if (i!=4 && !switchStringField(info,keys[i],expected[i])) return false;
  if (ShellyJson::field(info,"port",port) != 1) return false;
  double number = 0;
  return ShellyJson::stringEquals(port,"9527") ||
      (ShellyJson::number(info,"port",number) && number == 9527);
}

uint32_t switchHash(const uint8_t* bytes, size_t size) {
  uint32_t h = 2166136261UL;
  for (size_t i=0;i<size;++i) { h ^= bytes[i]; h *= 16777619UL; }
  return h;
}

void switchPut32(uint8_t* bytes, uint32_t value) {
  for (unsigned i=0;i<4;++i) bytes[i] = uint8_t(value >> (8*i));
}

uint32_t switchGet32(const uint8_t* bytes) {
  uint32_t value = 0;
  for (unsigned i=0;i<4;++i) value |= uint32_t(bytes[i]) << (8*i);
  return value;
}

void encodeSwitchJournal(uint8_t* out, uint8_t state, const uint8_t* ip, uint32_t sequence) {
  memset(out,0,36); memcpy(out,"EC08",4); out[4]=state; memcpy(out+5,ip,4);
  memcpy(out+9,EXPECTED_ECU_UID,12);
  switchPut32(out+21,switchHash(reinterpret_cast<const uint8_t*>(CT_GRID_BACKUP),sizeof(CT_GRID_BACKUP)-1));
  switchPut32(out+25,sequence); switchPut32(out+32,switchHash(out,32));
}

bool switchJournalValid(const uint8_t* data, size_t n) {
  return n == 36 && !memcmp(data,"EC08",4) && data[4] <= SWITCH_UNKNOWN &&
    !memcmp(data+9,EXPECTED_ECU_UID,12) &&
    switchGet32(data+21) == switchHash(reinterpret_cast<const uint8_t*>(CT_GRID_BACKUP),sizeof(CT_GRID_BACKUP)-1) &&
    switchGet32(data+32) == switchHash(data,32);
}
// END SOURCE_SWITCH_HELPERS

// BEGIN SOURCE_SWITCH_CORE
WiFiClient switchClient;
Preferences switchPrefs;
uint8_t switchJournalState = SWITCH_INITIAL;
uint32_t switchSequence = 0;
IPAddress switchPinnedIP;
bool switchStorageOk = false, switchPinnedValid = false;
bool switchRestore = false, switchActive = false, switchWriteAttempted = false;
bool switchPreflightMatches = false, switchReplyComplete = false;
bool switchGridMatches = false, switchPVMatches = false;

unsigned switchPhase = SWITCH_IDLE;
const char* switchStatus = "pret";
uint32_t switchStartedMs = 0, switchSelectSends = 0, switchRestoreSends = 0;
uint32_t switchSelectBytes = 0, switchRestoreBytes = 0;
size_t switchRequestSize = 0, switchSentBytes = 0, switchResponseSize = 0;
size_t switchPreflightSize = 0, switchPreflightSent = 0;
char switchRequest[SWITCH_REQUEST_CAPACITY] = {}, switchReplyCode[3] = {};
char switchEspAddress[16] = {};
uint8_t switchResponse[ECU_RESPONSE_CAPACITY] = {}, switchPreflightResponse[ECU_RESPONSE_CAPACITY] = {};

bool switchCanPollShelly() {
  return switchJobActive && !switchActive && !ecuPending && !ecuActive;
}

bool switchMayUseEsp() {
  return switchJournalState == SWITCH_SELECT_UNCERTAIN || switchJournalState == SWITCH_ESP_REPORTED ||
         switchJournalState == SWITCH_RESTORE_UNCERTAIN || switchJournalState == SWITCH_UNKNOWN;
}

bool persistSwitchState(uint8_t state, const IPAddress& peer) {
  const uint8_t ip[] = {peer[0],peer[1],peer[2],peer[3]};
  uint8_t blob[36], readback[36] = {};
  encodeSwitchJournal(blob,state,ip,switchSequence+1);
  if (!switchStorageOk || switchPrefs.putBytes("journal",blob,sizeof(blob)) != sizeof(blob) ||
      switchPrefs.getBytes("journal",readback,sizeof(readback)) != sizeof(readback) ||
      memcmp(blob,readback,sizeof(blob))) { switchStorageOk=false; return false; }
  ++switchSequence; switchJournalState=state; switchPinnedIP=peer; switchPinnedValid=true;
  return true;
}

void loadSwitchJournal() {
  switchStorageOk = switchPrefs.begin("ecu-bridge-v08",false);
  if (!switchStorageOk) { switchJournalState=SWITCH_UNKNOWN; switchStatus="memoire_indisponible"; return; }
  if (!switchPrefs.isKey("journal")) return;
  uint8_t blob[36] = {};
  if (switchPrefs.getBytesLength("journal") != sizeof(blob) ||
      switchPrefs.getBytes("journal",blob,sizeof(blob)) != sizeof(blob) || !switchJournalValid(blob,sizeof(blob))) {
    switchStorageOk=false; switchJournalState=SWITCH_UNKNOWN; switchStatus="journal_invalide"; return;
  }
  switchJournalState=blob[4]; switchSequence=switchGet32(blob+25);
  switchPinnedIP=IPAddress(blob[5],blob[6],blob[7],blob[8]); switchPinnedValid=true;
  ecuTarget=switchPinnedIP; ecuTargetText=ecuTarget.toString();
  switchStatus = switchMayUseEsp() ? "reprendre_ou_restaurer" : "tores_signales_precedemment";
}

String liveAllowedPeerText() {
  if (ecuIdentityUsable) return ecuTarget.toString();
  return switchMayUseEsp() && switchPinnedValid ? switchPinnedIP.toString() : String("");
}

bool switchPeerAllowed(const IPAddress& peer) {
  return switchMayUseEsp() && switchPinnedValid && meterId == EXPECTED_METER_ID &&
         ecuAddressIsLocalUnicast(switchPinnedIP) && peer == switchPinnedIP;
}

void serviceSwitchResume() {
  // Ne change JAMAIS les reglages ECU. Reprend uniquement la collecte/reponse fraiche
  // si une selection precedente est confirmee ou reste incertaine.
  if (!switchMayUseEsp() || switchResumeHeld || !switchPinnedValid ||
      !servicesStarted || !selfTestOk || meterId != EXPECTED_METER_ID ||
      !ecuAddressIsLocalUnicast(switchPinnedIP)) return;
  if (!shellyEnabled) {
    shellyEnabled=true; shellyDueNow=true; shellyReadOk=false; shellyStatus="en_attente";
  }
  if (!liveEnabled) { liveEnabled=true; liveStatus="reprise_attente_mesure_et_ecu"; }
}

void finishSourceSwitch(const char* reason) {
  switchClient.stop(); switchActive=false; switchJobActive=false; switchPhase=SWITCH_IDLE;
  switchStatus=reason;
  // L'etat persistant reste incertain en cas de timeout, envoi partiel ou coupure.
  // Jamais de renvoi automatique, meme au redemarrage de l'ESP32.
  Serial.print("Selection compteur : "); Serial.println(reason);
}

void inspectSwitchReply() {
  const int n = switchPayloadLength(switchResponse,switchResponseSize,"0073");
  switchReplyComplete=n>=0;
  if (n<0) { finishSourceSwitch("reponse_invalide_etat_incertain"); return; }
  memcpy(switchReplyCode,switchResponse+13,2); switchReplyCode[2]='\0';
  if (strcmp(switchReplyCode,"00")) { finishSourceSwitch("refus_ecu_verification_necessaire"); return; }
  const char* json=reinterpret_cast<const char*>(switchResponse+19);
  const String fullHost=hostName+".local";
  switchGridMatches = switchRestore ? switchCtRoleMatches(json,size_t(n),"meterGrid") :
    switchEspRoleMatches(json,size_t(n),meterId.c_str(),fullHost.c_str(),instanceName.c_str(),switchEspAddress);
  switchPVMatches=switchCtRoleMatches(json,size_t(n),"meterPV");
  if (!switchGridMatches) { finishSourceSwitch("reglage_reseau_non_confirme"); return; }
  const uint8_t state=switchRestore ? SWITCH_CT_REPORTED : SWITCH_ESP_REPORTED;
  const bool saved=persistSwitchState(state,ecuTarget);
  switchJournalState=state;
  if (switchRestore) stopLiveMeter("retour_tores_signale_par_ecu");
  if (!switchPVMatches) { finishSourceSwitch("reglage_reseau_confirme_production_differente"); return; }
  finishSourceSwitch(!saved ? "reglage_confirme_journal_non_sauve" :
    (switchRestore ? "tores_signales_par_ecu" : "esp32_signale_par_ecu"));
}

void serviceSourceSwitch() {
  if (!switchJobActive) return;
  if (WiFi.status()!=WL_CONNECTED) { finishSourceSwitch("wifi_perdu_etat_a_verifier"); return; }
  if (switchPhase==SWITCH_IDENTITY) {
    if (ecuPending || ecuActive) return;
    if (!ecuIdentityUsable || strcmp(ecuQueryStatus,"reponse_recue")) { finishSourceSwitch("identification_echouee"); return; }
    switchPhase=switchRestore ? SWITCH_WRITE_WAIT : SWITCH_PREFLIGHT_WAIT;
    switchStatus=switchRestore ? "attente_avant_retour_tores" : "attente_avant_precontrole";
  }
  if (switchPhase==SWITCH_PREFLIGHT_WAIT || switchPhase==SWITCH_WRITE_WAIT) {
    if (ecuWaitSeconds()) return;
    if (!ecuIdentityUsable || !ecuAddressIsLocalUnicast(ecuTarget) ||
        meterId!=EXPECTED_METER_ID) { finishSourceSwitch("identite_inattendue"); return; }
    const bool preflight=switchPhase==SWITCH_PREFLIGHT_WAIT;
    if (!preflight && !switchRestore && !switchPreflightMatches) {
      finishSourceSwitch("selection_annulee_precontrole_absent"); return;
    }
    if (!preflight && !switchRestore && (!mdnsReady || !shellyEnabled || !buildProfilePreview())) {
      finishSourceSwitch("selection_annulee_mesure_indisponible"); return;
    }
    char payload[SWITCH_REQUEST_CAPACITY-33];
    if (preflight) {
      if (!buildSourceReadRequest(switchRequest,sizeof(switchRequest),ecuUid)) {
        finishSourceSwitch("construction_impossible"); return;
      }
      switchRequestSize=28;
    } else {
      const String fullHost=hostName+".local";
      const String address=WiFi.localIP().toString();
      snprintf(switchEspAddress,sizeof(switchEspAddress),"%s",address.c_str());
      const size_t n=buildSwitchPayload(payload,sizeof(payload),switchRestore,meterId.c_str(),
        fullHost.c_str(),instanceName.c_str(),switchEspAddress);
      switchRequestSize=buildSourceWriteRequest(switchRequest,sizeof(switchRequest),ecuUid,payload,n);
      if (!switchRequestSize || switchWriteAttempted) { finishSourceSwitch("construction_impossible"); return; }
    }
    switchClient.stop(); lastEcuActionMs=millis();
    if (!switchClient.connect(ecuTarget,ECU_PORT,1500)) { finishSourceSwitch("connexion_impossible_aucun_envoi"); return; }
    switchClient.setTimeout(1); switchClient.setNoDelay(true);
    if (!preflight) {
      // Recontrole APRES connect(), qui peut durer ; avant la seule ecriture TCP.
      if (!switchRestore && (!shellyEnabled || !buildProfilePreview())) {
        finishSourceSwitch("selection_annulee_mesure_indisponible"); return;
      }
      const uint8_t state=switchRestore ? SWITCH_RESTORE_UNCERTAIN : SWITCH_SELECT_UNCERTAIN;
      const bool saved=persistSwitchState(state,ecuTarget);
      if (!saved && !switchRestore) { finishSourceSwitch("selection_annulee_memoire_indisponible"); return; }
      // Le retour aux tores reste possible meme si la memoire locale est en panne.
      switchJournalState=state; switchPinnedIP=ecuTarget; switchPinnedValid=true;
      if (!switchRestore && (!shellyEnabled || !buildProfilePreview())) {
        finishSourceSwitch("selection_annulee_mesure_indisponible"); return;
      }
      switchWriteAttempted=true; sourceProbeAttempted=false;
      if (switchRestore) ++switchRestoreSends;
      else { ++switchSelectSends; switchResumeHeld=false; liveEnabled=true; liveStatus="attente_requete_ecu"; }
    }
    switchStartedMs=millis(); lastEcuActionMs=switchStartedMs; switchActive=true;
    switchPhase=preflight ? SWITCH_PREFLIGHT_READ : SWITCH_WRITE_READ;
    switchStatus=preflight ? "precontrole_en_cours" : (switchRestore ? "retour_tores_envoye" : "selection_envoyee");
    const size_t sent=switchClient.write(reinterpret_cast<const uint8_t*>(switchRequest),switchRequestSize);
    if (preflight) switchPreflightSent=sent; else switchSentBytes=sent;
    if (!preflight) { if (switchRestore) switchRestoreBytes+=sent; else switchSelectBytes+=sent; }
    if (sent!=switchRequestSize) { finishSourceSwitch("envoi_incomplet_aucune_relance"); return; }
  }
  if (!switchActive) return;
  const bool preflight=switchPhase==SWITCH_PREFLIGHT_READ;
  uint8_t* response=preflight ? switchPreflightResponse : switchResponse;
  size_t& size=preflight ? switchPreflightSize : switchResponseSize;
  unsigned budget=1024;
  while (switchClient.available() && budget--) {
    const int byte=switchClient.read(); if (byte<0) break;
    if (size==ECU_RESPONSE_CAPACITY) { finishSourceSwitch("reponse_trop_grande"); return; }
    response[size++]=uint8_t(byte);
    const int expected=ecuDeclaredLength(response,size);
    if (expected<0 || expected>int(ECU_RESPONSE_CAPACITY)) { finishSourceSwitch("enveloppe_invalide"); return; }
    if (expected>0 && size==size_t(expected)) {
      switchClient.stop(); switchActive=false; lastEcuActionMs=millis();
      if (!preflight) { inspectSwitchReply(); return; }
      const int n=switchPayloadLength(response,size,"0072");
      const char* json=reinterpret_cast<const char*>(response+19);
      switchPreflightMatches=n>0 && !memcmp(response+13,"00",2) &&
        switchCtRoleMatches(json,size_t(n),"meterGrid") && switchCtRoleMatches(json,size_t(n),"meterPV");
      if (!switchPreflightMatches) { finishSourceSwitch("reglages_differents_selection_annulee"); return; }
      switchRequest[0]='\0'; switchRequestSize=0;
      switchPhase=SWITCH_WRITE_WAIT; switchStatus="precontrole_ok_attente_selection"; return;
    }
  }
  if (!switchClient.connected() && !switchClient.available()) { finishSourceSwitch("connexion_fermee_etat_a_verifier"); return; }
  if (uint32_t(millis()-switchStartedMs)>=SOURCE_READ_TIMEOUT_MS) finishSourceSwitch("delai_depasse_etat_a_verifier");
}
// END SOURCE_SWITCH_CORE

void handleSourceSwitchRequest() {
  web.sendHeader("Cache-Control","no-store");
  if (!web.hasArg("token") || web.arg("token")!=formToken) {
    web.send(400,"text/plain; charset=utf-8","Recharge la page avant une nouvelle action."); return;
  }
  const String action=web.arg("action");
  if (action=="cancel") {
    if (!switchJobActive || switchActive || switchWriteAttempted || ecuPending || ecuActive) {
      web.send(409,"text/plain; charset=utf-8","Cette opération ne peut plus être annulée ici. Consulte son résultat, puis utilise Retour aux tores."); return;
    }
    finishSourceSwitch("annule_avant_selection");
  } else {
    if (action!="select" && action!="restore") { web.send(400,"text/plain","Action inconnue."); return; }
    if (switchJobActive || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive ||
        searchPending || searchActive || ecuWaitSeconds()) {
      web.send(409,"text/plain; charset=utf-8","Attends la fin de l’opération et du délai d’une minute."); return;
    }
    IPAddress target; String address=web.arg("ecu_ip"); address.trim();
    if (WiFi.status()!=WL_CONNECTED || !target.fromString(address) || !ecuAddressIsLocalUnicast(target) ||
        meterId!=EXPECTED_METER_ID) {
      web.send(409,"text/plain; charset=utf-8","Vérifie le Wi-Fi, l’adresse locale de l’ECU et la carte ESP32 prévue."); return;
    }
    const bool restore=action=="restore";
    if (!restore && (switchMayUseEsp() || !switchStorageOk || !mdnsReady || !shellyEnabled || !buildProfilePreview())) {
      web.send(409,"text/plain; charset=utf-8","La sélection exige une mesure Shelly récente, l’annonce réseau et aucun précédent changement incertain. Utilise le relevé du choix de compteur ou Retour aux tores."); return;
    }
    switchRestore=restore; switchActive=false; switchWriteAttempted=false;
    switchPreflightMatches=switchReplyComplete=switchGridMatches=switchPVMatches=false;
    switchResponseSize=switchPreflightSize=switchSentBytes=switchPreflightSent=switchRequestSize=0;
    switchRequest[0]=switchReplyCode[0]='\0'; switchJobActive=true;
    // Identite relue pour chaque changement. Le repondeur deja actif continue.
    switchPhase=SWITCH_IDENTITY; switchStatus="identification_avant_changement";
    startEcuInfoQuery(target);
  }
  renewFormToken(); web.sendHeader("Location","/"); web.send(303,"text/plain","Operation prise en compte.");
}

void reconcileSourceSwitch(const char* json, size_t n) {
  // Releve manuel 0072 : seule une configuration reconnue leve l'incertitude.
  const String fullHost=hostName+".local", address=WiFi.localIP().toString();
  if (switchCtRoleMatches(json,n,"meterGrid")) {
    persistSwitchState(SWITCH_CT_REPORTED,ecuTarget); switchJournalState=SWITCH_CT_REPORTED;
    stopLiveMeter("tores_signales_par_releve"); switchStatus="tores_signales_par_releve";
  } else if (switchEspRoleMatches(json,n,meterId.c_str(),fullHost.c_str(),instanceName.c_str(),address.c_str())) {
    persistSwitchState(SWITCH_ESP_REPORTED,ecuTarget); switchJournalState=SWITCH_ESP_REPORTED;
    switchPinnedIP=ecuTarget; switchPinnedValid=true; switchStatus="esp32_signale_par_releve";
  } else { switchStatus="releve_non_reconnu"; }
}

const char* sourceSwitchLabel() {
  if (!strcmp(switchStatus,"pret")) return "Prêt pour un essai surveillé";
  if (!strcmp(switchStatus,"esp32_signale_par_ecu") || !strcmp(switchStatus,"esp32_signale_par_releve")) return "L’ECU indique le compteur ESP32 pour le réseau";
  if (!strcmp(switchStatus,"tores_signales_par_ecu") || !strcmp(switchStatus,"tores_signales_par_releve")) return "L’ECU indique le retour aux tores réseau";
  if (!strcmp(switchStatus,"identification_avant_changement")) return "Vérification de l’identité ECU";
  if (!strcmp(switchStatus,"attente_avant_precontrole")) return "Attente avant le contrôle des réglages";
  if (!strcmp(switchStatus,"precontrole_en_cours")) return "Contrôle des réglages réseau et production";
  if (!strcmp(switchStatus,"precontrole_ok_attente_selection")) return "Réglages conformes ; attente avant sélection";
  if (!strcmp(switchStatus,"attente_avant_retour_tores")) return "Attente avant le retour aux tores";
  if (!strcmp(switchStatus,"selection_envoyee")) return "Sélection envoyée ; attente de la réponse ECU";
  if (!strcmp(switchStatus,"retour_tores_envoye")) return "Retour aux tores envoyé ; attente de la réponse ECU";
  if (!strcmp(switchStatus,"reglages_differents_selection_annulee")) return "Réglages différents du relevé : sélection annulée";
  if (!strcmp(switchStatus,"reprendre_ou_restaurer")) return "Sélection précédente enregistrée ; répondeur repris avec mesures valides";
  if (!strcmp(switchStatus,"tores_signales_precedemment")) return "Dernier état enregistré : tores réseau";
  if (!strcmp(switchStatus,"selection_annulee_mesure_indisponible")) return "Sélection annulée : mesure Shelly indisponible";
  if (!strcmp(switchStatus,"annule_avant_selection")) return "Opération annulée avant sélection";
  return "Vérification nécessaire : consulte le détail ci-dessous et le diagnostic";
}

String sourceSwitchJsonFields() {
  String s=",\"source_switch_status\":\""+String(switchStatus)+"\"";
  s+=",\"source_switch_state\":"+String(switchJournalState)+",\"source_switch_running\":"+(switchJobActive ? "true" : "false");
  s+=",\"source_switch_restore\":"; s+=switchRestore ? "true" : "false";
  s+=",\"source_switch_nvs_ok\":"; s+=switchStorageOk ? "true" : "false";
  s+=",\"source_switch_sequence\":"+String(switchSequence);
  s+=",\"source_switch_pinned_ip\":\""+(switchPinnedValid ? switchPinnedIP.toString() : String(""))+"\"";
  s+=",\"source_switch_preflight_matches_backup\":"; s+=switchPreflightMatches ? "true" : "false";
  s+=",\"source_switch_preflight_sent\":"+String(switchPreflightSent);
  s+=",\"source_switch_preflight_hex\":\""+hexOf(switchPreflightResponse,switchPreflightSize)+"\"";
  s+=",\"source_switch_request_hex\":\""+hexOf(reinterpret_cast<const uint8_t*>(switchRequest),switchRequestSize)+"\"";
  s+=",\"source_switch_bytes_sent\":"+String(switchSentBytes);
  s+=",\"source_switch_response_hex\":\""+hexOf(switchResponse,switchResponseSize)+"\"";
  s+=",\"source_switch_reply_complete\":"; s+=switchReplyComplete ? "true" : "false";
  s+=",\"source_switch_reply_code\":\""+String(switchReplyCode)+"\"";
  s+=",\"source_switch_grid_matches\":"; s+=switchGridMatches ? "true" : "false";
  s+=",\"source_switch_pv_matches_backup\":"; s+=switchPVMatches ? "true" : "false";
  s+=",\"source_select_attempts_this_boot\":"+String(switchSelectSends);
  s+=",\"source_restore_attempts_this_boot\":"+String(switchRestoreSends);
  s+=",\"source_selection_write_sent\":"; s+=switchSelectBytes ? "true" : "false";
  s+=",\"source_restore_write_sent\":"; s+=switchRestoreBytes ? "true" : "false";
  s+=",\"source_manual_restore_available\":true,\"source_automatic_restore_available\":false";
  s+=",\"source_ct_backup\":"+String(CT_GRID_BACKUP);
  return s;
}

String sourceSwitchHtml() {
  String h=F("<section><h2>Sélection du compteur réseau</h2>"
    "<p>Source : Shelly local, canal 1, maison monophasée. Le compteur de production reste sur les tores.</p>"
    "<p><strong>Essai surveillé :</strong> le contrôle des réglages et leur changement peuvent relancer le programme principal de l’ECU. N’envoie pas d’autres réglages depuis EMA Manager pendant l’essai.</p>"
    "<p>Un clic vérifie l’identité, contrôle les réglages de tores, puis sélectionne l’ESP32. Une minute sépare chaque commande ; prévoir environ deux minutes.</p>");
  h+="<p>État : <strong>"+String(sourceSwitchLabel())+"</strong><br><small>"+String(switchStatus)+"</small></p>";
  if (switchJobActive && ecuWaitSeconds()) h+="<p>Prochaine étape dans "+String(ecuWaitSeconds())+" s.</p>";
  const bool busy=switchJobActive || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds();
  h+="<form method='post' action='/source-switch'><input type='hidden' name='token' value='"+formToken+"'>";
  h+="<p><label>IP ECU <input name='ecu_ip' required value='"+ecuTargetText+"'></label></p><p><button name='action' value='select'";
  if (busy || switchMayUseEsp() || !switchStorageOk || !mdnsReady || !shellyEnabled || !shellyMeasurementValid()) h+=" disabled";
  h+=F(">Sélectionner l’ESP32 pour le réseau</button></p><p><button name='action' value='restore'");
  if (busy) h+=" disabled";
  h+=F(">Retour aux tores réseau</button></p></form>");
  if (switchJobActive && !switchActive && !switchWriteAttempted && !ecuPending && !ecuActive)
    h+="<form method='post' action='/source-switch'><input type='hidden' name='token' value='"+formToken+"'><button name='action' value='cancel'>Annuler avant sélection</button></form>";
  h+=F("<p>Le bouton demande le retour aux réglages réseau relevés dans ton diagnostic v0.7.1. La sauvegarde reste disponible après redémarrage de l’ESP32 ; la réponse de l’ECU doit confirmer le retour. Il nécessite une connexion à l’ECU, mais aucune mesure Shelly. Le champ PH et les rapports de tores sont conservés.</p>"
    "<p>Une coupure Wi-Fi ne rétablit pas les tores. Après reconnexion, la collecte et le répondeur reprennent si une sélection ESP32 a été enregistrée. Aucune mesure périmée ni zéro de remplacement n’est envoyé.</p>"
    "<p>La réponse de configuration et les trames transmises ne prouvent pas encore l’utilisation des mesures par la régulation.</p>"
    "<p><a href='/diagnostic.json'>Télécharger le diagnostic v0.8.8</a></p></section>");
  return h;
}


String test1000State() {
  return "{\"id\":\"" + String(test1000.id) + "\",\"restant_ms\":" +
         String(test1000.remaining(millis())) + "}";
}

void readTest1000() {
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", "{\"token\":\"" + formToken + "\",\"essai\":" + test1000State() + "}");
}

void changeTest1000() {
  web.sendHeader("Cache-Control", "no-store");
  if (!web.hasArg("token") || web.arg("token") != formToken) {
    web.send(409, "text/plain", "Jeton expire. Aucune action."); return;
  }
  const String action = web.arg("action"), id = web.arg("session");
  if (action == "arreter" && id.length() == 32 && id == test1000.id) {
    test1000.stop();
  } else if (action == "demarrer") {
    if (switchJournalState != SWITCH_ESP_REPORTED || switchJobActive || !selfTestOk ||
        !liveEnabled || !shellyEnabled || !shellyMeasurementValid() || !meterClientActive ||
        !meterClient.connected() || !meterConnectionReplies ||
        uint32_t(millis() - liveLastTxMs) > 60000 ||
        meterClient.remoteIP() != IPAddress(0,0,0,0) || !livePeerAllowed(meterClient.remoteIP())) {
      web.send(409, "text/plain", "Attendre une reponse fraiche a l'ECU avec l'ESP32 selectionne."); return;
    }
    if (!test1000.start(id.c_str(), millis())) {
      web.send(409, "text/plain", "Essai en cours, deja utilise ou identifiant invalide."); return;
    }
  } else {
    web.send(400, "text/plain", "Commande ou session inconnue."); return;
  }
  renewFormToken();
  if (web.hasArg("page")) {
    web.sendHeader("Location", "/"); web.send(303, "text/plain", "Retour a la mesure reelle.");
  } else {
    web.send(200, "application/json", test1000State());
  }
}

String statusJson() {
  // Les chaines variables ci-dessous sont des identifiants generes ou de l'hex.
  String s = "{\"version\":\"v0.8.8-compteur-reseau\",\"mode\":\"compteur_reseau_manuel_et_shelly\"";
  s += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  s += ",\"host\":\"" + hostName + ".local\"";
  s += ",\"instance\":\"" + instanceName + "\"";
  s += ",\"meter_id\":\"" + meterId + "\"";
  s += ",\"meter_port\":" + String(METER_PORT);
  s += ",\"mdns_ready\":"; s += mdnsReady ? "true" : "false";
  s += ",\"self_test_ok\":"; s += selfTestOk ? "true" : "false";
  s += ",\"connections\":" + String(connectionCount);
  s += ",\"bytes_received\":" + String(receivedBytes);
  s += ",\"last_peer\":\"" + lastPeer + "\"";
  s += ",\"capture_truncated\":"; s += captureTruncated ? "true" : "false";
  s += ",\"capture_hex\":\"" + hexOf(captureData, captureSize) + "\"";
  s += ",\"first_block_plain_hex\":\"" + decodedHex + "\"";
  s += ",\"expected_profile_read\":"; s += expectedRead ? "true" : "false";
  s += ecuInfoJsonFields();
  s += searchJsonFields();
  s += meterConfigJsonFields();
  s += shellyJsonFields();
  s += profilePreviewJsonFields();
  s += wifiJsonFields();
  s += tcpBenchJsonFields();
  s += sourceReadJsonFields();
  s += liveMeterJsonFields();
  s += ",\"test_1000\":" + test1000State();
  s += sourceSwitchJsonFields();
  s += ",\"ecu_expected_version\":\"" + String(EXPECTED_ECU_VERSION) + "\"";
  s += ",\"ecu_identity_matches_expected\":"; s += ecuIdentityUsable ? "true" : "false";
  s += ",\"loop_max_gap_ms\":" + String(loopMaxGapMs);
  s += ",\"loop_max_body_ms\":" + String(loopMaxBodyMs);
  s += ",\"web_nonblocking_io\":true,\"web_max_service_ms\":" + String(web.maxServiceMs);
  s += ",\"web_requests\":" + String(web.requests) + ",\"web_timeouts\":" + String(web.timeouts);
  s += ",\"uptime_s\":" + String(millis() / 1000) + "}";
  return s;
}

void handleHome() {
  String html = F("<!doctype html><html lang='fr'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>");
  if (switchJobActive || liveEnabled || sourceJobActive || benchActive || configJobActive || ecuPending || ecuActive || searchPending || searchActive || ecuWaitSeconds()) {
    html += F("<script>let editing=false;addEventListener('input',()=>editing=true);setInterval(()=>{if(!editing&&!document.querySelector('input:focus'))location.reload()},5000);</script>");
  } else if (shellyEnabled) {
    html += F("<script>async function refreshShelly(){const ctl=new AbortController();let timer;try{"
      "const panel=document.getElementById('shelly-panel');if(!panel||panel.contains(document.activeElement))return;"
      "timer=setTimeout(()=>ctl.abort(),5000);const r=await fetch('/shelly-panel',{cache:'no-store',signal:ctl.signal});"
      "if(r.ok){const text=await r.text();if(!panel.contains(document.activeElement))panel.innerHTML=text;}"
      "}catch(e){}finally{clearTimeout(timer);setTimeout(refreshShelly,2000);}}"
      "addEventListener('load',()=>setTimeout(refreshShelly,2000));</script>");
  }
  html += F("<title>ECU-C / ESP32 - diagnostic</title><style>"
    "body{font:17px system-ui;max-width:780px;margin:30px auto;padding:0 18px;color:#173047;background:#f4f7fa}"
    "h1{font-size:26px}h2{font-size:21px}section{background:white;border:1px solid #d9e2ea;border-radius:12px;padding:20px;margin:20px 0}"
    "code{overflow-wrap:anywhere}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px;background:#f4f7fa;padding:12px}a{color:#135da7}strong{color:#12523d}input,button{font:inherit;padding:10px;border-radius:6px;border:1px solid #aac0d1}"
    "button{background:#135da7;color:white;cursor:pointer}button:disabled{opacity:.5;cursor:default}</style></head><body>"
    "<h1>ECU-C / ESP32</h1><p>v0.8.8 · connexion Shelly permanente</p>");
  html += wifiHtml();
  html += "<div id='shelly-panel'>" + shellyHtml() + "</div>";
  const uint32_t remaining = test1000.remaining(millis());
  if (remaining) {
    html += "<section><h2>Essai +1 000 W</h2><p>Retour à la mesure réelle dans " +
      String((remaining + 999) / 1000) + " s.</p><form method='post' action='/test-1000'>" +
      "<input type='hidden' name='token' value='" + formToken +
      "'><input type='hidden' name='session' value='" + String(test1000.id) +
      "'><input type='hidden' name='page' value='1'><button name='action' value='arreter'>Arrêter le décalage</button></form></section>";
  }
  html += sourceSwitchHtml();
  html += liveMeterHtml();
  html += meterContinuityHtml();
  html += "<details><summary>Contrôles précédents et outils avancés</summary>";
  html += sourceReadHtml();
  html += tcpBenchHtml();
  html += profilePreviewHtml();
  html += meterConfigHtml();
  html += ecuInfoHtml();
  html += meterSearchHtml();
  html += F("<section><h2>Annonce du compteur</h2>"
    "<p>L’annonce réseau reste active. Les réponses nécessitent une activation manuelle et une mesure Shelly valide.</p>");
  html += "<p>Nom : <code>" + instanceName + "</code></p>";
  html += "<p>Adresse ESP32 : <code>" + WiFi.localIP().toString() + "</code></p>";
  html += "<p>Service mDNS : " + String(mdnsReady ? "démarré" : "échec") + "</p>";
  html += "<p>Autotest du déchiffrement : " + String(selfTestOk ? "réussi" : "échec") + "</p>";
  html += "<p>Connexions reçues par le compteur : " + String(connectionCount) + "</p>";
  html += "<p>Octets reçus par le compteur : " + String(receivedBytes) + "</p>";
  html += "<p>Requête de mesure attendue : " + String(expectedRead ? "reconnue" : "pas encore reçue") + "</p>";
  html += F("<p>Ces compteurs sont distincts de la lecture des informations ECU ci-dessus.</p></section>"
    "</details><p><a href='/status'>Voir le diagnostic JSON</a></p></body></html>");
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/html; charset=utf-8", html);
}

void beginServices() {
  meterServer.begin();
  meterServer.setNoDelay(true);
  web.begin();
  mdnsReady = MDNS.begin(hostName.c_str());
  if (mdnsReady) {
    MDNS.setInstanceName(instanceName.c_str());
    MDNS.addService("http", "tcp", METER_PORT);
    MDNS.addServiceTxt("http", "tcp", "ID", meterId.c_str());
    MDNS.addServiceTxt("http", "tcp", "type", "3");
    MDNS.addServiceTxt("http", "tcp", "company", "prototype");
  }
  servicesStarted = true;
  Serial.print("Page de diagnostic : http://"); Serial.println(WiFi.localIP());
  Serial.print("Nom annonce : "); Serial.println(instanceName);
  Serial.println("Port 9527 : observation sauf selection precedente enregistree ; aucun changement ECU automatique.");
}

void endServices() {
  stopLiveMeter("wifi_perdu");
  if (switchJobActive) finishSourceSwitch("wifi_perdu_etat_a_verifier");
  if (sourceJobActive) finishSourceRead("wifi_perdu");
  if (benchActive) finishTcpBench("wifi_perdu");
  shellyEnabled = false; shellyDueNow = false;
  finishShellyQuery("wifi_perdu");
  ecuIdentityUsable = false;
  if (searchPending || searchActive) finishMeterSearch("wifi_perdu");
  if (ecuPending || ecuActive) finishEcuQuery("wifi_perdu");
  if (configJobActive) finishMeterConfigRead("wifi_perdu");
  meterClient.stop(); meterServer.end(); web.stop(); MDNS.end();
  servicesStarted = false; mdnsReady = false;
}


void setup() {
  Serial.begin(115200);
  wifiResetReason = int(esp_reset_reason());
  Serial.printf("Firmware v0.8.8 - reset=%d (%s)\n", wifiResetReason, resetReasonLabel(wifiResetReason));
  Serial.printf("Arduino-ESP32 %d.%d.%d ; ESP-IDF %s\n", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH, esp_get_idf_version());
  renewFormToken();
  char suffix[13];
  snprintf(suffix, sizeof(suffix), "%012llX", (unsigned long long)(ESP.getEfuseMac() & 0xffffffffffffULL));
  // BEGIN METER_IDENTITY
  meterId = String("LAB") + suffix;
  // C1.2.27A : prefixe M01 sensible a la casse, puis ID IPC exact apres +3.
  // La meme chaine sert au Wi-Fi, a mDNS et au hostName transmis a l'ECU.
  hostName = String("M01") + meterId;
  instanceName = String("APS-METER-ESP32-") + suffix;
  // END METER_IDENTITY
  loadSwitchJournal();
  selfTestOk = localSelfTest();
  Serial.print("Autotest local : "); Serial.println(selfTestOk ? "OK" : "ECHEC");
  web.on("/", ACC_HTTP_GET, handleHome);
  web.on("/ecu-info", ACC_HTTP_POST, handleEcuInfoRequest);
  web.on("/meter-search", ACC_HTTP_POST, handleMeterSearchRequest);
  web.on("/meter-config-read", ACC_HTTP_POST, handleMeterConfigReadRequest);
  web.on("/shelly-read", ACC_HTTP_POST, handleShellyRequest);
  web.on("/tcp-bench", ACC_HTTP_POST, handleTcpBenchRequest);
  web.on("/source-read", ACC_HTTP_POST, handleSourceReadRequest);
  web.on("/source-switch", ACC_HTTP_POST, handleSourceSwitchRequest);
  web.on("/live-meter", ACC_HTTP_POST, handleLiveMeterRequest);
  web.on("/meter-reconnect", ACC_HTTP_POST, handleMeterReconnectRequest);
  web.on("/test-1000", ACC_HTTP_GET, readTest1000);
  web.on("/test-1000", ACC_HTTP_POST, changeTest1000);
  web.on("/shelly-panel", ACC_HTTP_GET, []() {
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "text/html; charset=utf-8", shellyHtml());
  });
  web.on("/status", ACC_HTTP_GET, []() {
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", statusJson());
  });
  web.on("/diagnostic.json", ACC_HTTP_GET, []() {
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("Content-Disposition", "attachment; filename=diagnostic_esp32_ecu_v088.json");
    web.send(200, "application/json", statusJson());
  });
  WiFi.onEvent(recordWifiEvent);
  initializeWifiRecovery();
}

void loop() {
  const uint32_t loopAt=millis();
  test1000.remaining(loopAt);
  if (switchJobActive) test1000.stop();
  if (loopSeen && uint32_t(loopAt-loopLastMs)>loopMaxGapMs) loopMaxGapMs=uint32_t(loopAt-loopLastMs);
  loopSeen=true; loopLastMs=loopAt;
  serviceWifiRecovery();
  serviceWifiDiagnostics();
  const WifiTrace trace = wifiSnapshot();
  if (wifiHandledLosses != trace.connectionLosses) {
    wifiHandledLosses = trace.connectionLosses;
    // Une coupure/reconnexion rapide peut survenir pendant une requete Web.
    if (servicesStarted) endServices();
  }
  const bool wifiOk = WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
  if (wifiOk && !servicesStarted) beginServices();
  if (!wifiOk && servicesStarted) endServices();
  if (wifiOk) {
    serviceSwitchResume();
    serviceMeterTcp();
    serviceShellyQuery();
    web.handleClient();
    serviceMeterTcp(); // Priorite aux octets arrives pendant le traitement Web.
    serviceEcuQuery(); serviceMeterSearch(); serviceMeterConfigRead();
    serviceSourceRead(); serviceSourceSwitch(); serviceTcpBench();
    serviceShellyQuery();
    serviceMeterTcp(); // Reprise immediate apres la collecte Shelly.
  }
  const uint32_t loopDuration=uint32_t(millis()-loopAt);
  if (loopDuration>loopMaxBodyMs) loopMaxBodyMs=loopDuration;
  delay(2);
}
