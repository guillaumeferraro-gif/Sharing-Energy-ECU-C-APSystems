"""Relevé direct ECU-C, avec un seul essai +1000 W de 90 secondes sur l’ESP32."""
import hashlib
from http import client as http_client
import io
import json
import math
import os
from pathlib import Path
import re
import secrets
import socket
import sys
import tarfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from email.utils import parsedate_to_datetime
from urllib.parse import urlencode
from onduleurs import read_rows, new_telemetry
import bridage

ECU = "IP_ECU"
ESP = "IP_ESP"
SHELLY = "IP_SHELLY"
UID = "000000000000"
MODEL = "ECU-C-Z_C1.2.27A"
METER = "LAB000000000000"
STAGE = "/home/update_from_app/update_localweb/assist"
GRID = "/tmp/gridQuickPower.conf"
TEMPLATE_HASH = "b325e84faef2b7b49e4c914ca1d7b4a0e9171546272d1a788387b9f03049cece"
N_POINTS = 301


def http(host, path, body=None, content_type=None, timeout=7, limit=256 * 1024):
    """Direct LAN request. Fixed hosts, one request, no proxies or redirects."""
    if host not in (ECU, ESP, SHELLY) or (host == SHELLY and body is not None):
        raise ValueError("Destination inattendue")
    conn = http_client.HTTPConnection(host, timeout=timeout)
    headers = {"Connection": "close", "Cache-Control": "no-cache", "Accept-Encoding": "identity"}
    if content_type:
        headers["Content-Type"] = content_type
    start = time.time()
    try:
        conn.request("GET" if body is None else "POST", path, body=body, headers=headers)
        response = conn.getresponse()
        data = response.read(limit + 1)
        if len(data) > limit:
            raise ValueError("Réponse trop volumineuse")
        return response.status, data, response.getheader("Date"), [start, time.time()]
    finally:
        conn.close()


def get_json(host, path, **kwargs):
    status, raw, _, bracket = http(host, path, **kwargs)
    if status != 200:
        raise ValueError("HTTP " + str(status))
    return json.loads(raw), bracket


def ecu_identity():
    # 0001 is the same identity-only request used in the validated sketch.
    deadline = time.monotonic() + 10
    with socket.create_connection((ECU, 8899), timeout=6) as conn:
        conn.settimeout(7)
        conn.sendall(b"APS1100160001END")
        msg = bytearray()
        length = 9
        while len(msg) < length:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError("Délai de lecture de l’identité ECU dépassé")
            conn.settimeout(min(left, 7))
            chunk = conn.recv(length - len(msg))
            if not chunk:
                raise ValueError("Réponse d'identité ECU incomplète")
            msg.extend(chunk)
            if len(msg) == 9 and length == 9:
                if msg[:5] not in (b"APS11", b"APS12") or not msg[5:9].isdigit():
                    raise ValueError("En-tête d'identité ECU invalide")
                length = int(msg[5:9])
                if not 28 <= length <= 4096:
                    raise ValueError("Longueur d'identité ECU invalide")
    model_field = (f"{len(MODEL):03d}" + MODEL).encode("ascii")
    if msg[9:25] != ("0001" + UID).encode() or msg[-3:] != b"END" or model_field not in msg[25:-3]:
        raise ValueError("Identité ou version de l'ECU différente de celle attendue")


def reader_package(template, token):
    if hashlib.sha256(template).hexdigest() != TEMPLATE_HASH:
        raise ValueError("Le fichier lecture.sh diffère de la version du kit")
    if re.fullmatch(r"[0-9a-f]{32}", token) is None:
        raise ValueError("Identifiant de session invalide")
    script = template.replace(b"__SESSION__", token.encode("ascii"))
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:bz2", format=tarfile.USTAR_FORMAT) as tar:
        entry = tarfile.TarInfo("update_localweb/assist")
        entry.size, entry.mode = len(script), 0o755
        tar.addfile(entry, io.BytesIO(script))
    return buf.getvalue()


def remote_file_exists(path):
    body = urlencode({"src": path}).encode("ascii")
    data, _ = get_json(ECU, "/index.php/management/isexist_file", body=body,
                       content_type="application/x-www-form-urlencoded", limit=4096)
    v = str(data.get("value"))
    if v not in ("0", "-1"):
        raise ValueError("Présence du fichier impossible à vérifier")
    return v == "0"


def upload_once(package):
    boundary = "acc" + secrets.token_hex(16)
    head = ("--" + boundary + '\r\nContent-Disposition: form-data; name="file"; '
            'filename="lecture.tar.bz2"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
    body = head + package + ("\r\n--" + boundary + "--\r\n").encode()
    status, response, _, _ = http(ECU, "/index.php/management/exec_upgrade_ecu_app", body,
                                  "multipart/form-data; boundary=" + boundary, timeout=15, limit=16384)
    if status != 200:
        return "HTTP " + str(status)
    try:
        return "res=" + str(json.loads(response).get("res"))
    except (ValueError, AttributeError):
        return "réponse de lancement non reconnue"


def integer(value):
    if isinstance(value, bool) or re.fullmatch(r"-?[0-9]{1,14}", str(value).strip()) is None:
        return None
    return int(value)


def power(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return value if math.isfinite(value) and abs(value) <= 30000 else None


def shelly_read():
    data, stamp = get_json(SHELLY, "/rpc/Shelly.GetStatus", timeout=5)
    channel, system = data.get("em1:1", {}), data.get("sys", {})
    if str(system.get("mac", "")).upper() != "000000000000" or channel.get("id") != 1:
        raise ValueError("Identité ou canal Shelly inattendu")
    if channel.get("errors", []) != [] or power(channel.get("act_power")) is None:
        raise ValueError("Puissance du canal 1 indisponible")
    return {"pc_s": stamp, "shelly_epoch_s": integer(system.get("unixtime")), "w": channel["act_power"]}


def inverter_read():
    status, data, date, bracket = http(ECU, "/index.php/realtimedata", timeout=5)
    if status != 200:
        raise ValueError("Page locale des micro-onduleurs HTTP " + str(status))
    return {"pc_interval_s": bracket, "date_http": date, "canaux": read_rows(data)}


class ShellySampler:
    """Une seule lecture à la fois, indépendante de l'affichage plus lent de l'ECU."""
    def __init__(self, destination):
        self.destination = destination
        self.pending = []
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.worker = threading.Thread(target=self.collect, name="Shelly-direct")

    def collect(self):
        while not self.stop.is_set():
            start, wall = time.monotonic(), time.time()
            sample = safe(shelly_read)
            sample.setdefault("pc_s", [wall, time.time()])
            with self.lock:
                self.pending.append(sample)
            self.stop.wait(max(0.1, 2 - (time.monotonic() - start)))

    def __enter__(self):
        self.worker.start()
        return self

    def drain(self):
        with self.lock:
            self.destination.extend(self.pending)
            self.pending.clear()

    def __exit__(self, *args):
        self.stop.set()
        self.worker.join()
        self.drain()


def cache_value(stamp1, stamp2, value, now):
    a, b = integer(stamp1), integer(stamp2)
    if a != b:
        return {"w": None, "mtime_s": None, "age_s": None, "status": "en_cours_ecriture"}
    if value is None:
        return {"w": None, "mtime_s": b, "age_s": None, "status": "indisponible"}
    return {"w": value, "mtime_s": b, "age_s": now - b if b is not None else None,
            "status": "lu" if b is not None else "date_inconnue"}


def safe(fn, *args):
    try:
        return fn(*args)
    except (OSError, ValueError, TypeError, KeyError, AttributeError, http_client.HTTPException) as exc:
        return {"erreur": str(exc)[:240]}


def save(path, report):
    temp = path.with_suffix(".part")
    temp.write_text(json.dumps(report, ensure_ascii=False, indent=1, allow_nan=False), encoding="utf-8")
    os.replace(temp, path)


def watts(value):
    return "indisponible" if value is None else f"{value:+g} W"


def duration(rows):
    try:
        return int(float(rows[-1]["uptime_ecu_s"])) - int(float(rows[0]["uptime_ecu_s"]))
    except (ValueError, OverflowError):
        return rows[-1]["epoch_ecu_s"] - rows[0]["epoch_ecu_s"]


def esp_read():
    data, bracket = get_json(ESP, "/status", timeout=5)
    if data.get("version") != "v0.8.8-compteur-reseau":
        raise ValueError("Cet essai demande la v0.8.8. Le relevé attend le compteur ESP32 v0.8.8 déjà utilisé pour la première signature.")
    if data.get("meter_id") != METER or data.get("ecu_uid") not in (UID, ""):
        raise ValueError("Identité de l’ESP32 ou ECU associé inattendus")
    # Au redémarrage la v0.8.7 conservait déjà le choix et l'IP dans son journal NVS.
    # Le PC vérifie séparément l'identité réelle de l'ECU, puis les écritures Grid.
    selection = (data.get("live_meter_allowed_peer") == ECU
                 and data.get("source_switch_state") == 2
                 and data.get("source_switch_running") is False
                 and data.get("live_meter_enabled") is True)
    pulse = data.get("test_1000")
    if (not isinstance(pulse, dict) or not isinstance(pulse.get("id"), str)
            or not re.fullmatch(r"(?:[0-9a-f]{32})?", pulse["id"])
            or type(pulse.get("restant_ms")) is not int or not 0 <= pulse["restant_ms"] <= 90000):
        raise ValueError("État du test ESP32 illisible.")
    last = integer(data.get("meter_tcp_last_request_uptime_ms"))
    age = integer(data.get("meter_tcp_last_request_age_ms"))
    records = data.get("meter_exchanges")
    if not isinstance(records, list):
        raise ValueError("Historique ESP32 inattendu")
    history = []
    for raw in records:
        if not isinstance(raw, dict) or raw.get("sent_bytes") != 192 or power(raw.get("power_w")) is None:
            continue
        event = {k: raw.get(k) for k in ("connection", "uptime_ms", "power_w", "shelly_sample_w",
                                         "offset_w", "test_id", "sample_age_ms", "service_ms")}
        value, offset, tag = power(event["shelly_sample_w"]), event["offset_w"], event["test_id"]
        if (value is None or type(offset) is not int or offset not in (0, 1000)
                or not isinstance(tag, str) or not re.fullmatch(r"(?:[0-9a-f]{32})?", tag)
                or (offset and not tag) or abs(value + offset - event["power_w"]) > .005):
            continue
        at = integer(event["uptime_ms"])
        if None not in (last, age, at) and min(last, age, at) >= 0:
            delay = (last + age - at) % (2 ** 32)
            if delay < 2 ** 31:
                # Uptime du début de la demande. L'écriture tombe dans les millisecondes de service.
                service = integer(event["service_ms"])
                if service is not None and 0 <= service <= 5000:
                    event["envoi_pc_s"] = [round(bracket[0] - delay / 1000, 3),
                                           round(bracket[1] + (service - delay) / 1000, 3)]
        history.append(event)
    return {"pc_s": bracket, "selection_esp_confirmee": selection, "envois": history, "test_1000": pulse,
            "shelly_w": power(data.get("shelly_power_w")) if data.get("shelly_measurement_valid") is True else None,
            "age_shelly_ms": integer(data.get("shelly_age_ms"))}


def decode_trace(content, token):
    # Le serveur peut transmettre la fin d'une ligne pendant son écriture.
    lines = content.split(b"\n")[:-1]
    if not lines:
        return [], False
    if lines[0] != f"ACCB1\t{token}\t{N_POINTS}".encode("ascii"):
        raise ValueError("Marqueur ou session du relevé ECU incohérent")
    rows = []
    finished = False
    config = None
    pending_config = False
    for line in lines[1:]:
        if finished:
            raise ValueError("Données après la fin de la trace")
        if line.startswith(b"CFG\t"):
            parts = line.split(b"\t", 2)
            if (len(parts) != 3 or parts[1] != str(len(rows) + 1).encode() or pending_config
                    or len(rows) >= N_POINTS):
                raise ValueError("Chronologie de la configuration ECU illisible")
            config = bridage.parse_config(parts[2])
            pending_config = True
            continue
        fields = line.decode("ascii").split("\t")
        if (fields == ["FIN", str(len(rows))] and 0 < len(rows) <= N_POINTS
                and not pending_config and (len(rows) == N_POINTS or duration(rows) >= 299)):
            finished = True
            continue
        if config is None or len(fields) != 10 or integer(fields[0]) != len(rows) + 1 or len(rows) >= N_POINTS:
            raise ValueError("Ligne de la trace incomplète ou dans le désordre")
        pending_config = False
        _, epoch, uptime, m1, w, m2, valid, j1, json_text, j2 = fields
        now = integer(epoch)
        if now is None:
            raise ValueError("Horloge ECU illisible")
        try:
            detail = json.loads(json_text)
        except ValueError:
            detail = None
        values = {}
        for role in ("meterGrid", "meterPV"):
            section = detail.get(role) if isinstance(detail, dict) else None
            total = integer(section.get("ALL")) if isinstance(section, dict) else None
            values[role] = cache_value(j1, j2, total, now)
        rows.append({"n": len(rows) + 1, "epoch_ecu_s": now, "uptime_ecu_s": uptime,
                     "grid": cache_value(m1, m2, integer(w), now),
                     "statut_grid": int(valid) if valid in ("0", "1") else None, "reglage": config, **values})
    return rows, finished


def trace_read(token):
    resource = "/resources/acc_bridage_" + token
    status, body, date, bracket = http(ECU, resource + "/points.txt?t=" + str(time.time_ns()), limit=2*1024*1024)
    if status == 404:
        return {"en_attente": True}
    if status != 200:
        raise ValueError("Trace ECU HTTP " + str(status))
    rows, finished = decode_trace(body, token)
    server = None
    if date:
        try:
            stamp = parsedate_to_datetime(date)
            if stamp.tzinfo is not None:
                server = int(stamp.timestamp())
        except (ValueError, TypeError, OverflowError):
            pass
    return {"lignes": rows, "fin": finished, "date_http_s": server, "pc_s": bracket}


def summarize(report):
    rows = report["points_ecu"]
    observed = report["observations"]
    events = {}
    clocks = []
    for ob in observed:
        if ob["esp"].get("selection_esp_confirmee"):
            for e in ob["esp"].get("envois", []):
                limits = e.get("envoi_pc_s")
                if limits:
                    # L'intervalle PC distingue aussi deux événements après un redémarrage de l'ESP.
                    key = (e["connection"], e["uptime_ms"], e["power_w"],
                           e["shelly_sample_w"], e["offset_w"], e["test_id"])
                    existing = events.setdefault(key, [])
                    for i, (a, b) in enumerate(existing):
                        if limits[0] <= b and a <= limits[1]:
                            existing[i] = (max(a, limits[0]), min(b, limits[1]))
                            break
                    else:
                        existing.append(tuple(limits))
        meta = ob["ecu_http"]
        server, bracket, n = meta.get("date_http_s"), meta.get("pc_s"), meta.get("dernier_n", 0)
        if (server is not None and bracket and 0 <= bracket[1] - bracket[0] <= 3 and 0 < n <= len(rows)
                and -1 <= server - rows[n - 1]["epoch_ecu_s"] <= 8):
            clocks.append((server, bracket))
    results = []
    previous = None
    for row in rows:
        cache = row["grid"]
        m = cache["mtime_s"]
        if cache["status"] != "lu" or m is None:
            continue
        old, previous = previous, m
        if old is None or m <= old or not 0 <= cache["age_s"] <= 5:
            continue
        item = {"n": row["n"], "w_ecu": cache["w"], "mtime_ecu_s": m,
                "statut_grid": row["statut_grid"], "envois_proches": []}
        results.append(item)
        bounds = bridage.clock_window(clocks, row["epoch_ecu_s"], m)
        if row["statut_grid"] != 1 or not bounds or not row["reglage"]["actif_a_zero"]:
            continue
        lo, hi = bounds
        item["ecriture_pc_s"] = bounds
        for (link, uptime, value, raw, offset, tag), intervals in events.items():
            for a, b in intervals:
                if b >= lo - 3 and a <= hi:
                    item["envois_proches"].append({"connexion": link, "uptime_ms": uptime,
                        "w_esp": value, "w_shelly": raw, "offset_w": offset, "test_id": tag,
                        "envoi_pc_s": [a, b], "entier_identique": math.trunc(value) == cache["w"]})
    vs = [r["grid"]["w"] for r in rows if r["grid"]["w"] is not None]
    return {"signature": phase_counts(report, results), "points_numeriques": len(vs), "valeurs_distinctes_w": len(set(vs)),
            "ecritures_datees": len(results),
            "ecritures_avec_envoi_compatible": sum(any(e["entier_identique"] for e in it["envois_proches"])
                                                   for it in results),
            "details_ecritures": results, "ecritures_pv": bridage.pv_writes(rows, clocks)}


def phase_counts(report, writes):
    trial = report["essai_1000"]
    trigger = trial.get("demande_pc_s", math.inf)
    session = report.get("session")
    counts = {"avant": 0, "pendant": 0, "apres": 0}
    seen = set()
    stage = 0
    for row in writes:
        near = row["envois_proches"]
        compatible = [e for e in near if e["entier_identique"]]
        if not compatible:
            continue
        phases = set()
        for candidate in compatible:
            if candidate["test_id"] == session and math.isfinite(trigger):
                phases.add("pendant" if candidate["offset_w"] == 1000 else "apres")
            elif (candidate["offset_w"] == 0 and row["ecriture_pc_s"][1] < trigger
                  and candidate["envoi_pc_s"][1] < trigger):
                phases.add("avant")
            else:
                phases.add("inconnue")
        if len(phases) != 1 or "inconnue" in phases:
            continue
        phase = phases.pop()
        if phase == "pendant" and any(math.trunc(v["w_shelly"]) == row["w_ecu"] for v in near):
            continue
        order = bridage.PHASES.index(phase)
        if order < stage:
            continue
        # À 0 W, plusieurs trames identiques sont normales. Exiger la même phase
        # pour toutes et un envoi distinct disponible pour chaque nouvelle écriture.
        candidates = sorted(compatible, key=lambda e: e["envoi_pc_s"][0])
        keys = [(e["connexion"], e["uptime_ms"], e["test_id"]) for e in candidates]
        unused = next((key for key in keys if key not in seen), None)
        if unused is None:
            continue
        seen.add(unused)
        stage = order
        counts[phase] += 1
        row["phase_confirmee"] = phase
    return {"ecritures_par_phase": counts,
            "verdict": "signature_confirmee" if min(counts.values()) >= 2 else "indetermine"}


def set_offset(session, action):
    auth, _ = get_json(ESP, "/test-1000", limit=2048, timeout=5)
    csrf = auth.get("token")
    if not isinstance(csrf, str) or not re.fullmatch(r"[0-9A-F]{16}", csrf):
        raise ValueError("Jeton ESP32 illisible.")
    state = auth.get("essai", {})
    if action == "arreter":
        if state.get("id") != session or state.get("restant_ms") == 0:
            return {"deja_termine_ou_autre_session": True}
    elif action != "demarrer" or state.get("restant_ms") != 0:
        raise ValueError("ESP32 occupé ou commande inconnue.")
    form = urlencode({"action": action, "token": csrf, "session": session}).encode("ascii")
    ack, _ = get_json(ESP, "/test-1000", body=form,
                      content_type="application/x-www-form-urlencoded", limit=2048, timeout=5)
    remaining = ack.get("restant_ms")
    if (ack.get("id") != session or type(remaining) is not int
            or (action == "demarrer" and not 0 < remaining <= 90000)
            or (action == "arreter" and remaining != 0)):
        raise ValueError("Accusé ESP32 indéterminé. Aucun nouvel envoi.")
    return ack

def load_token(marker):
    token = marker.read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9a-f]{32}", token):
        raise ValueError("Session locale illisible. Aucun nouvel envoi dans ce dossier.")
    return token


def start(folder, report):
    marker = folder / "session_bridage.txt"
    if marker.exists():
        return load_token(marker), "reprise_lecture"
    preflight = esp_read()
    report["esp_avant"] = preflight
    if preflight["test_1000"]["restant_ms"]:
        raise ValueError("Un autre essai +1 000 W est en cours. Attendre le retour à la mesure réelle.")
    if not preflight["selection_esp_confirmee"]:
        raise ValueError("Le répondeur ESP32 doit être sélectionné pour le réseau et prêt sur sa page.")
    ecu_identity()
    status, page, _, _ = http(ECU, "/index.php/hidden/upload")
    if status != 200 or b"ENERGY COMMUNICATION UNIT" not in page.upper() or b"do_upload" not in page:
        raise ValueError("Page de récupération différente de celle du kit v3.3. Arrêt avant envoi.")
    if not remote_file_exists(GRID):
        raise ValueError("Fichier de mesure rapide absent. Arrêt avant envoi.")
    token = secrets.token_hex(16)
    package = reader_package((folder / "lecture.sh").read_bytes(), token)
    resource = "/resources/acc_bridage_" + token
    if remote_file_exists("/home/local_web/pages" + resource):
        raise ValueError("Emplacement de retour déjà utilisé. Arrêt avant envoi.")
    if remote_file_exists(STAGE):
        raise ValueError("Emplacement assist occupé. Arrêt avant envoi, laisser la maintenance se terminer.")
    # Cette marque est durable avant toute émission. Une reprise ne fait que lire.
    try:
        with marker.open("x", encoding="ascii") as f:
            f.write(token + "\n")
            f.flush()
            os.fsync(f.fileno())
    except FileExistsError:
        return load_token(marker), "reprise_lecture"
    report["session"] = token
    report["etat"] = "envoi_engage"
    save(folder / "releve_bridage_ECU.json", report)
    print("Envoi unique du collecteur temporaire à l'ECU.", flush=True)
    receipt = safe(upload_once, package)
    report["reponse_envoi"] = receipt
    if isinstance(receipt, str) and receipt.startswith("res=") and receipt not in ("res=0", "res=None"):
        raise ValueError("Le service de maintenance a refusé le paquet (" + receipt + ").")
    return token, "envoi_unique"


def maybe_start_offset(dest, report, token, esp, rows, elapsed):
    trial = report["essai_1000"]
    if report["mode"] != "envoi_unique" or not rows:
        return
    if "demande_pc_s" in trial:
        changed = (esp.get("selection_esp_confirmee") is False or
                   any(r["reglage"] != trial["reglage_depart"] for r in rows if r["n"] >= trial["premier_n"]))
        if ("arret_demande_pc_s" not in trial and esp.get("test_1000", {}).get("id") == token
                and esp["test_1000"]["restant_ms"] > 0 and changed):
            trial["arret_demande_pc_s"] = time.time()
            save(dest, report)
            trial["arret_accuse"] = safe(set_offset, token, "arreter")
            print("Réglage ou source du compteur modifié ou illisible. Arrêt demandé à l’ESP32.", flush=True)
        return
    if not 25 <= elapsed <= 115 or not esp.get("selection_esp_confirmee") or esp["test_1000"]["restant_ms"]:
        return
    meta = report["observations"][-1]["ecu_http"]
    server, pc = meta.get("date_http_s"), meta.get("pc_s")
    if (server is None or not pc or not -1 <= server - rows[-1]["epoch_ecu_s"] <= 6
            or not 0 <= time.time()-pc[1] <= 7):
        trial["attente"] = "Attente d’une trace ECU fraîche."
        return
    reason = bridage.eligible(report, summarize(report)["details_ecritures"], time.time())
    if reason:
        if trial.get("attente") != reason:
            print(reason, flush=True)
        trial["attente"] = reason
        return
    trial.pop("attente", None)
    trial["premier_n"] = rows[-1]["n"]
    trial["reglage_depart"] = rows[-1]["reglage"]
    # Sauvegarder l'intention avant le seul POST. Une reprise ne lance jamais de nouveau test.
    trial["demande_pc_s"] = time.time()
    save(dest, report)
    print("Début de l'essai +1 000 W pour 90 s. Retour automatique sur l'ESP32.", flush=True)
    trial["accuse"] = safe(set_offset, token, "demarrer")
    save(dest, report)
    if "erreur" in trial["accuse"]:
        print("Réponse de démarrage incertaine. Le relevé continue sans renvoyer de commande.", flush=True)


def monitor(folder, report, token):
    dest = folder / "releve_bridage_ECU.json"
    report["etat"] = "collecte"
    save(dest, report)
    started = time.monotonic()
    next_inverter = started
    inverter = None
    with ShellySampler(report["shelly_direct"]) as shelly, ThreadPoolExecutor(max_workers=3) as pool:
        while time.monotonic() - started < 405:
            turn = time.monotonic()
            get_trace = pool.submit(safe, trace_read, token)
            get_esp = pool.submit(safe, esp_read)
            if turn >= next_inverter and inverter is None:
                inverter = pool.submit(safe, inverter_read)
                next_inverter = turn + 25
            trace, esp = get_trace.result(), get_esp.result()
            shelly.drain()
            rows = trace.pop("lignes", [])
            if rows:
                old = report["points_ecu"]
                common = min(len(old), len(rows))
                if old[:common] != rows[:common]:
                    raise ValueError("La trace ECU a changé dans une partie déjà reçue.")
                if len(rows) > len(old):
                    report["points_ecu"] = rows
                row = rows[-1]
                current = row["grid"]
                history = esp.get("envois", [])
                age = current["age_s"]
                age_text = f"écriture il y a {age} s" if age is not None and age >= 0 else "date d'écriture inconnue"
                valid_text = {1: "compteur valide", 0: "compteur signalé indisponible"}.get(
                    row["statut_grid"], "statut compteur inconnu")
                sent_text = watts(history[-1]["power_w"]) if history else "indisponible"
                if history and history[-1].get("envoi_pc_s"):
                    a, b = history[-1]["envoi_pc_s"]
                    sent_text += f" (il y a environ {max(0, round(time.time() - (a + b) / 2))} s)"
                elapsed = duration(rows)
                pulse = esp.get("test_1000", {})
                if pulse.get("restant_ms", 0):
                    phase = f"+1 000 W ({(pulse['restant_ms'] + 999) // 1000} s)"
                elif pulse.get("id") == token:
                    phase = "après, mesure réelle"
                elif "demande_pc_s" in report["essai_1000"]:
                    phase = "vérification du retour"
                else:
                    phase = "avant, mesure réelle"
                recent = report["shelly_direct"][-1] if report["shelly_direct"] else {}
                raw = recent.get("w") if recent.get("pc_s") and 0 <= time.time()-recent["pc_s"][1] < 7 else None
                pv = row["meterPV"]
                pv_age = f"âge {pv['age_s']} s" if pv["age_s"] is not None else "âge inconnu"
                print(f"{elapsed:3d} s | {phase} | Shelly direct {watts(raw)} | ESP32 {sent_text}"
                      f" | ECU {watts(current['w'])} ({age_text}, {valid_text}) | PV {watts(pv['w'])} ({pv_age})",
                      flush=True)
            trace["dernier_n"] = len(rows)
            observation = {"ecu_http": trace, "esp": esp}
            if inverter is not None and (inverter.done() or trace.get("fin")):
                observation["onduleurs"] = inverter.result()
                inverter = None
            report["observations"].append(observation)
            save(dest, report)
            if trace.get("fin"):
                report["etat"] = "complet" if any(r["grid"]["w"] is not None for r in rows) else "complet_sans_watts"
                return True
            if rows:
                maybe_start_offset(dest, report, token, esp, rows, elapsed)
            if (trace.get("en_attente") or "erreur" in trace) and turn - started > 32 and not rows:
                # Si des échantillons ont été reçus, laisser le temps au LAN de revenir.
                if not report["points_ecu"] or report.get("mode") == "reprise_lecture":
                    raise ValueError("Résultat temporaire inaccessible ou expiré. Les lectures déjà reçues sont conservées.")
            time.sleep(max(0, 4 - (time.monotonic() - turn)))
    raise ValueError("Délai écoulé avant la fin du relevé de cinq minutes.")


def lock_local(file):
    # Verrou libéré par le système si le lecteur est interrompu.
    if os.name == "nt":
        import msvcrt
        file.seek(0, os.SEEK_END)
        if file.tell() == 0:
            file.write(b"0")
            file.flush()
        file.seek(0)
        msvcrt.locking(file.fileno(), msvcrt.LK_NBLCK, 1)
    else:
        import fcntl
        fcntl.flock(file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)


def run(folder):
    path = folder / "releve_bridage_ECU.json"
    marker = folder / "session_bridage.txt"
    report = {"format": "ACC-bridage-test1000-1", "etat": "preparation", "points_ecu": [], "observations": [],
              "essai_1000": {"duree_max_s": 90, "offset_w": 1000},
              "shelly_direct": []}
    if path.exists():
        saved = json.loads(path.read_text(encoding="utf-8"))
        if saved.get("format") != report["format"] or (not marker.exists() and saved.get("session")):
            raise ValueError("Un autre relevé existe ici. Extraire le nouveau kit dans un dossier séparé.")
        if marker.exists():
            if saved.get("session") != load_token(marker):
                raise ValueError("Rapport et session différents. Aucun envoi.")
            report = saved
    if report["etat"].startswith("complet"):
        print("Relevé déjà terminé. Le fichier à joindre est", path.name)
        return 0
    try:
        token, mode = start(folder, report)
        report.update(session=token, mode=mode)
        if mode == "reprise_lecture":
            print("Reprise de la même session, en lecture seule.", flush=True)
        monitor(folder, report, token)
    except KeyboardInterrupt:
        report["etat"] = "interrompu"
        trial = report["essai_1000"]
        if (report.get("mode") == "envoi_unique" and "demande_pc_s" in trial
                and "arret_demande_pc_s" not in trial and time.time()-trial["demande_pc_s"] < 100):
            trial["arret_demande_pc_s"] = time.time()
            save(path, report)
            trial["arret_accuse"] = safe(set_offset, report["session"], "arreter")
    except (OSError, ValueError, http_client.HTTPException) as exc:
        report["etat"] = "incomplet" if marker.exists() else "arrete_avant_envoi"
        report["dernier_incident"] = str(exc)
        print("Arrêt.", exc)
    report["bilan"] = summarize(report)
    report["regulation"] = bridage.evaluate(report, report["bilan"])
    report["cycles_micro_onduleurs"] = new_telemetry(report["observations"])
    save(path, report)
    n = report["bilan"]["points_numeriques"]
    sig = report["bilan"]["signature"]
    counts = sig["ecritures_par_phase"]
    print("Écritures concordantes : avant {avant}, pendant {pendant}, après {apres}.".format(**counts))
    if "demande_pc_s" not in report["essai_1000"]:
        print("Le +1 000 W n'a pas été lancé.", report["essai_1000"].get("attente", ""))
    else:
        print("Décalage et retour à la mesure réelle retrouvés dans l'ECU." if sig["verdict"] == "signature_confirmee" else
              "Décalage ou retour indéterminé dans les écritures de l'ECU.")
    result = report["regulation"]
    for phase, stage in result["phases"].items():
        real = watts(stage["shelly_reel"]["mediane_w"])
        grid = watts(stage["ecu_reseau"]["mediane_w"])
        pv = watts(stage["production_pv"]["mediane_w"])
        print(f"{phase} : médianes Shelly réel {real} | ECU {grid} | PV {pv}")
    if result["export_reel"] == "cycle_0_moins1000_0_observe":
        print("Injection réelle d’environ 1 kW puis retour près de zéro observés.")
    elif result["export_reel"] == "cycle_non_observe":
        print("Mesures disponibles : le cycle d’injection attendu n’a pas été observé dans la bande retenue.")
    else:
        print("Résultat de régulation indéterminé. Joindre le JSON, même avec une phase incomplète.")
    if result["reaction_pv_totale"] == "compatible_avec_le_bridage":
        print("Hausse puis baisse de la production PV compatibles avec la réaction attendue.")
    print(f"{n} lectures numériques du cache ECU. Fichier à joindre : {path.name}")
    return 0 if report["etat"] == "complet" else 1


if __name__ == "__main__":
    folder = Path(__file__).resolve().parent
    try:
        with (folder / "lecteur_bridage.lock").open("a+b") as fp:
            try:
                lock_local(fp)
            except OSError:
                print("Ce dossier a déjà un lecteur ouvert. Garder la première fenêtre.")
                sys.exit(1)
            sys.exit(run(folder))
    except (OSError, ValueError) as exc:
        print("Ouverture impossible.", exc)
        sys.exit(1)
