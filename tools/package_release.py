import argparse
import hashlib
import json
import os
import re
import shutil
import tarfile
import tempfile
import zipfile
from pathlib import Path

from build_web import FRONTEND, NATIVE, PLATFORMS, SCOPED

ROOT = Path(__file__).resolve().parent.parent


def windows_runtime(build):
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith("CMAKE_CXX_COMPILER:"):
            compiler = line.partition("=")[2]
            if compiler and not compiler.endswith("-NOTFOUND"):
                return Path(compiler).parent
    raise RuntimeError(f"CMAKE_CXX_COMPILER is not configured in {build / 'CMakeCache.txt'}")


def validate_version(version):
    if not re.fullmatch(r"v[0-9A-Za-z][0-9A-Za-z._-]*", version) or version.endswith("."):
        raise ValueError(f"Invalid release tag for asset filenames: {version}")


def package(platform, build, output, version):
    validate_version(version)
    libraries = sorted((build / "core/libs/libs").glob("*.prx"))
    expected = {f"{directory.name}.prx" for directory in (ROOT / "core/libs/prx").iterdir() if directory.is_dir() and not directory.name.startswith(".")}
    missing = expected - {library.name for library in libraries}
    if missing:
        raise RuntimeError(f"Missing patched libraries: {', '.join(sorted(missing))}")
    executable = "relinker.exe" if platform == "windows" else "relinker"
    files = [library for library in libraries if library.name in expected]
    if platform == "windows":
        runtime = windows_runtime(build)
        files.extend(runtime / name for name in ("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll"))
    binary = build / "core/relinker" / executable
    for file in [*files, binary]:
        if not file.is_file() or file.stat().st_size == 0:
            raise RuntimeError(f"Missing or empty release file: {file}")
    asset = f"relinker-{version}.exe" if platform == "windows" else f"relinker-{version}"
    names = (f"prx-{platform}-{version}.zip", f"prx-{platform}-{version}.tar.gz", asset)
    for name in names:
        destination = output / name
        if destination.exists() and not destination.is_file():
            raise RuntimeError(f"Release destination is not a file: {destination}")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".release-", dir=output) as temporary:
        staging = Path(temporary)
        with zipfile.ZipFile(staging / f"prx-{platform}-{version}.zip", "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for file in files:
                archive.write(file, arcname=f"libs/{file.name}")
        with tarfile.open(staging / f"prx-{platform}-{version}.tar.gz", "w:gz", compresslevel=9, dereference=True) as archive:
            for file in files:
                archive.add(file, arcname=f"libs/{file.name}")
        shutil.copy2(binary, staging / asset)
        for file in sorted(staging.iterdir()):
            file.replace(output / file.name)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        while chunk := file.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def validate_web(web, version):
    validate_version(version)
    snapshot = version.startswith("vdev-")
    if snapshot and not re.fullmatch(r"vdev-[0-9a-f]{40}", version):
        raise ValueError(f"Invalid snapshot tag: {version}")
    if web.is_symlink() or not web.is_dir():
        raise RuntimeError(f"Web site is not a directory: {web}")
    for name in [*(f"native/{name}" for name in NATIVE), *FRONTEND, "release.json"]:
        file = web / name
        if file.is_symlink() or not file.is_file() or file.stat().st_size == 0:
            raise RuntimeError(f"Missing or empty web file: {name}")
    manifest = json.loads((web / "release.json").read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("tag") != version:
        raise RuntimeError(f"Web release manifest does not match {version}")
    commit = manifest.get("commit")
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit) or (snapshot and commit != version.removeprefix("vdev-")):
        raise RuntimeError(f"Web release manifest commit does not match {version}")
    if manifest.get("converter") != {"path": f"native/{commit}/{NATIVE[0]}"}:
        raise RuntimeError(f"Web release manifest converter is not scoped to {commit}")
    for name in SCOPED:
        scoped = web / "native" / commit / name
        built = web / "native" / name if name in NATIVE else web / name
        if scoped.is_symlink() or not scoped.is_file() or sha256(scoped) != sha256(built):
            raise RuntimeError(f"Commit-scoped asset does not match the built asset: {name}")
    index = (web / "index.html").read_text(encoding="utf-8")
    for attribute, name in (("src", "app.mjs"), ("href", "app.css")):
        if index.count(f'{attribute}="native/{commit}/{name}"') != 1:
            raise RuntimeError(f"Web page does not reference commit-scoped {name}")
    platforms = manifest.get("platforms")
    if not isinstance(platforms, dict) or set(platforms) != set(PLATFORMS):
        raise RuntimeError("Web release manifest platforms are incomplete")
    for platform in PLATFORMS:
        asset = platforms[platform]
        if not isinstance(asset, dict) or not isinstance(asset.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", asset["sha256"]):
            raise RuntimeError(f"Invalid {platform} library manifest entry")
        if asset.get("path") != f"libraries/{platform}-{asset['sha256']}.zip" or type(asset.get("expandedBytes")) is not int or asset["expandedBytes"] < 0:
            raise RuntimeError(f"Invalid {platform} library manifest entry")
        file = web / asset["path"]
        if file.is_symlink() or not file.is_file() or type(asset.get("size")) is not int or file.stat().st_size != asset["size"] or sha256(file) != asset["sha256"]:
            raise RuntimeError(f"{platform} library archive does not match the web release manifest")
    files = []
    for directory, directories, names in os.walk(web):
        directories.sort()
        for name in [*directories, *names]:
            path = Path(directory) / name
            if path.is_symlink():
                raise RuntimeError(f"Web site contains a symbolic link: {path.relative_to(web)}")
        files.extend(Path(directory) / name for name in sorted(names))
    for file in files:
        if not file.is_file():
            raise RuntimeError(f"Web site contains a special file: {file.relative_to(web)}")
    return files


def package_web(web, output, version):
    files = validate_web(web, version)
    destination = output / f"web-{version}.zip"
    if destination.exists() and not destination.is_file():
        raise RuntimeError(f"Release destination is not a file: {destination}")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".release-", dir=output) as temporary:
        staged = Path(temporary) / destination.name
        with zipfile.ZipFile(staged, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for file in files:
                archive.write(file, arcname=file.relative_to(web).as_posix())
        staged.replace(destination)


def collect_docs(source, output):
    documents = sorted(document for document in source.rglob("*.md") if document.is_file())
    if not documents:
        raise RuntimeError(f"No Markdown documents found in {source}")
    names = {file.name.casefold() for file in output.iterdir()} if output.exists() else set()
    for document in documents:
        if document.name.casefold() in names:
            raise RuntimeError(f"Duplicate release asset name: {document.name}")
        names.add(document.name.casefold())
    output.mkdir(parents=True, exist_ok=True)
    for document in documents:
        shutil.copy2(document, output / document.name)


def main():
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--platform", choices=("linux", "windows"))
    mode.add_argument("--docs", type=Path)
    mode.add_argument("--web", type=Path)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--version")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.docs is not None:
        if args.build is not None or args.version is not None:
            parser.error("--docs cannot be combined with --platform, --build or --version")
        collect_docs(args.docs, args.output)
    elif args.web is not None:
        if args.build is not None or args.version is None:
            parser.error("--web requires --version and cannot be combined with --platform, --build or --docs")
        package_web(args.web, args.output, args.version)
    else:
        if args.platform is None or args.build is None or args.version is None:
            parser.error("--platform, --build and --version are required when --docs or --web is not specified")
        package(args.platform, args.build, args.output, args.version)


if __name__ == "__main__":
    main()
