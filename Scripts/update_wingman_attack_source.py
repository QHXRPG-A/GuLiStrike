"""Compatibility entry: export authored secondary weapons without restoring seed values.

Edit Data/Excel/GuLiStrikeSecondaryWeapons.xlsx, then run this script or the
regular exporter. Weapon tuning is never maintained as a second Python literal.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    book = ROOT / 'Data/Excel/GuLiStrikeSecondaryWeapons.xlsx'
    if not book.is_file():
        raise RuntimeError('Run the secondary weapon workbook migration first')
    subprocess.run([sys.executable, str(ROOT / 'Tools/DataPipeline/export_data_from_excel.py')], check=True)
    print('Source: GuLiStrikeSecondaryWeapons.xlsx / WingmanWeapons and WingmanTargeting')
    print('Next: python Scripts/ue_exec.py Scripts/import_secondary_weapon_data.py')


if __name__ == '__main__':
    main()
