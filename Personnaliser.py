"""Prepare une copie locale des outils. Aucun acces reseau, aucun televersement."""
import argparse
import hashlib
import io
import ipaddress
import json
from pathlib import Path
import re
import secrets
import shutil
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent
FOLDERS = ('01_ESP32', '02_Test_signature', '03_Test_bridage', '04_Recuperation_ECU', '05_Analyse_REA')


def literal(value):
    return json.dumps(value, ensure_ascii=False)


def replace_one(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Les fichiers sources ont ete modifies : preparation annulee.')
    return text.replace(old, new)


def read_config(path):
    c = json.loads(path.read_text(encoding='utf-8-sig'))
    names = ('wifi_ssid', 'wifi_mot_de_passe', 'ip_ecu', 'uid_ecu', 'version_ecu',
             'ip_shelly', 'mac_shelly', 'canal_shelly', 'ip_esp32', 'id_compteur_esp32')
    if not isinstance(c, dict) or set(c) != set(names):
        raise ValueError('Conserver exactement les champs de configuration.exemple.json.')
    for key in names:
        if key != 'canal_shelly':
            if not isinstance(c[key], str) or any(ord(x) < 32 for x in c[key]):
                raise ValueError('Champ texte invalide : ' + key)
    for key in ('ip_ecu', 'ip_shelly', 'ip_esp32'):
        if key == 'ip_esp32' and not c[key]:
            continue
        try:
            address = ipaddress.IPv4Address(c[key])
        except ipaddress.AddressValueError:
            raise ValueError('Renseigner une adresse IPv4 valide : ' + key) from None
        if address.is_unspecified or address.is_multicast or address.is_loopback:
            raise ValueError('Adresse de cible invalide : ' + key)
    if not 1 <= len(c['wifi_ssid'].encode('utf-8')) <= 32:
        raise ValueError('Renseigner le SSID Wi-Fi (1 a 32 octets UTF-8).')
    password = c['wifi_mot_de_passe']
    size = len(password.encode('utf-8'))
    if not (8 <= size <= 63 or (size == 64 and re.fullmatch('[0-9a-fA-F]{64}', password))):
        raise ValueError('Le mot de passe Wi-Fi attendu fait 8 a 63 octets, ou 64 chiffres hexadecimaux.')
    if not re.fullmatch(r'[0-9]{12}', c['uid_ecu']) or c['uid_ecu'] == '0' * 12:
        raise ValueError('Renseigner les 12 chiffres de l’UID ECU.')
    if c['version_ecu'] != 'ECU-C-Z_C1.2.27A':
        raise ValueError('Ce profil est valide pour ECU-C-Z_C1.2.27A. Une autre version demande une analyse du protocole.')
    c['mac_shelly'] = c['mac_shelly'].replace(':', '').replace('-', '').upper()
    if not re.fullmatch('[0-9A-F]{12}', c['mac_shelly']) or c['mac_shelly'] == '0' * 12:
        raise ValueError('Renseigner sys.mac du Shelly.')
    if type(c['canal_shelly']) is not int or c['canal_shelly'] not in (0, 1):
        raise ValueError('Le canal du Pro EM 50 doit etre 0 ou 1.')
    meter = c['id_compteur_esp32']
    if meter and (not re.fullmatch(r'[A-Za-z0-9_.-]{1,30}', meter) or meter == 'LAB' + '0' * 12):
        raise ValueError('Copier exactement meter_id depuis /status, ou laisser vide pour le premier reperage.')
    return c


def write(path, text):
    text = text.replace('\r\n', '\n')
    if path.suffix in ('.ps1', '.cmd'):
        text = text.replace('\n', '\r\n')
    path.write_bytes(text.encode('utf-8-sig' if path.suffix == '.ps1' else 'utf-8'))


def configure(dest, c):
    channel = str(c['canal_shelly'])
    meter = c['id_compteur_esp32'] or 'LAB000000000000'
    sketch = dest/'01_ESP32/ESP32_v088/ESP32_v088.ino'
    s = sketch.read_text()
    s = s.replace('IP_ECU', c['ip_ecu']).replace('IP_SHELLY', c['ip_shelly'])
    for field, value in (('EXPECTED_ECU_UID', c['uid_ecu']), ('SHELLY_MAC', c['mac_shelly']), ('EXPECTED_METER_ID', meter)):
        previous = 'LAB000000000000' if field == 'EXPECTED_METER_ID' else '000000000000'
        s = replace_one(s, f'{field}[] = "{previous}";', f'{field}[] = {literal(value)};')
    s = replace_one(s, 'const IPAddress target(0,0,0,0);',
                    'const IPAddress target(' + c['ip_shelly'].replace('.', ',') + ');')
    s = replace_one(s, 'meterClient.remoteIP() != IPAddress(0,0,0,0)',
                    'meterClient.remoteIP() != IPAddress(' + c['ip_ecu'].replace('.', ',') + ')')
    s = replace_one(s, 'constexpr unsigned SHELLY_CHANNEL = 1;', 'constexpr unsigned SHELLY_CHANNEL = ' + channel + ';')
    s = s.replace('em1:1', 'em1:' + channel).replace('em1data:1', 'em1data:' + channel)
    s = s.replace('canal API <code>1</code>', 'canal API <code>' + channel + '</code>')
    write(sketch, s)
    write(sketch.parent/'WifiLocal.h', '#pragma once\n'
          + 'constexpr char WIFI_SSID[] = ' + literal(c['wifi_ssid']) + ';\n'
          + 'constexpr char WIFI_PASSWORD[] = ' + literal(c['wifi_mot_de_passe']) + ';\n'
          + 'static_assert(sizeof(WIFI_SSID) > 1, "Renseigner WifiLocal.h");\n')
    for folder in ('02_Test_signature', '03_Test_bridage'):
        d = dest/folder
        sh = (d/'lecture.sh').read_text()
        sh = replace_one(sh, '= 000000000000 ] || exit 12', '= ' + c['uid_ecu'] + ' ] || exit 12')
        write(d/'lecture.sh', sh)
        s = (d/'relever.py').read_text()
        for old, new in (('IP_ECU', c['ip_ecu']), ('IP_SHELLY', c['ip_shelly']),
                         ('IP_ESP', c['ip_esp32'] or 'IP_ESP'), ('LAB000000000000', meter)):
            s = s.replace(old, new)
        s = replace_one(s, 'UID = "000000000000"', 'UID = ' + literal(c['uid_ecu']))
        s = replace_one(s, '!= "000000000000" or channel.get("id") != 1',
                        '!= ' + literal(c['mac_shelly']) + ' or channel.get("id") != ' + channel)
        s = s.replace('"em1:1"', '"em1:' + channel + '"').replace('canal 1 indisponible', 'canal ' + channel + ' indisponible')
        digest = hashlib.sha256((d/'lecture.sh').read_bytes()).hexdigest()
        s, n = re.subn(r'TEMPLATE_HASH = "[0-9a-f]{64}"', f'TEMPLATE_HASH = "{digest}"', s)
        if n != 1:
            raise ValueError('Empreinte du collecteur introuvable.')
        write(d/'relever.py', s)
    d = dest/'04_Recuperation_ECU'
    token = secrets.token_hex(16)
    sh = (d/'sources/assist.sh').read_text().replace('00000000000000000000000000000000', token)
    write(d/'sources/assist.sh', sh)
    data = (d/'sources/assist.sh').read_bytes()
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode='w:bz2', format=tarfile.USTAR_FORMAT) as tar:
        item = tarfile.TarInfo('update_localweb/assist')
        item.mode, item.size, item.mtime = 0o755, len(data), 0
        tar.addfile(item, io.BytesIO(data))
    package = stream.getvalue()
    (d/'ECU_C_recuperation_v3_3.tar.bz2').write_bytes(package)
    digest = hashlib.sha256(package).hexdigest()
    s = (d/'Recuperer_ECU.ps1').read_text(encoding='utf-8-sig').replace('IP_ECU', c['ip_ecu'])
    s = s.replace('00000000000000000000000000000000', token)
    s, n = re.subn(r"\$ExpectedPackageHash = '[0-9a-f]{64}'", f"$ExpectedPackageHash = '{digest}'", s)
    if n != 1:
        raise ValueError('Empreinte du paquet de collecte introuvable.')
    write(d/'Recuperer_ECU.ps1', s)
    write(d/'PAQUET_SHA256.txt', digest + '  ECU_C_recuperation_v3_3.tar.bz2\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', type=Path, default=ROOT/'configuration.locale.json')
    parser.add_argument('--sortie', type=Path, default=ROOT/'installation_locale')
    args = parser.parse_args()
    c = read_config(args.configuration)
    output = args.sortie.expanduser().resolve()
    if output.exists():
        raise ValueError('Le dossier de sortie existe deja. Choisir --sortie installation_locale_2 pour conserver la copie precedente.')
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = Path(tempfile.mkdtemp(prefix='.preparation_acc_', dir=output.parent))
    try:
        for name in FOLDERS:
            shutil.copytree(ROOT/name, temp/name)
        configure(temp, c)
        ready = bool(c['ip_esp32'] and c['id_compteur_esp32'])
        write(temp/'LIRE_EN_PREMIER.txt',
              'COPIE LOCALE PERSONNELLE - ne pas publier ce dossier.\n'
              'Verifier CT_GRID_BACKUP et switchCtRoleMatches avant toute selection.\n'
              + ('Identites renseignees ; suivre les etapes et tests du guide.\n' if ready else
                 'REPERAGE INITIAL : relever IP ESP32 et meter_id, puis refaire une copie complete.\n'
                 'Ne pas lancer les deux lecteurs avant cette seconde preparation.\n'))
        temp.rename(output)
    except BaseException:
        shutil.rmtree(temp, ignore_errors=True)
        raise
    print('Copie locale preparee :', output)
    print('Aucun appareil contacte. Les valeurs Wi-Fi ne sont pas affichees.')
    if not ready:
        print('Reperage initial : relever IP ESP32 et meter_id, completer la configuration puis regenerer dans un nouveau dossier.')
    print('Avant selection : verifier les vrais reglages de retour aux tores, comme indique dans le guide.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, tarfile.TarError) as error:
        raise SystemExit('Arret : ' + str(error))
