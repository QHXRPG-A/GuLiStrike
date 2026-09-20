"""Reapply the scoped UE5.7 Niagara bool fix after updating the ignored VibeUE plugin.

Run from the project root. --check reads without changing the source.
Rebuild GuLiStrikeEditor and GuLiStrike from the source engine after an actual edit.
"""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
source = Path(__file__).resolve().parents[2] / 'Plugins/VibeUE/Source/VibeUE/Private/PythonAPI/UNiagaraService.cpp'
original = source.read_bytes()
text = original.decode('utf-8')
fixed = text.replace('GetParameterValue<bool>(Param)', 'GetParameterValue<FNiagaraBool>(Param).GetValue()')

def fix_bool_branch(match):
    block = match.group(0)
    if 'Value.ToBool()' not in block:
        return block
    # Niagara bool storage is four bytes; C++ bool is not its storage type.
    block = re.sub(r'\b(?:bool|int32) (Val|BoolValue) = ([^;]+);', r'FNiagaraBool \1(\2);', block)
    block = re.sub(r'FMemory::Memcpy\(Data, &Val, sizeof\((?:bool|int32)\)\)', 'FMemory::Memcpy(Data, &Val, sizeof(Val))', block)
    return block

fixed = re.sub(r'else if \(TypeDef == FNiagaraTypeDefinition::GetBoolDef\(\)\)\s*\{[^{}]*\}', fix_bool_branch, fixed)
changed = fixed != text
if changed and not args.check:
    source.write_bytes(fixed.encode('utf-8'))
print(f'{source}: ' + ('repair needed' if changed and args.check else 'repaired; rebuild required' if changed else 'already uses FNiagaraBool'))
raise SystemExit(1 if changed and args.check else 0)
