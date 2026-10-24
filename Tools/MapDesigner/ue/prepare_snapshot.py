"""Convert an exported editor snapshot into the first .gmap, without contacting UE."""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import storage


def main():
    parser=argparse.ArgumentParser();parser.add_argument('snapshot_id');args=parser.parse_args()
    storage.init_storage();result=storage.import_snapshot(storage.SNAPSHOTS/storage.ident(args.snapshot_id))
    print(json.dumps({k:v for k,v in result.items() if k!='project'},ensure_ascii=False,indent=2))


if __name__=='__main__':main()
