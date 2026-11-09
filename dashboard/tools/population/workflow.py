"""One local population command for supported original/prepared inputs."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime,timezone
import os
import zipfile

import build_era_library as library
import build_media_transport_metrics as transport
from build_av2_asset_index import rows_from_entries,encode as encode_asset_index
from avatar_editor.stage import prepared_asset_files,sha256
from input_paths import input_root
from .archives import ArchiveCache
from .storage import Plan

PROJECT=Path(__file__).resolve().parents[2]
# Exact supported executable/pack pair, already exercised by the host harness.
# A directory name alone is not enough to select a guest ABI or shader pack.
EDITOR_XEX='5593985c1e9b980ea3587a838583571d3acc2d7cd1f808ec22c856f9760337cb'
EDITOR_PACK='09303e5a1fbf6744c450a740468740e1e065adbc45659f42a606755db4982b02'


def original_member(cache,archive,member):
    cache.note(archive,member,'runtime-artwork')
    return dict(kind='original-input-member',archive=str(archive),
                archive_sha256=hashlib.sha256(cache.read(archive)).hexdigest(),
                member=member,conversion=None)


def add_shared(plan,shared,name,cache):
    if shared.is_file():
        data=cache.lookup(shared,normalize_slashes=True).get(name)
        if data is None:raise ValueError('Original shared member is missing: '+name)
        plan.add_bytes('private/xbox/shrdres/'+name,data,original_member(cache,shared,name))
    else:
        source=shared/name
        if not source.is_file():raise ValueError('Original shared artwork is missing: '+str(source))
        plan.add_file('private/xbox/shrdres/'+name,source,
                      dict(kind='prepared-input-file',source=str(source),conversion=None,originality='supplied provenance'))


def add_dashboard(plan,project,resources,shared,cache):
    """Read original members directly; no full extraction or XML intermediary."""
    if not resources.is_dir():raise ValueError('Dashboard resources not found: '+str(resources))
    paths=library.used_paths((project/'main.cpp').read_text(encoding='utf-8'))
    baseline={}
    nxe=resources/'NXE/Retail/9199/dash.xex'
    for relative in paths:
        module,name=relative.split('/',1)
        if module=='shared':
            add_shared(plan,shared,name,cache)
            continue
        archive=nxe/module
        data=cache.lookup(archive,normalize_slashes=True).get(name)
        if data is None:raise ValueError('Original 9199 member is missing: '+relative)
        baseline[relative]=data
        plan.add_bytes('private/xbox/unpacked/'+relative,data,original_member(cache,archive,name))
    # The original baseline excludes shared/, which has a separate loader root.
    # Compare against the in-memory baseline instead of writing and reading it back.
    files,summaries=library.collect(resources,project/'private/xbox/unpacked',paths,
                                   archives=cache,baseline_bytes=baseline)
    packages={}
    for build,directory in library.BUILDS.items():
        folder=resources/directory/'dash.xex'
        packages[build]={p.name:cache.lookup(p,normalize_slashes=True)
                         for p in sorted(folder.iterdir()) if p.is_file() and cache.read(p)[:4]==b'XUIZ'}
    # Match the existing extractor's stale-file boundary. A forgotten old
    # revision image must not survive silently and change runtime fallback.
    era_output=plan.root/'private/era-library'
    expected=set(files)|{'manifest.tsv'}
    stale=[p.relative_to(era_output).as_posix() for p in era_output.rglob('*')
           if p.is_file() and p.relative_to(era_output).as_posix() not in expected]
    if stale:raise ValueError('Unmapped existing era artwork (preserved): '+', '.join(sorted(stale)))
    rows=['build\tpath\tsha256']
    for relative,data in files.items():
        build_text,logical=relative.split('/',1);build=int(build_text);module,name=logical.split('/',1)
        original,identity=library.library_source(packages[build],module,name)
        if original!=data:raise ValueError('Asset mapping changed during population: '+relative)
        package,member=identity.split('/',1)
        archive=resources/library.BUILDS[build]/'dash.xex'/package
        origin=original_member(cache,archive,member)
        origin['runtime_alias']=logical
        plan.add_bytes('private/era-library/'+relative,data,origin)
        rows.append(build_text+'\t'+logical+'\t'+hashlib.sha256(data).hexdigest())
    plan.add_bytes('private/era-library/manifest.tsv',('\n'.join(rows)+'\n').encode(),
                   dict(kind='generated-index',source='original scene members'),generated=True)
    layouts,records=transport.collect(resources,cache)
    plan.add_bytes('media_transport_layout.hpp',transport.render(layouts),
                   dict(kind='generated-code',source='original per-build transport scenes'),generated=True)
    plan.add_bytes('private/q10/transport-metrics.json',(json.dumps(records,indent=2)+'\n').encode(),
                   dict(kind='generated-provenance',source='original per-build transport scenes'),generated=True)
    return dict(builds=list(transport.CASES),runtime_paths=len(paths),era_specific_images=len(files),
                summaries=summaries,unresolved_widths=sum(not c['width_verified'] for row in records for c in row.get('cells',[])))


def add_editor(plan,source):
    """Copy the prepared asset tree unchanged, without legacy run-1 config output."""
    source=source.resolve()
    assets=source/'assets'
    for name,expected in [('avatareditor.xex',EDITOR_XEX),('ae.pack',EDITOR_PACK)]:
        path=assets/name
        if not path.is_file() or sha256(path)!=expected:
            raise ValueError('Unsupported or missing 12581 input '+str(path)+'; exact original executable/shader pair required')
    directories=[]
    files=[path for path in prepared_asset_files(source,directories) if path.relative_to(assets).as_posix() not in ('.av2-index','.av2-index.tmp')]
    records=[('private/avatar-editor/assets/'+path.relative_to(assets).as_posix(),path,
              dict(kind='prepared-input-file',source=str(path),conversion=None,
                   originality='supplied provenance; not inferred from extension')) for path in files]
    plan.add_files(records,progress=lambda count:print('Checked '+str(count)+' editor input files',flush=True))
    count=len(files);total=sum(plan.entries[record[0]].size for record in records)
    # Reuse validated path/size records. Do not copy the tree and then rescan it
    # just to build metadata; preserve empty directories in the original index.
    index_entries=[(path.relative_to(assets).as_posix(),plan.entries[record[0]].size,1) for path,record in zip(files,records)]
    index_entries.extend((path.relative_to(assets).as_posix(),0,2) for path in directories)
    for path in directories:plan.add_directory('private/avatar-editor/assets/'+path.relative_to(assets).as_posix())
    index_rows=rows_from_entries(index_entries);index_data=encode_asset_index(index_rows)
    plan.add_bytes('private/avatar-editor/assets/.av2-index',index_data,
                   dict(kind='derived-filesystem-index',format='AV2IDX01',entries=len(index_rows)),generated=True)
    # Preserve any supplied provenance without treating previously converted
    # fonts/containers as untouched original Microsoft bytes.
    for name in ['asset-provenance.json','active-font-provenance.json','font-provenance.json',
                 'release-provenance.json','hair-integration-report.json','closet-import-report.json']:
        path=source/name
        if path.is_file():plan.add_file('private/avatar-editor/provenance/'+name,path,
                                       dict(kind='supplied-provenance',source=str(path)))
    return dict(build=12581,files=count,bytes=total,exact_xex_sha256=EDITOR_XEX,exact_pack_sha256=EDITOR_PACK,
                derived_index_entries=len(index_rows),derived_index_sha256=hashlib.sha256(index_data).hexdigest(),
                legacy_console_configuration_written=False,guest_code_generated=False)


def build_plan(project,output,components,resources=None,shared=None,editor=None,packs=None,source_audit=False,intake=None):
    plan=Plan(output);cache=ArchiveCache();summary={}
    for source in [resources,shared,editor]:
        if source is not None:plan.protect(source)
    if 'dashboard' in components:summary['dashboard']=add_dashboard(plan,project,resources,shared,cache)
    if 'editor' in components:summary['editor']=add_editor(plan,editor)
    if 'packs' in components:
        from .prepared_packs import add_packs
        summary['packs']=add_packs(plan,packs)
    if source_audit:
        from audit_source_export import audit
        report=audit(project)
        plan.add_bytes('private/av2/population/source-export-audit.json',
                       (json.dumps(report,indent=2)+'\n').encode(),
                       dict(kind='generated-export-review',published=False),generated=True)
        summary['source_audit']=dict(source_candidates=len(report['source_candidates']),
            review_required=len(report['review_required']),published=False)
    receipt=dict(schema=1,scope='prepared-input population',components=list(components),
                 outputs=plan.manifest(),directories=sorted(plan.directories.values()),archives=cache.evidence(),summary=summary)
    if intake:receipt['recovery']=intake
    data=(json.dumps(receipt,indent=2,sort_keys=True)+'\n').encode()
    receipt_hash=hashlib.sha256(data).hexdigest()
    receipt_path='private/av2/population/receipts/'+receipt_hash+'.json'
    plan.add_bytes(receipt_path,data,dict(kind='generated-receipt'),generated=True)
    current=(json.dumps(dict(schema=1,status='prepared-verified',receipt=receipt_path,sha256=receipt_hash),indent=2)+'\n').encode()
    plan.add_bytes('private/av2/population/current.json',current,dict(kind='generated-receipt-pointer'),generated=True)
    return plan,cache,summary,receipt_path


def run_build(project,kind,log_dir):
    """Build locally using argument arrays. No install, console or network step."""
    log_dir=log_dir/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(os.getpid()))
    log_dir.mkdir(parents=True,exist_ok=False)
    commands=[['make','build/test_dash','build/test_library'],['./build/test_dash'],['./build/test_library']]
    commands.append(['make','build/exbox-host' if kind=='host' else 'build/eboot.elf'])
    for index,command in enumerate(commands):
        with (log_dir/('build-'+str(index)+'.log')).open('w') as log:
            result=subprocess.run(command,cwd=project,stdout=log,stderr=subprocess.STDOUT)
        if result.returncode:raise ValueError('Build/check failed: '+repr(command)+'; see '+str(log_dir))
    return dict(kind=kind,status='pass',commands=commands,logs=str(log_dir))


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs',type=Path,help='Drop-folder root (default: inputs/ or EXBOX_INPUTS)')
    parser.add_argument('--resources',type=Path,help='Override decrypted dashboard resource tree')
    parser.add_argument('--shared',type=Path,help='Original XUIZ shrdres.xzp or prepared flat shared artwork folder')
    parser.add_argument('--editor',type=Path,help='Override prepared editor folder containing assets/')
    parser.add_argument('--module-tree',type=Path,help='Original dashboard archive source tree; requires local XEX tool source')
    parser.add_argument('--guide-source',type=Path,help='Original Guide modules, organized by era/revision/guide-core')
    parser.add_argument('--updates',type=Path,help='Original system-update ZIP or folder of ZIPs; deltas retained pending exact bases')
    parser.add_argument('--xex-source',type=Path,help='Local MIT XEX recovery C++ source, compiled and self-tested locally')
    parser.add_argument('--packs',type=Path,help='Previously recovered data packs; copied unchanged with provenance')
    parser.add_argument('--editor-source',type=Path,help='Editor title source tree after guest codegen; isolated native rebuild')
    parser.add_argument('--audit-source',action='store_true',help='Record a local source-only export audit; does not publish')
    parser.add_argument('--output',type=Path,default=PROJECT,help='Output checkout/scratch root')
    parser.add_argument('--components',nargs='+',choices=['dashboard','editor','packs','updates','guide'],help='Default: components found in inputs')
    parser.add_argument('--check',action='store_true',help='Check prepared outputs or inspect raw input locations; write nothing; raw extraction remains pending')
    parser.add_argument('--build',choices=['none','host','payload','editor-native'],default='none',help='Optional local build; native editor requires local source and verified SDK/runtime dependencies')
    parser.add_argument('--intake',nargs='+',type=Path,help='Identify and copy dropped files through the shared intake engine')
    parser.add_argument('--intake-status',action='store_true',help='Show missing input features without copying or building')
    args=parser.parse_args(argv)
    root=args.inputs.resolve() if args.inputs else input_root(PROJECT)
    if args.intake or args.intake_status:
        from .intake import intake,feature_status
        try:
            result=intake(args.intake,root,args.check) if args.intake else dict(missing=feature_status(root))
            print(json.dumps(result,indent=2));return 0
        except (ValueError,OSError,zipfile.BadZipFile) as error:
            print('Intake failed: '+str(error),file=sys.stderr);return 1
    resources=args.resources.resolve() if args.resources else root/'dashboard-resources'
    shared=args.shared.resolve() if args.shared else next((p for p in [root/'dashboard-shared',root/'shrdres.xzp'] if p.exists()),root/'dashboard-shared')
    editor=args.editor.resolve() if args.editor else root/'avatar-editor'
    modules=args.module_tree.resolve() if args.module_tree else root/'dashboard-source'
    guides=args.guide_source.resolve() if args.guide_source else root/'guide-source'
    updates=args.updates.resolve() if args.updates else root/'updates'
    tool_source=args.xex_source.resolve() if args.xex_source else root/'tool-sources/xextool-re'
    packs=args.packs.resolve() if args.packs else root/'prepared-packs'
    components=args.components if args.components is not None else [name for name,present in
        [('dashboard',resources.is_dir() or modules.is_dir()),('editor',editor.is_dir()),
         ('packs',packs.is_dir()),('updates',updates.exists()),('guide',guides.is_dir())] if present]
    if not components and not args.audit_source and args.build!='editor-native':
        parser.error('No supported inputs found. See docs/PORTABLE-INPUTS.md; videos use import_trailers.py separately.')
    if args.build in ('host','payload') and ('dashboard' not in components or args.output.resolve()!=PROJECT.resolve()):
        parser.error('--build host/payload requires dashboard inputs and current checkout output')
    if args.build=='editor-native' and args.output.resolve()!=PROJECT.resolve():
        parser.error('--build editor-native requires current checkout output')
    try:
        from .bootstrap import recover_inputs,native_editor
        intake=recover_inputs(PROJECT,args.output,components,modules,guides,updates,tool_source,
                              resources,args.check)
        if intake.get('resource_root'):resources=Path(intake['resource_root'])
        prepared=[c for c in components if c in ('dashboard','editor','packs')]
        if args.check and intake.get('pending_dashboard'):prepared.remove('dashboard')
        plan,cache,summary,receipt=build_plan(PROJECT,args.output,prepared,resources,shared,editor,packs,args.audit_source,intake)
        if args.check:plan.preflight()
        result=dict(status=('inputs-inspected; raw recovery pending' if intake.get('pending_dashboard') or (args.check and intake.get('raw')) else 'checked') if args.check else 'populated',components=components,intake=intake,
                    files=len(plan.entries),archives=cache.statistics(),summary=summary,receipt=receipt)
        result['unresolved_inputs']=any('unresolved' in row.get('status','') for row in [intake.get('dashboard',{}),intake.get('guide',{}),*intake.get('updates',[])])
        if result['unresolved_inputs'] and not args.check:result['status']='populated; recovery has unresolved inputs'
        if not args.check:
            result['writes']=plan.apply()
            if args.build=='editor-native':result['build']=native_editor(PROJECT,args.editor_source or root/'editor-source')
            elif args.build!='none':result['build']=run_build(PROJECT,args.build,PROJECT/'private/av2/population/build-logs')
        print(json.dumps(result,indent=2))
        return 0
    except (OSError,ValueError,KeyError,AssertionError,subprocess.CalledProcessError,zipfile.BadZipFile) as error:
        print('Setup failed: '+str(error),file=sys.stderr)
        return 1


if __name__=='__main__':raise SystemExit(main())
