"""Import local video files into the existing Game Videos library.

Independent from avatar/native setup. Originals are retained byte-for-byte.
FFmpeg streams multipart JPEG straight into NXEF: no loose-frame export layer.
Nothing connects to a console or downloads a video.
"""
from pathlib import Path
from fractions import Fraction
import argparse,hashlib,json,os,re,struct,subprocess,sys,wave
from datetime import datetime,timezone
from population.storage import Plan
from verified_files import sha256 as digest
ROOT=Path(__file__).resolve().parents[1]
VIDEO={'.mp4','.m4v','.mkv','.webm','.wmv','.avi','.mov','.mpg','.mpeg','.ts','.m2ts','.mts','.ogv','.flv','.asf','.3gp'}
BOUNDARY=b'--exboxframe'


def inspect(source):
    args=['ffprobe','-v','error','-protocol_whitelist','file,pipe','-show_streams','-show_format','-of','json',str(source)]
    info=json.loads(subprocess.check_output(args,text=True))
    stream=next((s for s in info.get('streams',[]) if s.get('codec_type')=='video' and not s.get('disposition',{}).get('attached_pic')),None)
    if not stream:raise ValueError('No supported video stream: '+str(source))
    if stream.get('color_transfer') in ('smpte2084','arib-std-b67'):raise ValueError('HDR input needs a verified SDR conversion first; original is untouched')
    w,h=int(stream['width']),int(stream['height'])
    if w<2 or h<2:raise ValueError('Video dimensions must be at least 2 by 2')
    encoded_w,encoded_h=w,h
    rotation=next((float(s['rotation']) for s in stream.get('side_data_list',[]) if 'rotation' in s),float(stream.get('tags',{}).get('rotate',0)))
    if abs(rotation/90-round(rotation/90))>0.00001:raise ValueError('Only right-angle video rotation is currently supported')
    rotated=round(rotation/90)%2
    if rotated:w,h=h,w
    try:source_fps=Fraction(stream['r_frame_rate'])
    except (ValueError,ZeroDivisionError):
        try:source_fps=Fraction(stream.get('avg_frame_rate','0'))
        except (ValueError,ZeroDivisionError):raise ValueError('Video has no usable frame rate')
    if source_fps<=0:raise ValueError('Video has no usable frame rate')
    fps=max(1,min(30,round(float(source_fps))))
    scale=min(1,1920/w,1080/h)
    try:aspect=Fraction(stream.get('display_aspect_ratio','').replace(':','/'))
    except (ValueError,ZeroDivisionError):
        try:aspect=Fraction(encoded_w,encoded_h)*Fraction(stream.get('sample_aspect_ratio','1:1').replace(':','/'))
        except (ValueError,ZeroDivisionError):aspect=Fraction(encoded_w,encoded_h)
    if aspect<=0:raise ValueError('Video has no usable display aspect')
    if rotated:aspect=1/aspect
    if aspect<=0 or aspect.numerator>100000 or aspect.denominator>100000:raise ValueError('Unsupported display aspect')
    profile=dict(version=3,width=max(2,int(w*scale)//2*2),height=max(2,int(h*scale)//2*2),fps=fps,jpeg_quality=2,
        aspect_num=aspect.numerator,aspect_den=aspect.denominator,source_width=w,source_height=h,
        source_fps=str(source_fps),source_bytes=source.stat().st_size,
        video_stream_index=int(stream['index']),encoded_width=encoded_w,encoded_height=encoded_h,rotation_degrees=rotation)
    duration=info.get('format',{}).get('duration') or stream.get('duration')
    if duration is not None and float(duration)*fps>100000:raise ValueError('Clip exceeds the current player limit of 100,000 frames; original is untouched')
    return profile,any(s.get('codec_type')=='audio' for s in info.get('streams',[]))


def multipart_frames(stream):
    """Trust bounded Content-Length, never search JPEG payload for delimiters."""
    while True:
        line=stream.readline(1025)
        if not line:return
        if len(line)>1024:raise ValueError('Oversize multipart delimiter')
        line=line.strip()
        if not line:continue
        if line==BOUNDARY+b'--':return
        if line!=BOUNDARY:raise ValueError('Unexpected JPEG stream delimiter')
        headers={}
        for _ in range(16):
            line=stream.readline(1025)
            # FFmpeg ends mpjpeg with an empty opening boundary, then EOF.
            if not line and not headers:return
            if len(line)>1024 or not line:raise ValueError('Truncated multipart headers')
            if not line.strip():break
            name,separator,value=line.partition(b':')
            if not separator:raise ValueError('Invalid multipart header')
            headers[name.lower().strip()]=value.strip()
        else:raise ValueError('Excess multipart headers')
        length=int(headers.get(b'content-length',b'0'))
        if not 0<length<=10000000:raise ValueError('Invalid packed JPEG size')
        data=bytearray()
        while len(data)<length:
            block=stream.read(length-len(data))
            if not block:raise ValueError('Truncated JPEG frame')
            data.extend(block)
        if data[:2]!=b'\xff\xd8' or data[-2:]!=b'\xff\xd9':raise ValueError('Invalid JPEG envelope')
        yield data


def encode(source,job,profile,has_audio):
    job.mkdir(parents=True,exist_ok=False)
    filters=f"fps={profile['fps']},scale={profile['width']}:{profile['height']}:flags=lanczos,setsar=1"
    args=['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-protocol_whitelist','file,pipe','-i',str(source),
        '-map','0:'+str(profile['video_stream_index']),'-an','-vf',filters,'-frames:v','100001','-q:v','2','-threads','2','-f','mpjpeg','-boundary_tag','exboxframe','pipe:1']
    (job/'conversion.json').write_text(json.dumps(dict(source=str(source),profile=profile,video_command=args,has_audio=has_audio),indent=2)+'\n',encoding='utf-8')
    count=0;pack=job/'video.frames'
    with (job/'video-encode.log').open('wb') as log,pack.open('wb') as output:
        process=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=log)
        try:
            output.write(b'NXEF'+bytes(4))
            for frame in multipart_frames(process.stdout):
                count+=1
                if count>100000:raise ValueError('Clip exceeds 100,000 frames; no truncated clip is published')
                output.write(struct.pack('<I',len(frame)));output.write(frame)
            if process.wait()!=0:raise ValueError('Video decoding failed; see '+str(job/'video-encode.log'))
            if not count:raise ValueError('Video produced no frames')
            output.seek(4);output.write(struct.pack('<I',count))
        finally:
            process.stdout.close()
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    seconds=count/profile['fps'];audio=job/'video.wav'
    if has_audio:
        args=['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-protocol_whitelist','file,pipe','-i',str(source),'-map','0:a:0','-vn',
            '-af',f'aresample=async=1:first_pts=0,apad,atrim=duration={seconds:.9f}']
    else:args=['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-f','lavfi','-i','anullsrc=r=48000:cl=stereo','-t',f'{seconds:.9f}']
    with (job/'audio-encode.log').open('wb') as log:
        subprocess.run(args+['-ac','2','-ar','48000','-c:a','pcm_s16le',str(audio)],stdout=log,stderr=subprocess.STDOUT,check=True)
    with wave.open(str(audio)) as stream:
        if abs(stream.getnframes()/stream.getframerate()-seconds)>0.02:raise ValueError('Audio/video duration mismatch')
    return count


def title_for(source):
    sidecar=source.with_suffix(source.suffix+'.json')
    title=json.loads(sidecar.read_text(encoding='utf-8-sig')).get('title') if sidecar.is_file() else source.stem
    if not isinstance(title,str) or not title.strip() or any(c in title for c in '\t\r\n\0'):raise ValueError('Invalid trailer title')
    # Runtime reads display names into its existing 192-byte buffer.
    if len(title.encode('utf-8'))>180:raise ValueError('Trailer title exceeds 180 UTF-8 bytes')
    return title.strip()


def import_one(source,output,check=False):
    source=source.resolve(strict=True);output=output.resolve()
    if not source.is_file() or source.suffix.lower() not in VIDEO:raise ValueError('Unsupported local video file')
    if not output.is_relative_to((ROOT/'private').resolve()):raise ValueError('Trailer output must stay under project/private')
    identity=digest(source);slug='local-'+identity[:24];title=title_for(source)
    sidecar=source.with_suffix(source.suffix+'.json')
    metadata_bytes=sidecar.read_bytes() if sidecar.is_file() else None
    profile,has_audio=inspect(source)
    proof=output/'imports'/(identity+'.json')
    if proof.is_file():
        saved=json.loads(proof.read_text(encoding='utf-8'))
        for row in saved['files']:
            target=output/row['path']
            if not target.resolve().is_relative_to(output) or digest(target)!=row['sha256']:raise ValueError('Existing imported trailer changed; preserved')
        manifest=output/'manifest.tsv'
        if not manifest.is_file() or not any(len(fields:=line.split('\t'))>1 and fields[1]==saved['slug'] for line in manifest.read_text(encoding='utf-8').splitlines()):raise ValueError('Imported media is missing from the manifest; existing files preserved')
        return dict(status='already-imported',source=str(source),title=saved['title'],proof=str(proof))
    if check:return dict(status='ready; no files written',source=str(source),title=title,source_sha256=identity,profile=profile,has_audio=has_audio)
    # Unique jobs retain failures for diagnosis; nothing writes into active packs
    # until the complete clip, PCM and original byte copy have been verified.
    job=ROOT/'private/trailer-import/jobs'/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(os.getpid()))
    count=encode(source,job,profile,has_audio)
    if digest(source)!=identity:raise ValueError('Original video changed during import')
    plan=Plan(output);plan.protect(source);records=[]
    for suffix,original in [('.frames',job/'video.frames'),('.wav',job/'video.wav')]:
        relative='packed/'+slug+suffix;plan.add_file(relative,original,dict(kind='converted-media',source_sha256=identity,originality='converted; original retained',profile=profile));records.append(dict(path=relative,sha256=digest(original)))
    archive='originals/'+slug+source.suffix.lower();plan.add_file(archive,source,dict(kind='original-video',sha256=identity,conversion=None));records.append(dict(path=archive,sha256=identity))
    if metadata_bytes is not None:
        metadata_path=archive+'.json'
        plan.add_bytes(metadata_path,metadata_bytes,dict(kind='original-supplied-metadata',source=str(sidecar),conversion=None))
        records.append(dict(path=metadata_path,sha256=hashlib.sha256(metadata_bytes).hexdigest()))
    relative='packed/'+slug+'.profile.json';payload=(json.dumps(profile,indent=2)+'\n').encode()
    plan.add_bytes(relative,payload,dict(kind='conversion-profile'));records.append(dict(path=relative,sha256=hashlib.sha256(payload).hexdigest()))
    manifest=output/'manifest.tsv';old=manifest.read_bytes() if manifest.is_file() else b''
    row=f"local {identity[:24]}\t{slug}\t{count}\t{profile['fps']}\t{title}\t{profile['aspect_num']}\t{profile['aspect_den']}\n".encode('utf-8')
    # Keep all pre-existing rows byte-for-byte. Imported clips have unique keys;
    # game associations remain the owner's choice in existing game settings.
    if any(line.split(b'\t')[0]==row.split(b'\t')[0] for line in old.splitlines()):raise ValueError('Imported trailer key already exists without verified proof')
    plan.add_bytes('manifest.tsv',old+(b'\n' if old and not old.endswith(b'\n') else b'')+row,dict(kind='generated-media-index'),generated=True)
    record=dict(schema=1,title=title,source=str(source),source_sha256=identity,original_untouched=True,
        slug=slug,frames=count,profile=profile,silent_source=not has_audio,files=records,network=False,
        loose_jpeg_files_created=0,originality='original video retained; NXEF/JPEG and PCM are explicitly converted runtime media')
    plan.add_bytes('imports/'+identity+'.json',(json.dumps(record,indent=2)+'\n').encode(),dict(kind='conversion-provenance'))
    result=plan.apply()
    return dict(status='imported',title=title,source=str(source),proof=str(proof),frames=count,writes=result)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'inputs/trailers',help='Local video or drop folder')
    parser.add_argument('--output',type=Path,default=ROOT/'private/trailers')
    parser.add_argument('--check',action='store_true',help='Probe inputs; never encode or write')
    args=parser.parse_args(argv);source=args.source
    if not source.exists():parser.error('Trailer drop folder not found: '+str(source))
    paths=[source] if source.is_file() else sorted(p for p in source.iterdir() if p.is_file() and p.suffix.lower() in VIDEO)
    results=[];errors=[]
    for path in paths:
        try:
            if path.is_symlink():raise ValueError('Symlink video refused')
            result=import_one(path,args.output,args.check);results.append(result);print(json.dumps(result),flush=True)
        except (OSError,ValueError,KeyError,StopIteration,subprocess.CalledProcessError) as error:errors.append(dict(source=str(path),error=str(error)));print(json.dumps(errors[-1]),file=sys.stderr,flush=True)
    print(json.dumps(dict(clips=len(paths),results=results,errors=errors,console_actions=False),indent=2))
    return 1 if errors else 0


if __name__=='__main__':raise SystemExit(main())
