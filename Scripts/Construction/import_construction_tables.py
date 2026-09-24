"""Import only the material registry and UI text data; leaves other in-progress tables alone."""
import contextlib
import gc
import io
import json
from pathlib import Path
import unreal


def run():
    root = Path('D:/UE5.7/test1')
    scope = {'__name__': '__main__', 'GULI_TABLE_FILTER': {'DT_GuLiStrikeVfx_Effects', 'DT_GuLiStrikeGameTexts_Texts'}}
    with contextlib.redirect_stdout(io.StringIO()):
        exec(compile((root / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8'), 'import_data_to_engine.py', 'exec'), scope)
    report = json.loads((root / 'Data/tmp_import_report.json').read_text(encoding='utf-8'))
    assert all(item.get('imported') for item in report['tables']), report
    return report


def main():
    try:
        result = {'success': True, 'report': run()}
    except Exception as error:
        result = {'success': False, 'error': str(error)}
    Path('D:/UE5.7/test1/outputs/construction-20260922/import.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=True))


main()
gc.collect()
