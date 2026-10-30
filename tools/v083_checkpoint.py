"""Local source checkpoints and guarded rollback; never touches the live server."""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import shutil
from datetime import datetime

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(r'D:\gmod昼夜求生资源\文档\制作计划\v0.8.3 优化计划\implementation_20261030')
TREES = ['addons/sourcemod/scripting', 'addons/sourcemod/configs', 'addons/sourcemod/plugins']

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def files():
    return {p.relative_to(ROOT).as_posix(): p for tree in TREES for p in (ROOT / tree).rglob('*') if p.is_file()}

def checkpoint(name):
    if not name or any(c not in '0123456789abcdefghijklmnopqrstuvwxyz_-' for c in name):
        raise ValueError('Use a unique lowercase checkpoint name.')
    target = WORK / 'checkpoints' / name
    target.mkdir(exist_ok=False)
    baseline = WORK / 'checkpoints/00_baseline/files'
    manifest = {}
    patches = []
    for relative, source in sorted(files().items()):
        dest = target / 'files' / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
        manifest[relative] = digest(dest)
        old = baseline / relative
        if relative.endswith(('.inc', '.sp')) and (not old.exists() or digest(old) != manifest[relative]):
            patches.extend(difflib.unified_diff(old.read_text(encoding='utf-8-sig').splitlines(True) if old.exists() else [], source.read_text(encoding='utf-8-sig').splitlines(True), fromfile='baseline/' + relative, tofile=name + '/' + relative))
    (target / 'manifest.json').write_text(json.dumps({'created': datetime.now().astimezone().isoformat(), 'root': str(ROOT), 'files': manifest, 'live_modified_by_this_tool': False}, ensure_ascii=False, indent=2), encoding='utf-8')
    (target / 'source.patch').write_text(''.join(patches), encoding='utf-8')
    print(json.dumps({'checkpoint': str(target), 'files': len(manifest)}, ensure_ascii=True))

def rollback(target_name, expected_name, apply):
    target = WORK / 'checkpoints' / target_name / 'files'
    expected = WORK / 'checkpoints' / expected_name / 'files'
    if not target.is_dir() or not expected.is_dir():
        raise ValueError('Both checkpoints must exist.')
    for folder in (target, expected):
        manifest = json.loads((folder.parent / 'manifest.json').read_text(encoding='utf-8'))['files']
        for relative, expected_hash in manifest.items():
            source = folder / relative
            if not source.is_file() or digest(source) != expected_hash:
                raise ValueError('Corrupt checkpoint; no files changed: ' + str(source))
    actions = []
    for relative in sorted(set(p.relative_to(target).as_posix() for p in target.rglob('*') if p.is_file()) | set(p.relative_to(expected).as_posix() for p in expected.rglob('*') if p.is_file())):
        if not any(relative.startswith(t + '/') for t in TREES):
            raise ValueError('Path outside the production trees: ' + relative)
        current, source, prior = ROOT / relative, target / relative, expected / relative
        current_hash = digest(current) if current.exists() else None
        wanted_hash = digest(source) if source.exists() else None
        if current_hash == wanted_hash:
            continue
        if current_hash != (digest(prior) if prior.exists() else None):
            raise ValueError('Refusing to overwrite later changes: ' + relative)
        actions.append((relative, current, source))
    # Preserve every file about to be overwritten, including newly added files.
    if apply:
        backup = WORK / 'rollback_preserved' / datetime.now().strftime('%Y%m%d_%H%M%S_%f')
        for relative, current, source in actions:
            if current.exists():
                dest = backup / relative
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(current, dest)
            if source.exists():
                current.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, current)
            else:
                current.unlink()
    print(json.dumps({'applied': apply, 'target': target_name, 'expected': expected_name, 'changed_paths': [a[0] for a in actions], 'live_modified': False}, ensure_ascii=True))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--save')
    parser.add_argument('--restore')
    parser.add_argument('--expected')
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    if args.save and not args.restore:
        checkpoint(args.save)
    elif args.restore and args.expected and not args.save:
        rollback(args.restore, args.expected, args.apply)
    else:
        parser.error('Use --save NAME, or --restore TARGET --expected CURRENT [--apply].')
