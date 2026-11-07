import io
import json
import os
import re
import sys
import threading
import urllib.request
import zipfile

_lock = threading.RLock()
_target = None
_failed = {}

_NAME_RE = re.compile(r"^\s*([A-Za-z0-9][A-Za-z0-9._-]*)")


def configure(target: str):
    global _target
    _target = target
    os.makedirs(target, exist_ok=True)
    if target not in sys.path:
        sys.path.append(target)


def _normalize(name: str) -> str:
    return re.sub(r"[-_.]+", "-", name).lower()


def _installed(name: str) -> bool:
    try:
        from importlib import metadata
        metadata.distribution(name)
        return True
    except Exception:
        pass
    norm = _normalize(name).replace("-", "_")
    if _target and os.path.isdir(_target):
        for entry in os.listdir(_target):
            if entry.lower().endswith(".dist-info"):
                if _normalize(entry.rsplit("-", 1)[0]).replace("-", "_") == norm:
                    return True
    return False


def _wheel_score(filename: str) -> int:
    name = filename.lower()
    if not name.endswith(".whl"):
        return -1
    tags = name[:-4].split("-")[-3:]
    python, abi, platform = tags if len(tags) == 3 else ("", "", "")
    if platform == "any" and abi == "none":
        return 3 if "py3" in python else 1
    compatible = "cp311" in python or abi == "abi3"
    if not compatible:
        return -1
    import platform as _platform
    machine = _platform.machine().lower()
    if machine in ("amd64", "x64"):
        machine = "x86_64"
    if machine == "aarch64":
        machine = "arm64"
    if sys.platform == "win32" and "win_amd64" in platform:
        return 4
    if sys.platform.startswith("linux") and "manylinux" in platform \
            and (machine in platform or (machine == "arm64" and "aarch64" in platform)):
        return 4
    if sys.platform == "darwin" and "macosx" in platform \
            and (machine in platform or "universal2" in platform):
        return 4
    return -1


def _marker_ok(marker: str) -> bool:
    if not marker:
        return True
    if "extra" in marker:
        return False
    try:
        from packaging.markers import Marker
        return Marker(marker).evaluate()
    except Exception:
        return True


def _install_one(spec: str, depth: int = 0):
    match = _NAME_RE.match(spec or "")
    if not match:
        return
    name = match.group(1)
    if _installed(name) or depth > 6:
        return
    if name in _failed:
        raise RuntimeError(_failed[name])
    url = f"https://pypi.org/pypi/{name}/json"
    request = urllib.request.Request(url, headers={"User-Agent": "exDesktop"})
    with urllib.request.urlopen(request, timeout=20) as response:
        info = json.load(response)
    files = sorted(
        info.get("urls") or [],
        key=lambda f: _wheel_score(f.get("filename", "")),
        reverse=True)
    files = [f for f in files if _wheel_score(f.get("filename", "")) >= 0]
    if not files:
        _failed[name] = f"no compatible wheel for {name}"
        raise RuntimeError(_failed[name])
    wheel = files[0]
    request = urllib.request.Request(
        wheel["url"], headers={"User-Agent": "exDesktop"})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for member in archive.namelist():
            path = os.path.normpath(os.path.join(_target, member))
            if not path.startswith(os.path.normpath(_target)):
                continue
            archive.extract(member, _target)
    for requirement in info.get("info", {}).get("requires_dist") or []:
        spec_part, _, marker = requirement.partition(";")
        if _marker_ok(marker.strip()):
            _install_one(spec_part, depth + 1)


def missing(requirements) -> list:
    if not _target or not requirements:
        return []
    result = []
    for spec in requirements:
        match = _NAME_RE.match(str(spec or ""))
        if match and not _installed(match.group(1)) and match.group(1) not in _failed:
            result.append(str(spec))
    return result


def ensure(requirements) -> list:
    if not _target or not requirements:
        return []
    errors = []
    with _lock:
        for spec in requirements:
            try:
                _install_one(str(spec))
            except Exception as e:
                errors.append(f"{spec}: {e}")
    import importlib
    importlib.invalidate_caches()
    return errors
