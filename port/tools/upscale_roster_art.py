"""Upscale original native overlay rasters; never redraw or replace existing HD files."""
import argparse,hashlib,json,struct
from pathlib import Path
from PIL import Image

def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--factor',type=int,default=4);p.add_argument('--hd-minimum',type=int,default=128);p.add_argument('--target',type=int,default=0,help='Minimum square side; preserve files already this size');args=p.parse_args()
 if args.factor<1 or args.factor>8: p.error('factor must be1..8')
 source=args.source.resolve();dest=args.output.resolve()
 if source==dest or source.is_relative_to(dest):p.error('output must be a separate overlay directory')
 changed=[];preserved=[]
 for folder in ['beta8/mod-select/chips','beta8/custom-small-portraits','beta8/small-portraits']:
  for f in sorted((source/folder).glob('*.rgba')):
   raw=f.read_bytes();x,y=struct.unpack_from('<II',raw);rel=f.relative_to(source);row={'path':str(rel).replace('\\','/'),'before':hashlib.sha256(raw).hexdigest()}
   if min(x,y)>=(args.target or args.hd_minimum):preserved.append(row);continue
   scale=(args.target/min(x,y))if args.target else args.factor
   if len(raw)!=8+x*y*4 or max(x,y)*scale>512:raise ValueError(str(f))
   im=Image.frombytes('RGBA',(x,y),raw[8:]).resize((round(x*scale),round(y*scale)),Image.Resampling.LANCZOS)
   payload=struct.pack('<II',im.width,im.height)+im.tobytes();out=dest/rel;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(payload);row.update(sha256=hashlib.sha256(payload).hexdigest(),dimensions=list(im.size));changed.append(row)
 dest.mkdir(parents=True,exist_ok=True);(dest/'original-upscale-manifest.json').write_text(json.dumps({'method':'Lanczos, original pixels only','assets':changed,'preserved_hd':preserved},indent=2),encoding='utf-8');print('Upscaled',len(changed),'preserved HD',len(preserved))
if __name__=='__main__':main()
