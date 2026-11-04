"""Resolve hash-verified versioned scene catalogs, retaining the legacy fallback."""
from pathlib import Path
import hashlib, json

def _inside(assets,relative):
    root=Path(assets).resolve();path=(root/relative).resolve()
    if not path.is_relative_to(root):raise ValueError('Scene bundle path escapes asset root: '+str(relative))
    return path

def _verified(assets,record):
    path=_inside(assets,record['path'])
    if hashlib.sha256(path.read_bytes()).hexdigest()!=record['sha256']:
        raise ValueError('Scene bundle hash mismatch: '+str(path))
    return path

def active_bundle(assets):
    pointer=Path(assets)/'scene-audit/revisions/ACTIVE.json'
    if not pointer.exists():return None
    record=json.loads(pointer.read_text(encoding='utf-8'))
    if record.get('schema')!=1:raise ValueError('Unsupported active scene pointer schema')
    manifest=_verified(assets,record['manifest'])
    bundle=json.loads(manifest.read_text(encoding='utf-8'))
    if bundle.get('schema')!=1:raise ValueError('Unsupported scene bundle schema')
    return bundle

def inventory_path(assets,override=None):
    if override is not None:return Path(override)
    bundle=active_bundle(assets)
    return _verified(assets,bundle['inventory']) if bundle else Path(assets)/'scene-audit/revisions/inventory.json'

def timing_path(assets,override=None):
    if override is not None:return Path(override)
    bundle=active_bundle(assets)
    return _verified(assets,bundle['timing']) if bundle else Path(assets)/'scene-audit/revisions/timing-catalog.json'

def catalog_pair(assets,inventory_override=None,timing_override=None):
    bundle=active_bundle(assets)
    inventory=Path(inventory_override) if inventory_override else _verified(assets,bundle['inventory']) if bundle else Path(assets)/'scene-audit/revisions/inventory.json'
    timing=Path(timing_override) if timing_override else _verified(assets,bundle['timing']) if bundle else Path(assets)/'scene-audit/revisions/timing-catalog.json'
    return inventory,timing

def decoder_settings(assets):
    bundle=active_bundle(assets)
    if not bundle:return dict(cli='tools/xuihelper/XUIHelper.CLI/bin/Release/net8.0/XUIHelper.CLI.dll',schema='tools/xuihelper/XUIHelper.Core/Assets/Extensions/V8',numeric_format='legacy_six_decimal')
    config=json.loads(_verified(assets,bundle['decoder']).read_text(encoding='utf-8'))
    for record in config['files']:_verified(assets,record)
    return config

def protect_active_output(assets,destination):
    bundle=active_bundle(assets)
    if not bundle:return
    root=_verified(assets,bundle['inventory']).parent.resolve()
    target=Path(destination).resolve()
    if target.is_relative_to(root):
        raise ValueError('Choose an output outside the active immutable scene release: '+str(target))
