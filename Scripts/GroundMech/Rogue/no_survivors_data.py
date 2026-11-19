"""Register the range's two VFX bindings and its measured 1x blast in the source workbooks."""
import argparse
import json
from pathlib import Path

import update_initial_bullet_cards as excel

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "Artifacts/GroundNoSurvivors"
FLIGHT = "/Game/GuLiStrike/FX/GroundMech/NoSurvivors/NS_NoSurvivorsFlight"
IMPACT = "/Game/GuLiStrike/FX/GroundMech/NoSurvivors/NS_NoSurvivorsImpact"


def install(measured):
    excel.OUT = OUT
    book, sheet = "GuLiStrikeMech.xlsx", "技能表"
    skills = excel.read(book, sheet)
    fields = ("HorizontalRangeCentimeters", "DiveAccelerationCmPerSecondSquared")
    missing = [field for field in fields if field not in skills[0]]
    for col, field in enumerate(missing, len(skills[0]) + 1):
        excel.write(book, sheet, [excel.cell(i, col, v) for i, v in enumerate((field, "float", "Optional"), 1)])
    skill = next((dict(zip(skills[0], row)) for row in skills[3:] if row[1] == "GroundNoSurvivors"), {})
    new_skill = not skill
    migrate_dive = not skill.get("DiveAccelerationCmPerSecondSquared")
    pending_skill = None
    if new_skill:
        pending_skill = dict(id=4, name="GroundNoSurvivors", Note="游骑兵无人生还；从每发肩部发射点量水平射程，单位厘米",
                     ExecutionType="Barrage", HorizontalRangeCentimeters=20000, ProjectileSpeed=4550,
                     ProjectileLifetime=30, DiveAccelerationCmPerSecondSquared=9500, SweepRadius=0, BulletVfxId=67, ImpactVfxId=68, IconId=9)
    elif migrate_dive:
        pending_skill = dict(id=4, name="GroundNoSurvivors", DiveAccelerationCmPerSecondSquared=9500, SweepRadius=0)
    records = []
    vfx_rows = excel.read("GuLiStrikeVfx.xlsx", "Effects")
    existing = {row[1]: dict(zip(vfx_rows[0], row)) for row in vfx_rows[3:]}
    for number, name, path in [(67, "GroundNoSurvivorsFlight", FLIGHT), (68, "GroundNoSurvivorsImpact", IMPACT)]:
        record = dict(id=number, name=name, ResourcePath=path+"."+path.rsplit("/",1)[-1])
        if name not in existing or (number == 67 and new_skill):
            scale = 2 if number == 67 else 1
            record.update(Note="游骑兵无人生还，弹头倍率与连续尾迹" if number==67 else "游骑兵无人生还，爆炸原始一倍",
                          ScaleX=scale, ScaleY=scale, ScaleZ=scale, InitialStyleLengthMultiplier=1)
            if number == 67:
                record.update(TrailLengthCm=2730, TrailFadeSeconds=.6)
        elif number == 67 and migrate_dive:
            record.update(TrailLengthCm=2730, TrailFadeSeconds=.6)
        records.append(record)
    if measured:
        calibration = json.loads((OUT/"calibration-optimized.json").read_text(encoding="utf8"))
        assert calibration["success"] and calibration["system"] == IMPACT
        radius = calibration["radius_cm"]
        assert 0 < radius < 10000
        records[1]["RadiusAtUnitScaleCm"] = radius
        field = dict(id=3, name="GroundNoSurvivors", Note="游骑兵超级导弹单发爆炸，原始一倍主爆发层实测半径",
                     FieldType="Combat", Damage=30, RadiusCentimeters=radius, Timing="Instant", DelaySeconds=0,
                     DurationSeconds=0, PulseIntervalSeconds=1, DissipationSeconds=5.25)
        rows = excel.read("GuLiStrikeSpellFields.xlsx", "Fields")
        current = next((dict(zip(rows[0], row)) for row in rows[3:] if row and row[0] == "3"), {})
        if any(current.get(key) != str(value) for key, value in field.items()):
            excel.upsert("GuLiStrikeSpellFields.xlsx", "Fields", [field])
    excel.upsert("GuLiStrikeVfx.xlsx", "Effects", records)
    # Commit the migration marker last so a partial workbook write can be retried.
    if pending_skill:
        excel.upsert(book, sheet, [pending_skill])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--measured", action="store_true")
    install(parser.parse_args().measured)
