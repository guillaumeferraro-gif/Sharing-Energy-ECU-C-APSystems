"""Critères documentés de l'essai de régulation zéro export, sans commande réseau."""
import json
import re
from statistics import median

PHASES = ("avant", "pendant", "apres")
BAND_W = 200
PV_DELTA_BAND_W = 350


def parse_config(raw):
    """Champs chaînes utilisés par meter_thread_getMeterInfoNew dans C1.2.27A."""
    try:
        obj = json.loads(raw)
    except (ValueError, UnicodeError):
        obj = None
    obj = obj if isinstance(obj, dict) else {}
    func = obj.get("FUNC")
    zero = obj.get("zeroExport")
    zero = zero if isinstance(zero, dict) else {}
    mode = zero.get("mode", "0")  # absence de mode : le firmware initialise ce champ à 0
    keys = {"0": "P1", "1": "P1", "2": "P2", "3": "P3", "4": "P4"}
    key = keys.get(mode) if isinstance(mode, str) else None
    target = zero.get(key)
    if isinstance(target, str) and re.fullmatch(r"[+-]?\d{1,7}(?:\.0+)?", target.strip()):
        limit = int(target.strip().split(".")[0])
    else:
        limit = None
    return {"FUNC": func if isinstance(func, str) and func in ("0", "1", "2", "3", "4") else None,
            "mode": mode if key and isinstance(obj.get("zeroExport"), dict) else None,
            "champ": key if isinstance(obj.get("zeroExport"), dict) else None,
            "limite_w": limit, "actif_a_zero": func == "1" and key is not None and limit == 0}


def clock_window(clocks, sample, stamp):
    if not clocks:
        return None
    server, bracket = min(clocks, key=lambda v: (abs(v[0]-sample), v[1][1]-v[1][0]))
    if abs(server - sample) > 15:
        return None
    # Date HTTP et mtime sont à la seconde entière. Garder l'incertitude des deux.
    return [round(bracket[0]-server+stamp-1, 3), round(bracket[1]-server+stamp+1, 3)]


def pv_writes(rows, clocks):
    seen = set()
    out = []
    for row in rows:
        pv = row["meterPV"]
        stamp = pv["mtime_s"]
        if (pv["status"] != "lu" or stamp in seen or pv["w"] < 0 or not 0 <= pv["age_s"] <= 5
                or not row["reglage"]["actif_a_zero"]):
            continue
        seen.add(stamp)
        bracket = clock_window(clocks, row["epoch_ecu_s"], stamp)
        if bracket:
            out.append({"n": row["n"], "mtime_ecu_s": stamp, "w": pv["w"], "pc_s": bracket})
    return out


def direct_points(samples):
    seen = set()
    result = []
    for sample in samples:
        ts, clock = sample.get("pc_s"), sample.get("shelly_epoch_s")
        if ("w" in sample and type(clock) is int and clock not in seen and ts
                and 0 <= ts[1] - ts[0] <= 6):
            seen.add(clock)
            result.append(sample)
    return result


def in_window(samples, limits):
    if not limits:
        return []
    return [s for s in samples if limits[0] <= s["pc_s"][0] <= s["pc_s"][1] <= limits[1]]


def stats(points, target=None):
    values = [p["w"] for p in points]
    span = max(0, points[-1]["pc_s"][0]-points[0]["pc_s"][1]) if points else 0
    return {"n": len(points), "duree_min_s": round(span, 1),
            "mediane_w": median(values) if values else None,
            "dans_la_bande": sum(abs(v-target) <= BAND_W for v in values) if target is not None else None}


def settled(stat, count, span):
    return (stat["n"] >= count and stat["duree_min_s"] >= span
            and stat["dans_la_bande"] * 3 >= stat["n"] * 2)


def eligible(report, writes, now):
    row = report["points_ecu"][-1]
    grid, pv = row["grid"], row["meterPV"]
    if not row["reglage"]["actif_a_zero"]:
        return "Bridage actif à 0 W non confirmé dans le réglage ECU."
    if (row["statut_grid"] != 1 or grid["status"] != "lu" or not 0 <= grid["age_s"] <= 6
            or abs(grid["w"]) > BAND_W):
        return "Attente d’une nouvelle mesure ECU proche de 0 W."
    if pv["status"] != "lu" or not 0 <= pv["age_s"] <= 10 or pv["w"] <= 50:
        return "Attente d’une mesure PV fraîche au-dessus de 50 W."
    tail = [p for p in report["points_ecu"] if row["epoch_ecu_s"]-22 <= p["epoch_ecu_s"]]
    if (row["epoch_ecu_s"] - tail[0]["epoch_ecu_s"] < 15
            or any(p["reglage"] != row["reglage"] for p in tail)):
        return "Attente d’un réglage de bridage stable."
    samples = in_window(direct_points(report["shelly_direct"]), [now-25, now])
    if (not settled(stats(samples, 0), 3, 6) or abs(samples[-1]["w"]) > BAND_W
            or now-samples[-1]["pc_s"][1] > 7):
        return "Attente de mesures directes du Shelly stabilisées près de 0 W."
    recent = [w for w in writes if w.get("phase_confirmee") == "avant" and abs(w["w_ecu"]) <= BAND_W
              and row["epoch_ecu_s"]-w["mtime_ecu_s"] <= 40]
    if len(recent) < 2:
        return "Attente de deux écritures ECU concordantes avant l'essai."
    return None


def phase_windows(report, writes):
    marker = report["essai_1000"].get("demande_pc_s")
    phases = {p: [w for w in writes if w.get("phase_confirmee") == p] for p in PHASES}
    windows = {p: None for p in PHASES}
    if marker is None:
        return windows
    observations = report["observations"]
    captures = [o["ecu_http"]["pc_s"] for o in observations if o["ecu_http"].get("dernier_n") and o["ecu_http"].get("pc_s")]
    if captures and phases["avant"]:
        windows["avant"] = [max(captures[0][0], marker-45), marker]
    session = report["session"]
    for phase in ("pendant", "apres"):
        group = phases[phase]
        if not group:
            continue
        first = group[0]["ecriture_pc_s"][1]
        end = group[-1]["ecriture_pc_s"][0]
        for o in observations:
            esp, meta = o.get("esp", {}), o["ecu_http"]
            pulse = esp.get("test_1000", {})
            if (meta.get("dernier_n") and esp.get("selection_esp_confirmee") and pulse.get("id") == session
                    and (pulse["restant_ms"] > 0) == (phase == "pendant")):
                end = max(end, esp["pc_s"][0])
        # Écarter la transition et garder la dernière partie du plateau connu.
        lo = max(first+8, end-50)
        if end > lo:
            windows[phase] = [lo, end]
    return windows


def evaluate(report, summary):
    writes = summary["details_ecritures"]
    windows = phase_windows(report, writes)
    direct = direct_points(report["shelly_direct"])
    pv = summary["ecritures_pv"]
    stages = {}
    for phase, window in windows.items():
        ecu = [{"pc_s": w["ecriture_pc_s"], "w": w["w_ecu"]} for w in writes if w.get("phase_confirmee") == phase]
        stages[phase] = {"fenetre_pc_s": window,
            "shelly_reel": stats(in_window(direct, window), -1000 if phase == "pendant" else 0),
            "ecu_reseau": stats(in_window(ecu, window), 0), "production_pv": stats(in_window(pv, window))}
    trial = report["essai_1000"]
    arm = trial.get("premier_n")
    stable_config = bool(arm) and all(p["reglage"] == trial.get("reglage_depart")
        for p in report["points_ecu"] if p["n"] >= arm)
    signature = summary["signature"]["verdict"] == "signature_confirmee"
    valid = stable_config and signature and report["etat"] == "complet" and "arret_demande_pc_s" not in trial
    coverage = valid and all(v["shelly_reel"]["n"] >= 3 and v["shelly_reel"]["duree_min_s"] >= 6
                            and v["ecu_reseau"]["n"] >= 2 for v in stages.values())
    observed = coverage and all(settled(v["shelly_reel"], 3, 6) and settled(v["ecu_reseau"], 2, 0)
                                for v in stages.values())
    delta_up = delta_down = None
    if all(v["production_pv"]["n"] >= 2 for v in stages.values()):
        p = [stages[phase]["production_pv"]["mediane_w"] for phase in PHASES]
        delta_up, delta_down = p[1]-p[0], p[1]-p[2]
    pv_ok = (observed and delta_up is not None and abs(delta_up-1000) <= PV_DELTA_BAND_W
             and abs(delta_down-1000) <= PV_DELTA_BAND_W)
    result = {"phases": stages, "tolerance_reseau_w": BAND_W, "tolerance_delta_pv_w": PV_DELTA_BAND_W,
              "reglage_continu_depuis_declenchement": stable_config,
              "export_reel": "cycle_0_moins1000_0_observe" if observed else
                        ("cycle_non_observe" if coverage else "indetermine"),
              "hausse_pv_w": delta_up, "baisse_pv_w": delta_down,
              "reaction_pv_totale": "compatible_avec_le_bridage" if pv_ok else "indeterminee",
              "delai_variation_visible_pv_s": None}
    if pv_ok:
        shifted = [w for w in writes if w.get("phase_confirmee") == "pendant"]
        origin = shifted[0]["ecriture_pc_s"]
        start_grid = shifted[0]["w_ecu"]
        base = stages["avant"]["production_pv"]["mediane_w"]
        end = windows["pendant"][1]
        # Encadrer la première hausse visible du compteur PV. Le cache n'horodate pas une consigne des MO.
        # Une première valeur déjà régulée, ou un trou important, laisse le délai indéterminé.
        if start_grid >= 500:
            ordered = [s for s in pv if origin[0]-8 <= s["pc_s"][1] and s["pc_s"][1] <= end]
            for low, high in zip(ordered, ordered[1:]):
                if high["w"] < base + 300:
                    continue
                lo, hi = low["pc_s"][0] - origin[1], high["pc_s"][1] - origin[0]
                if (abs(low["w"]-base) <= BAND_W and high["pc_s"][0] >= origin[1]
                        and 0 <= hi <= 15 and high["pc_s"][1]-low["pc_s"][0] <= 10):
                    result["delai_variation_visible_pv_s"] = [round(max(0, lo), 1), round(hi, 1)]
                break
    return result
