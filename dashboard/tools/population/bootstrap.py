"""Optional local intake/build stages for setup.

Raw originals live only under private/. Checks inspect inputs without compiling
or extracting; they deliberately do not claim recovered-output verification.
Native title rebuilds reuse explicitly fingerprinted SDK/runtime dependencies.
"""
from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,os,zipfile


def recover_inputs(project,output,components,modules,guides,updates,tool_source,resources,check):
    needs_modules='dashboard' in components and not resources.is_dir()
    raw=needs_modules or 'guide' in components or 'updates' in components
    if not raw:return {}
    if Path(output).resolve()!=Path(project).resolve():
        raise ValueError('Raw recovery requires current checkout output; originals stay private')
    if not tool_source.is_dir():raise ValueError('Local XEX tool source is required: '+str(tool_source))
    paths=[]
    if 'updates' in components:
        paths=[updates] if updates.is_file() else sorted(updates.glob('*.zip'))
        if not paths:raise ValueError('No original update ZIPs found')
        if any(not zipfile.is_zipfile(path) for path in paths):raise ValueError('Invalid update ZIP input')
    for selected,path in [(needs_modules,modules),('guide' in components,guides)]:
        if selected and not path.is_dir():raise ValueError('Original module folder missing: '+str(path))
    result=dict(raw=True,check_only=check,archives=[str(path) for path in paths],
                originals_modified=False,delta_bases_assumed=False)
    if check:
        # No output/cache/tool creation here. Full CRC/decryption checks occur
        # in the population stage and are reported independently from this probe.
        result.update(status='inputs located; extraction/tool compilation not run',pending_dashboard=needs_modules)
        return result
    base=project/'private/av2/population/recovered'
    if needs_modules or 'guide' in components:
        from .module_tree import recover_tree
        for selected,source,kind in [(needs_modules,modules,'dashboard'),('guide' in components,guides,'guide')]:
            if not selected:continue
            # Source-path identity separates user trees; content fingerprints in
            # recovery reports detect changes and protect already derived bytes.
            identity=hashlib.sha256(str(source.resolve()).encode()).hexdigest()[:16]
            report=recover_tree(source,base/(kind+'-'+identity),tool_source,kind=kind)
            result[kind]=dict(status=report['status'],source_files=report['source_files'],
                resource_bindings=report['resolved_resource_bindings'],unresolved=len(report['unresolved']),
                report=str(base/(kind+'-'+identity)/'module-recovery.json'))
            if kind=='dashboard':result['resource_root']=report['resource_root']
    if paths:
        from .raw_updates import ingest
        reports=ingest(paths,base/'updates',tool_source)
        result['updates']=[dict(archive=r['source_archive'],status=r['status'],
            resources=r['resource_count'],failures=len(r['failures']),revision_mapped=r['revision_mapped'],
            report=str(base/'updates'/r['archive_sha256']/'recovery-report.json')) for r in reports]
    return result


def native_editor(project,source):
    """Rebuild title and package separately; never replace the active title.

    This is intentionally not named a full clean SDK bootstrap: the checked
    runtime/guest/dependency archives are required local prerequisites today.
    """
    from prepare_av2_title_port import prepare as prepare_host
    from prepare_av2_native_title import prepare as prepare_native
    from native_editor.build_title import build
    from native_editor.candidate import capture_dependencies,build_candidate
    source=Path(source).resolve(strict=True)
    state=project/'private/av2/population/native-builds'/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(os.getpid()))
    state.mkdir(parents=True,exist_ok=False)
    record=dict(schema=1,status='preparing',source=str(source),installed=False,
                full_sdk_bootstrap=False,limitation='existing fingerprinted runtime/SDK/dependency archives required')
    def checkpoint():
        (state/'bootstrap.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    checkpoint()
    try:
        record['host']=prepare_host(source,state/'title-port',skip_support=True);checkpoint()
        record['native']=prepare_native(state/'title-port',state/'title-native');checkpoint()
        capture_dependencies(state/'dependencies.json')
        record['title_build']=build(state/'title-native',state/'title-build');checkpoint()
        record['package']=build_candidate(state/'title-build',state/'title-native',state/'dependencies.json',state/'candidate')
        record['status']='candidate-packaged; console verification pending';checkpoint()
        return dict(status=record['status'],record=str(state/'bootstrap.json'),installed=False,full_sdk_bootstrap=False)
    except Exception as error:
        record.update(status='failed; partial outputs preserved',error=str(error));checkpoint();raise
