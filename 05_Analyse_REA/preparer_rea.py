"""Prepare a private WSL investigation from the verified archive and start local Codex/REA."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from datetime import datetime
from zipfile import ZipFile, ZIP_DEFLATED

ARCHIVE_SHA = "SHA256_DE_VOTRE_EXTRACTION_A_RENSEIGNER"
KIT = Path(__file__).resolve().parent
CORE = ("rapport.md", "observations_rea.json", "protocole_terrain.md")


def checked_extract(path, dest):
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
        if digest.hexdigest() != ARCHIVE_SHA:
            raise ValueError("Verifier ARCHIVE_SHA, le manifeste et les nombres de fichiers pour votre propre extraction. Voir LIRE_MOI.txt.")
        stream.seek(0)
        hashes = {}
        with tarfile.open(fileobj=stream, mode="r:gz") as archive:
            for member in archive:
                name = PurePosixPath(member.name)
                if (not member.isfile() or name.is_absolute() or ".." in name.parts
                        or not name.parts or str(name) in hashes):
                    raise ValueError("Entree d'archive inattendue")
                out = dest.joinpath(*name.parts)
                out.parent.mkdir(parents=True, exist_ok=True)
                data = archive.extractfile(member).read()
                with out.open("xb") as saved:
                    saved.write(data)
                out.chmod(0o400)
                hashes[str(name)] = hashlib.sha256(data).hexdigest()
    manifest = {}
    for line in (dest / "SHA256SUMS.txt").read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})\s+\*?(.+)", line)
        if not match or str(PurePosixPath(match[2])) in manifest:
            raise ValueError("Manifeste illisible ou en double")
        manifest[str(PurePosixPath(match[2]))] = match[1]
    versions = ("payload/etc/yuneng/version.conf", "runtime/version_after.conf")
    if (len(hashes) != 951 or len(manifest) != 936 or
            any(hashes.get(n) != h for n, h in manifest.items()) or
            any((dest / n).read_text().strip() != "C1.2.27A" for n in versions)):
        raise ValueError("La verification des fichiers ou de la version a echoue")
    return hashes


def wsl_path(arg):
    if re.match(r"^[A-Za-z]:[\\/]", arg) or arg.startswith("\\\\"):
        return Path(subprocess.check_output(["wslpath", "-u", arg], text=True).rstrip("\r\n"))
    return Path(arg).expanduser().resolve()


def node_env():
    env = os.environ.copy()
    nvm = Path.home() / ".nvm/versions/node"
    dirs = [nvm / "v26.13.0/bin"] + [p / "bin" for p in sorted(nvm.glob("v*"), reverse=True)]
    for directory in dirs:
        if all(os.access(directory / name, os.X_OK) for name in ("node", "rea", "codex")):
            env["PATH"] = str(directory) + ":" + env["PATH"]
            break
    if not all(shutil.which(name, path=env["PATH"]) for name in ("node", "rea", "codex")):
        raise ValueError("Codex et REA sont attendus dans Ubuntu. Aucune installation lancee.")
    return env


def export_result(work, kit, hashes, return_code):
    out = work / "sortie"
    if not out.is_dir() or out.is_symlink():
        raise ValueError("Dossier de sortie inattendu")
    observations = []
    path = out / CORE[1]
    if path.is_file() and not path.is_symlink() and path.stat().st_size <= 2_000_000:
        try:
            observations = json.loads(path.read_text())
        except (ValueError, OSError):
            pass
    valid_ids = set()
    if isinstance(observations, list):
        for row in observations:
            if not isinstance(row, dict):
                continue
            firm_path = row.get("chemin_elf")
            proof = row.get("preuve_rea")
            if isinstance(firm_path, str) and firm_path in hashes and row.get("sha256") == hashes[firm_path] and isinstance(proof, str) and re.fullmatch(r"ev_[a-fA-F0-9]{64}", proof):
                valid_ids.add(proof)
    use = []
    for file in sorted(out.rglob("*")):
        rel = file.relative_to(out)
        if file.is_symlink() or any(p.is_symlink() for p in file.parents if p != out and out in p.parents):
            continue
        if not file.is_file() or file.stat().st_size > 4_000_000 or len(rel.parts)>4:
            continue
        if rel.as_posix() in CORE or (rel.parts[0]=="preuves" and file.suffix.lower() in {".c",".asm",".txt",".md",".json",".csv"}):
            use.append((file,rel.as_posix()))
    names = {name for file,name in use if file.stat().st_size}
    ready = return_code == 0 and bool(valid_ids) and set(CORE) <= names and any(n.startswith("preuves/") for n in names)
    prefix = "retour_REA_v33_" if ready else "retour_REA_v33_partiel_"
    run_id = work.name[4:] if work.name.startswith("v33_") else work.name
    target = kit / (prefix + run_id + ".zip")
    status = {"archive_sha256":ARCHIVE_SHA, "code_sortie_codex":return_code,
              "observations_avec_empreinte_et_id":len(valid_ids), "controle_de_forme":ready,
              "dossier_ubuntu":str(work)}
    if sum(file.stat().st_size for file,name in use)>24_000_000:
        raise ValueError("Sortie trop volumineuse. Le resultat est conserve dans Ubuntu.")
    with ZipFile(target,"x",compression=ZIP_DEFLATED) as zipper:
        zipper.writestr("etat.json", json.dumps(status,ensure_ascii=False,indent=2))
        for file,name in use:
            zipper.write(file,name)
    return target, ready


def main():
    if len(sys.argv) != 2:
        raise ValueError("Selectionner l'archive v3.3.")
    archive = wsl_path(sys.argv[1])
    if not archive.is_file():
        raise ValueError("Archive introuvable. Selectionner le fichier .tar.gz.")
    env = node_env()
    base = Path.home() / "REA-work/ECU-C"
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="v33_"+datetime.now().strftime("%Y%m%d_%H%M%S")+"_",
                                 dir=base))
    print("Dossier de travail Ubuntu :",work,flush=True)
    print("Verification et ouverture de la copie du firmware...",flush=True)
    hashes = checked_extract(archive, work / "capture")
    shutil.copyfile(KIT / "mission_REA.txt", work / "mission_REA.txt")
    shutil.copytree(KIT / "reference", work / "reference")
    (work / "sortie/preuves").mkdir(parents=True)
    print("936 empreintes verifiees. Demarrage de Codex avec sa configuration REA habituelle.",flush=True)
    codex = shutil.which("codex", path=env["PATH"])
    with (work / "mission_REA.txt").open("rb") as prompt:
        run = subprocess.run([codex, "exec", "--skip-git-repo-check", "--sandbox", "workspace-write",
                              "-C", str(work), "-o", str(work/"fin_codex.txt"), "-"],
                             stdin=prompt, env=env, cwd=work)
    target, complete = export_result(work, KIT, hashes, run.returncode)
    print(("Retour a joindre : " if complete else "Retour PARTIEL a joindre : ")+target.name,flush=True)
    print("Le ZIP se trouve a cote de 1_Analyser_avec_REA.cmd.",flush=True)
    return 0 if complete else 2


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError, tarfile.TarError) as exc:
        sys.exit("Arret : " + str(exc))
