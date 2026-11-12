"""Cross-table contracts for the circle movement source workbook."""
from __future__ import annotations

import math


def validate(tables, error_type=ValueError):
    names = {sheet: f"DT_GuLiStrikeAvoidance_{sheet}" for sheet in
             ("Units", "BuildingCircles", "OreClustering")}
    missing = [name for name in names.values() if name not in tables]
    if missing:
        raise error_type("避障源表缺少工作表：" + ", ".join(missing))

    def rows(sheet):
        return tables[names[sheet]]["rows"]

    def fail(sheet, row, message):
        raise error_type(f"GuLiStrikeAvoidance.xlsx / {sheet} / {row.get('Name', '?')}: {message}")

    def number(sheet, row, key, minimum=0, strictly_positive=False):
        value = row.get(key)
        if (isinstance(value, bool) or not isinstance(value, (int, float))
                or not math.isfinite(value) or value < minimum or (strictly_positive and value <= 0)):
            fail(sheet, row, f"{key} 必须为有效的{'正数' if strictly_positive else '范围内数字'}")
        return value

    soldiers = {r["Id"] for r in tables["DT_GuLiStrikeCommander_Soldiers"]["rows"]}
    units = rows("Units")
    ids = [r["UnitTypeId"] for r in units]
    if len(ids) != len(set(ids)) or set(ids) != soldiers:
        raise error_type(f"Units.UnitTypeId 必须不重复且完整覆盖 Soldiers：缺少{sorted(soldiers-set(ids))}，未知{sorted(set(ids)-soldiers)}")
    for row in units:
        for key in ("RadiusMeters", "GridReferenceDiameterMeters", "LookAheadSeconds", "YieldSeconds"):
            number("Units", row, key, strictly_positive=True)
        for key in ("ClearanceMeters", "ContactMarginCm", "ClearancePreference", "SidePreferenceRadians"):
            number("Units", row, key)
        angles = [number("Units", row, f"TurnAngle{i}Degrees", strictly_positive=True) for i in range(1, 8)]
        if angles[-1] >= 180 or any(a >= b for a, b in zip(angles, angles[1:])):
            fail("Units", row, "7个侧绕采样角必须在0至180度内严格递增")

    definitions = {r["Id"]: r for r in tables["DT_GuLiStrikeBuildings_Buildings"]["rows"]}
    seen, covered = set(), set()
    for row in rows("BuildingCircles"):
        definition, index = row["BuildingDefinitionId"], row["CircleIndex"]
        if definition not in definitions or not 0 <= index <= 65535:
            fail("BuildingCircles", row, "建筑引用不存在或圆编号不在0至65535范围内")
        key = (definition, index)
        if key in seen:
            fail("BuildingCircles", row, "同一建筑的圆编号重复")
        seen.add(key)
        number("BuildingCircles", row, "RadiusMeters", strictly_positive=True)
        center = row.get("LocalCenterMeters", {})
        if set(center) != {"X", "Y", "Z"} or any(not math.isfinite(v) for v in center.values()):
            fail("BuildingCircles", row, "局部圆心必须为完整有效的XYZ向量")
        low, high = row["HeightMinimumMeters"], row["HeightMaximumMeters"]
        if not math.isfinite(low) or not math.isfinite(high) or low >= high:
            fail("BuildingCircles", row, "高度范围必须有效且下界小于上界")
        phase = row["PhaseMask"]
        if phase not in (1, 2, 3):
            fail("BuildingCircles", row, "PhaseMask只允许施工中1、完工2、两者3")
        for flag in (1, 2):
            if phase & flag:
                covered.add((definition, flag))
    for definition, row in definitions.items():
        required = (1,) if row.get("CompletionUnitTypeId", 0) else (1, 2)
        if any((definition, flag) not in covered for flag in required):
            raise error_type(f"BuildingCircles未覆盖建筑{definition}的有效施工阶段")

    ore = rows("OreClustering")
    if len(ore) != 1 or ore[0]["Id"] != 1:
        raise error_type("OreClustering必须有且只有Id=1的当前聚合配置")
    for key in ("MaximumRadiusRatio", "MaximumAreaRatio"):
        number("OreClustering", ore[0], key, minimum=1)
    if ore[0]["PreserveWorkEntrances"] is not True:
        fail("OreClustering", ore[0], "必须保留工作入口和通道")
