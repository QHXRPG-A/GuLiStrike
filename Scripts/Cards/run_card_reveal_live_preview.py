from pathlib import Path
CARD_REVEAL_LIVE=True
source=Path('D:/UE5.7/test1/Scripts/Cards/preview_card_reveal_worker.py')
exec(compile(source.read_text(encoding='utf-8'),str(source),'exec'),globals())
