"""Calibrate broad Blades illumination from empty areas of reference captures.

Retail text, icons, pictures and controls are masked before sampling. Missing
material samples are harmonically interpolated. This is a static lighting
calibration aid, not the original animated XUI background shader.
"""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
from PIL import Image

def reconstruct(source,active):
    image=Image.open(source).convert('RGB').resize((1920,1080),Image.Resampling.LANCZOS)
    pixels=np.asarray(image)
    content=253+(active-1)*80;right=875+(active-1)*80;edge=1498+(active-1)*74
    mask=np.ones((1080,1920),bool)
    mask[:,:content-10]=False;mask[:,edge-30:]=False
    boxes=[(content-15,60,content+300,130)]
    if active==4:
        boxes += [(content-15,150,content+610,615),(right-10,150,edge,560)]
    else:
        rows_end={1:590,2:742,3:670}[active]
        boxes += [(content-10,135,content+600,360),(content-10,370,content+600,rows_end),
            (right+50,140,right+510,355),(right-10,370,edge,640),
            (content-30,765,edge,883)]
        if active==1:boxes.append((content-10,600,content+600,765))
    left=268+(active-1)*80;right_prompt=1427+(active-1)*80
    boxes += [(left-40,900,left+145,955),(left-40,945,left+320,1010),
        (right_prompt-85,900,right_prompt+35,955),(right_prompt-260,945,right_prompt+35,1010)]
    for x0,y0,x1,y1 in boxes:mask[max(y0,0):min(y1,1080),max(x0,0):min(x1,1920)]=False
    rows,columns=54,96
    grid=np.zeros((rows,columns,3),np.float64);known=np.zeros((rows,columns),bool)
    for row in range(rows):
        for col in range(columns):
            region=pixels[row*20:(row+1)*20,col*20:(col+1)*20]
            valid=mask[row*20:(row+1)*20,col*20:(col+1)*20]
            if valid.sum()>=240:
                grid[row,col]=np.median(region[valid],axis=0);known[row,col]=True
    # Separate glass/body zones so masked controls do not smear across a seam.
    for start,end in [(0,7),(7,45),(45,54)]:
        values=grid[start:end];fixed=known[start:end]
        if not fixed.any():raise ValueError('Reference contains no usable background samples')
        values[~fixed]=np.median(values[fixed],axis=0)
        for iteration in range(2400):
            border=np.pad(values,((1,1),(1,1),(0,0)),mode='edge')
            average=(border[1:-1,:-2]+border[1:-1,2:]+border[:-2,1:-1]+border[2:,1:-1])/4
            change=float(np.max(np.abs(average[~fixed]-values[~fixed])))
            values[~fixed]=average[~fixed]
            if change<.005:break
    field=Image.fromarray(np.uint8(np.clip(grid,0,255)),'RGB').resize((1920,1080),Image.Resampling.BICUBIC)
    return field,dict(source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        grid=[columns,rows],known_samples=int(known.sum()),excluded_controls=boxes)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('reference',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    report=[]
    for active,name in [(1,'live'),(2,'games'),(3,'music'),(4,'system')]:
        field,record=reconstruct(args.reference/f'designer-blades-{name}.jpg',active)
        target=args.output/f'lightfield-{active}.png';field.save(target)
        report.append(dict(file=target.name,**record))
    (args.output/'lightfield-sources.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Calibrated four material lightfields; retail controls excluded')
