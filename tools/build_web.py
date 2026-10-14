import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PLATFORMS = ("windows", "linux")
NATIVE = ("relinker_wasm.mjs", "relinker_wasm.wasm")
FRONTEND = ("index.html", "app.css", "app.mjs", "archive.mjs", "worker.mjs")
SCOPED = (*NATIVE, "app.css", "app.mjs", "archive.mjs", "worker.mjs")
MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_ARCHIVE_ENTRIES = 4096


def copy_sources(output):
    source = ROOT / "demo/web"
    for file in source.iterdir():
        if file.is_file():
            shutil.copy2(file, output / file.name)
    (output / ".nojekyll").touch()


def snapshot_identity(version, commit, repository):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError(f"Invalid commit SHA: {commit}")
    if version != f"vdev-{commit}":
        raise ValueError(f"Snapshot version {version} does not match commit {commit}")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9._-]+", repository) or repository.partition("/")[2] in (".", ".."):
        raise ValueError(f"Invalid repository: {repository}")
    return f"https://github.com/{repository}/releases/tag/{version}"


def stage_archive(source, destination):
    if source.is_symlink() or not source.is_file():
        raise ValueError(f"Missing library archive: {source}")
    digest = hashlib.sha256()
    size = 0
    with source.open("rb") as reader, destination.open("wb") as writer:
        while chunk := reader.read(1024 * 1024):
            size += len(chunk)
            if size > MAX_ARCHIVE_BYTES:
                raise ValueError(f"Library archive exceeds {MAX_ARCHIVE_BYTES} bytes: {source.name}")
            digest.update(chunk)
            writer.write(chunk)
    try:
        with zipfile.ZipFile(destination) as archive:
            files = archive.infolist()
            if not files or len(files) > MAX_ARCHIVE_ENTRIES:
                raise ValueError(f"Invalid library archive: {source.name}")
            for file in files:
                if not file.filename.startswith("libs/") or "\\" in file.filename or any(part in ("..", ".", "") for part in file.filename.rstrip("/").split("/")):
                    raise ValueError(f"Invalid library archive layout: {source.name}")
            if not any(file.filename.endswith(".prx") and not file.is_dir() for file in files):
                raise ValueError(f"Library archive has no PRX modules: {source.name}")
            corrupt = archive.testzip()
            if corrupt:
                raise ValueError(f"Corrupt library archive entry {corrupt}: {source.name}")
            expanded = sum(file.file_size for file in files)
    except zipfile.BadZipFile as error:
        raise ValueError(f"Invalid library archive {source.name}: {error}") from error
    return {"size": size, "expandedBytes": expanded, "sha256": digest.hexdigest()}


def publish_snapshot(output, archives, version, commit, repository):
    url = snapshot_identity(version, commit, repository)
    for name in [*FRONTEND, *(f"native/{name}" for name in NATIVE)]:
        file = output / name
        if file.is_symlink() or not file.is_file():
            raise ValueError(f"Built web site is incomplete: missing {name}")
    index = (output / "index.html").read_text(encoding="utf-8")
    for attribute, name in (("src", "app.mjs"), ("href", "app.css")):
        index, count = re.subn(rf'{attribute}="(?:native/[0-9a-f]{{40}}/)?{re.escape(name)}"', f'{attribute}="native/{commit}/{name}"', index)
        if count != 1:
            raise ValueError(f"Built web page must reference exactly one {name}")
    native = output / "native"
    converter = native / commit
    manifest = {"tag": version, "commit": commit, "url": url, "converter": {"path": f"native/{commit}/{NATIVE[0]}"}, "platforms": {}}
    libraries = output / "libraries"
    for directory in (libraries, native, converter):
        if directory.is_symlink() or (directory.exists() and not directory.is_dir()):
            raise ValueError(f"Web destination is not a directory: {directory}")
    with tempfile.TemporaryDirectory(prefix=".libraries-", dir=output) as temporary:
        staging = Path(temporary)
        for platform in PLATFORMS:
            staged = staging / f"{platform}.zip"
            asset = stage_archive(archives / f"prx-{platform}-{version}.zip", staged)
            path = f"libraries/{platform}-{asset['sha256']}.zip"
            staged.rename(staging / Path(path).name)
            manifest["platforms"][platform] = {"path": path, **asset}
        (staging / "converter").mkdir()
        for name in SCOPED:
            source = native / name if name in NATIVE else output / name
            shutil.copy2(source, staging / "converter" / name)
        (staging / "release.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        (staging / "index.html").write_text(index, encoding="utf-8")
        libraries.mkdir(exist_ok=True)
        for platform in PLATFORMS:
            name = Path(manifest["platforms"][platform]["path"]).name
            (staging / name).replace(libraries / name)
        if converter.exists():
            shutil.rmtree(converter)
        (staging / "converter").rename(converter)
        (staging / "index.html").replace(output / "index.html")
        (staging / "release.json").replace(output / "release.json")
    current = {Path(entry["path"]).name for entry in manifest["platforms"].values()}
    for file in libraries.iterdir():
        if file.name not in current and (file.is_file() or file.is_symlink()):
            file.unlink()
    for directory in native.iterdir():
        if directory.name != commit and re.fullmatch(r"[0-9a-f]{40}", directory.name) and directory.is_dir() and not directory.is_symlink():
            shutil.rmtree(directory)
    (output / ".nojekyll").touch()
    print(f"Published {version} library assets for {commit}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--library-archives", type=Path)
    parser.add_argument("--version")
    parser.add_argument("--commit")
    parser.add_argument("--repository")
    args = parser.parse_args()
    snapshot = (args.library_archives, args.version, args.commit, args.repository)
    if any(value is not None for value in snapshot) and any(value is None for value in snapshot):
        parser.error("--library-archives, --version, --commit and --repository must be specified together")
    output = args.build.resolve() / "web"
    for name in NATIVE:
        if not (output / "native" / name).is_file():
            raise ValueError(f"Build relinker_wasm first: missing {name}")
    if args.library_archives is None:
        copy_sources(output)
    else:
        publish_snapshot(output, args.library_archives, args.version, args.commit, args.repository)
    print(output)


if __name__ == "__main__":
    main()
