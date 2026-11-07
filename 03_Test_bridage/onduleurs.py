"""Parse the ECU-C page as data, without executing JavaScript or submitting its form."""
from datetime import datetime
from html.parser import HTMLParser
import re


class PowerTable(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.table = False
        self.body = False
        self.row = None
        self.cell = None
        self.rows = []
        self.found = False

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if tag == "table":
            classes = (attrs.get("class") or "").split()
            self.table = "table-condensed" in classes and "table-bordered" in classes
            self.found |= self.table
        elif self.table and tag == "tbody":
            self.body = True
        elif self.body and tag == "tr":
            self.row = []
        elif self.row is not None and tag == "td":
            self.cell = []

    def handle_data(self, data):
        if self.cell is not None:
            self.cell.append(data)

    def handle_endtag(self, tag):
        if tag == "td" and self.cell is not None:
            self.row.append(" ".join("".join(self.cell).split()))
            self.cell = None
        elif tag == "tr" and self.row is not None:
            self.rows.append(self.row)
            self.cell = self.row = None
        elif tag == "tbody":
            self.body = False
        elif tag == "table":
            self.table = self.body = False


def read_rows(body):
    table = PowerTable()
    table.feed(body.decode("utf-8", "replace"))
    table.close()
    if not table.found:
        raise ValueError("Table des onduleurs absente de la page locale")
    channels = []
    group_id = date = None
    for row in table.rows:
        if len(row) not in (4, 7):
            group_id = date = None
            continue
        device = re.fullmatch(r"([0-9A-Fa-f]{12})(?:-([A-Za-z0-9]{1,4}))?", row[0])
        if device is None:
            group_id = date = None
            continue
        identifier = device[1].upper()
        if len(row) == 7:
            group_id = identifier
            date = row[6] if row[6] not in ("", "-") else None
        elif group_id != identifier:
            date = None
            group_id = None
        power = re.fullmatch(r"([+-]?\d{1,6}(?:[.,]\d{1,3})?)\s*W", row[1], re.I)
        watts = float(power[1].replace(",", ".")) if power else None
        channels.append({"onduleur": identifier, "canal": device[2], "w": watts,
                         "date_cycle_ecu": date})
    return channels


def parse_stamp(raw):
    if not isinstance(raw, str):
        return None
    text = raw.strip()
    for pattern in ("%Y-%m-%d %H:%M:%S", "%Y/%m/%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S",
                    "%Y-%m-%d %H:%M", "%Y/%m/%d %H:%M", "%Y%m%d%H%M%S"):
        try:
            return datetime.strptime(text, pattern)
        except ValueError:
            pass
    return None


def new_telemetry(observations):
    """Report ECU cycle changes for each channel, with PC bounds on visibility."""
    previous = {}
    updates = []
    for obs in observations:
        page = obs.get("onduleurs") or {}
        window = page.get("pc_interval_s")
        if not window or len(window) != 2:
            continue
        for channel in page.get("canaux", []):
            key = channel["onduleur"], channel["canal"]
            stamp = parse_stamp(channel.get("date_cycle_ecu"))
            if stamp is None:
                previous.pop(key, None)
                continue
            prior = previous.get(key)
            if prior and stamp > prior[0]:
                updates.append({"onduleur": key[0], "canal": key[1],
                                "date_ecu_avant": prior[1], "date_ecu_apres": channel["date_cycle_ecu"],
                                "w_avant": prior[2], "w_apres": channel.get("w"),
                                "watts_changent": (channel["w"] != prior[2]) if channel.get("w") is not None and prior[2] is not None else None,
                                "intervalle_pc_observation_changement_s": [prior[3], window[1]]})
            previous[key] = stamp, channel["date_cycle_ecu"], channel.get("w"), window[0]
    return updates
