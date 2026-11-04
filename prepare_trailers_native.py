"""Prepare native-resolution previews; preserve source aspect up to 1080p/30 fps."""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from urllib.parse import urlsplit
from urllib.request import urlretrieve

ROOT=Path(__file__).resolve().parent
MAX_SECONDS=150

def key(value):
    parts=re.findall(r"[a-z0-9]+",value.lower())
    for suffix in (["xbox","series","x"],["xbox","series","s"],["xbox","360"],
                   ["xbox","one"],["ps5"],["ps4"],["ps3"],["xbox"],["pc"]):
        if parts[-len(suffix):]==suffix:
            parts=parts[:-len(suffix)]
            break
    return " ".join(parts)

def probe(path):
    result=subprocess.run(["ffprobe","-v","error","-select_streams","v:0","-show_entries",
        "stream=width,height,r_frame_rate,sample_aspect_ratio,display_aspect_ratio","-of","json",str(path)],
        check=True,capture_output=True,text=True)
    stream=json.loads(result.stdout)["streams"][0]
    w,h=int(stream["width"]),int(stream["height"])
    fps=max(1,min(30,round(float(Fraction(stream["r_frame_rate"])))))
    ratio=min(1,1920/w,1080/h)
    output_w=max(2,int(w*ratio)//2*2);output_h=max(2,int(h*ratio)//2*2)
    dar=stream.get("display_aspect_ratio","").replace(":","/")
    try:
        aspect=Fraction(dar)
    except (ValueError,ZeroDivisionError):
        sar=stream.get("sample_aspect_ratio","1:1").replace(":","/")
        try: aspect=Fraction(w,h)*Fraction(sar)
        except (ValueError,ZeroDivisionError): aspect=Fraction(w,h)
    return {"version":3,"width":output_w,"height":output_h,"fps":fps,"jpeg_quality":2,
            "aspect_num":aspect.numerator,"aspect_den":aspect.denominator,
            "source_width":w,"source_height":h,"source_fps":stream["r_frame_rate"],
            "source_bytes":path.stat().st_size}

def packed_frames(path):
    try:
        with path.open("rb") as file:
            head=file.read(8)
            if len(head)!=8 or head[:4]!=b"NXEF": return 0
            count=struct.unpack("<I",head[4:])[0]
            if not 0<count<=100000: return 0
            for _ in range(count):
                size=file.read(4)
                if len(size)!=4: return 0
                length=struct.unpack("<I",size)[0]
                if not 0<length<=10000000: return 0
                file.seek(length,1)
            return count if file.tell()==path.stat().st_size else 0
    except OSError:
        return 0

def prepare(entry,source_dir,output,rebuild=False):
    source=source_dir/entry["file"]
    profile=probe(source)
    slug=Path(entry["file"]).stem
    folder=output/"packed";folder.mkdir(parents=True,exist_ok=True)
    pack=folder/(slug+".frames");marker=folder/(slug+".profile.json")
    try: cached=json.loads(marker.read_text())==profile and packed_frames(pack)>0
    except (OSError,ValueError): cached=False
    if rebuild or not cached:
        with tempfile.TemporaryDirectory(prefix="exbox-trailer-") as work_dir:
            work=Path(work_dir)
            filters=f"fps={profile['fps']},scale={profile['width']}:{profile['height']}:flags=lanczos,setsar=1"
            subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y","-i",str(source),
                "-t",str(MAX_SECONDS),"-vf",filters,"-q:v","2",str(work/"%06d.jpg")],check=True)
            frames=sorted(work.glob("*.jpg"))
            if not frames: raise RuntimeError(f"No frames: {entry['game']}")
            partial=pack.with_suffix(".frames.part")
            with partial.open("wb") as file:
                file.write(b"NXEF"+struct.pack("<I",len(frames)))
                for frame in frames:
                    data=frame.read_bytes();file.write(struct.pack("<I",len(data)));file.write(data)
            if packed_frames(partial)!=len(frames): raise RuntimeError(f"Incomplete pack: {slug}")
            partial.replace(pack)
            marker.write_text(json.dumps(profile,indent=2)+"\n")
    count=packed_frames(pack)
    if not count: raise RuntimeError(f"Invalid pack: {slug}")
    audio=folder/(slug+".wav")
    audio_marker=audio.with_suffix(".audio.json")
    audio_profile={"version":1,"source_bytes":source.stat().st_size,"seconds":count/profile["fps"]}
    try: audio_cached=audio.is_file() and json.loads(audio_marker.read_text())==audio_profile
    except (OSError,ValueError): audio_cached=False
    if not audio_cached:
        partial=audio.with_suffix(".wav.part")
        filters=f"aresample=async=1:first_pts=0,apad,atrim=duration={audio_profile['seconds']:.9f}"
        result=subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y","-i",str(source),
            "-vn","-af",filters,"-ac","2","-ar","48000","-c:a","pcm_s16le","-f","wav",str(partial)])
        if result.returncode==0:
            partial.replace(audio);audio_marker.write_text(json.dumps(audio_profile,indent=2)+"\n")
        else: raise RuntimeError(f"Audio conversion failed: {entry['game']}")
    return slug,count,profile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources",type=Path,default=ROOT/"trailer_sources.json")
    parser.add_argument("--originals",type=Path,default=ROOT/"private/trailers/originals")
    parser.add_argument("--output",type=Path,default=ROOT/"private/trailers")
    parser.add_argument("--title")
    parser.add_argument("--download",action="store_true",help="fetch missing configured public sources at their available quality")
    parser.add_argument("--rebuild",action="store_true")
    parser.add_argument("--upgrade",action="store_true",help="upgrade missing/outdated profiles; native profiles are the default")
    args=parser.parse_args()
    entries=json.loads(args.sources.read_text())
    chosen=[entry for entry in entries if not args.title or key(entry["game"])==key(args.title)]
    if not chosen: parser.error("No matching configured title")
    args.output.mkdir(parents=True,exist_ok=True)
    manifest=args.output/"manifest.tsv"
    for index,entry in enumerate(chosen,1):
        source=args.originals/entry["file"]
        if not source.is_file():
            if not args.download:raise FileNotFoundError(f"{source} (use --download to fetch it)")
            source.parent.mkdir(parents=True,exist_ok=True)
            partial=source.with_suffix(source.suffix+".part")
            if urlsplit(entry["url"]).path.endswith(".m3u8"):
                subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y","-i",entry["url"],
                    "-t",str(MAX_SECONDS),"-c","copy","-f","mp4",str(partial)],check=True)
            else:urlretrieve(entry["url"],partial)
            partial.replace(source)
        slug,count,profile=prepare(entry,args.originals,args.output,args.rebuild)
        old=manifest.read_text().splitlines() if manifest.exists() else []
        rows=[row for row in old if len(row.split("\t"))>=2 and row.split("\t")[1]!=slug]
        for title_key in dict.fromkeys(key(title) for title in [entry["game"],*entry.get("aliases",[])]):
            rows.append(f"{title_key}\t{slug}\t{count}\t{profile['fps']}\t{entry['game']}\t{profile['aspect_num']}\t{profile['aspect_den']}")
        partial=manifest.with_suffix(".tsv.part")
        partial.write_text("\n".join(rows)+"\n");partial.replace(manifest)
        print(f"{index}/{len(chosen)} {entry['game']}: {profile['width']}x{profile['height']}, {profile['fps']} fps, {count} frames",flush=True)

if __name__=="__main__": main()
