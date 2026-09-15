"""Confirm the GAS migration only changes the Wingman coordinator's capability adapter."""
import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent.parent
# Freeze the pre-migration baseline so this check remains reproducible after committing the refactor.
baseline_commit = 'ed6e47a7641acfd8241e3933137adb9ce31e037f'
checks = {}
for name in ('GuLiWingmanAttackCombat.cpp', 'GuLiWingmanCombatCoordinator.cpp', 'GuLiWingmanCombatCoordinator.h'):
    path = 'Source/GuLiStrike/Battle/Combat/' + name
    original = subprocess.check_output(['git', 'show', baseline_commit + ':' + path], cwd=root).decode('utf-8').replace('\r\n', '\n')
    current = (root / path).read_text(encoding='utf-8')
    normalized = current.replace('Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h',
                                 'Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h')
    normalized = normalized.replace('UGuLiShipHangarCapabilityComponent', 'UGuLiShipAbilitySystemComponent')
    normalized = normalized.replace('HangarCapability', 'ShipASC').replace('ASC->GetOwner()', 'ASC->GetOwnerActor()')
    checks[path] = normalized == original
report = {'passed': all(checks.values()), 'baseline_commit': baseline_commit,
          'normalization': 'Only component type/include/context field and Actor owner accessor renamed', 'checks': checks}
(root / 'TestResults/ComponentSkills/wingman-combat-source-contract.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report))
raise SystemExit(0 if report['passed'] else 1)
