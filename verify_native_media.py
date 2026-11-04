"""Verify native preview profiles, every pack boundary and representative frames."""
from io import BytesIO
import argparse
import json
from pathlib import Path
import struct
import wave
from PIL import Image

def verify(root, media):
    sources=json.loads((root/"trailer_sources.json").read_text(encoding="utf-8"))
    rows=[line.split("\t") for line in (media/"manifest.tsv").read_text(encoding="utf-8").splitlines()]
    errors=[];results=[]
    for entry in sources:
        slug=Path(entry["file"]).stem
        pack=media/"packed"/(slug+".frames")
        try:
            profile=json.loads((pack.with_suffix(".profile.json")).read_text())
            assert profile["jpeg_quality"]==2 and profile["version"]==3,"quality profile"
            assert profile["width"]<=1920 and profile["height"]<=1080,"resolution limit"
            assert profile["width"]<=profile["source_width"] and profile["height"]<=profile["source_height"],"upscaled source"
            assert 1<=profile["fps"]<=30,"frame rate"
            with pack.open("rb") as file:
                header=file.read(8)
                assert header[:4]==b"NXEF" and len(header)==8,"header"
                frames=struct.unpack("<I",header[4:])[0]
                assert 0<frames<=100000,"frame count"
                for index in range(frames):
                    raw=file.read(4)
                    assert len(raw)==4,f"truncated frame {index}"
                    length=struct.unpack("<I",raw)[0]
                    assert 0<length<=10000000 and file.tell()+length<=pack.stat().st_size,f"frame length {index}"
                    if index in (0,frames//2,frames-1):
                        image=Image.open(BytesIO(file.read(length)));image.load()
                        assert image.size==(profile["width"],profile["height"]),f"dimensions {index}"
                    else:file.seek(length,1)
                assert file.tell()==pack.stat().st_size,"trailing data"
            matched=[row for row in rows if len(row)==7 and row[1]==slug]
            assert matched,"manifest missing"
            assert all((int(r[2]),int(r[3]),int(r[5]),int(r[6]))==
                (frames,profile["fps"],profile["aspect_num"],profile["aspect_den"]) for r in matched),"manifest metadata"
            with wave.open(str(pack.with_suffix(".wav"))) as audio:
                assert audio.getnchannels()==2 and audio.getsampwidth()==2 and audio.getframerate() in (44100,48000),"PCM format"
                audio_seconds=audio.getnframes()/audio.getframerate()
            seconds=frames/profile["fps"]
            assert abs(audio_seconds-seconds)<0.2,f"audio/video duration {audio_seconds:.3f}/{seconds:.3f}"
            results.append({"slug":slug,"frames":frames,"width":profile["width"],"height":profile["height"],
                            "fps":profile["fps"],"seconds":seconds,"bytes":pack.stat().st_size})
        except (OSError,ValueError,AssertionError,KeyError,struct.error) as error:
            errors.append(f"{slug}: {error}")
    report={"clips":len(sources),"manifest_rows":len(rows),"verified":len(results),"errors":errors,"results":results}
    print(json.dumps({k:v for k,v in report.items() if k!="results"},indent=2))
    return report

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root",type=Path,nargs="?",default=Path(__file__).resolve().parent)
    parser.add_argument("--media",type=Path)
    parser.add_argument("--report",type=Path)
    args=parser.parse_args()
    report=verify(args.root,args.media or args.root/"private/trailers")
    if args.report:args.report.write_text(json.dumps(report,indent=2)+"\n")
    raise SystemExit(bool(report["errors"]))
