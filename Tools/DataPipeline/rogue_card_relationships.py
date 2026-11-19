"""Prerequisite and exclusion rules shared by the exporter and graph editor."""


def validate(cards, fail):
    """Check rows keyed by card ID. fail(row, message) raises the caller's error."""
    excluded = {ident: set() for ident in cards}
    for ident, row in cards.items():
        for field in ("RequiredCardIds", "ExcludedCardIds"):
            refs = row[field]
            if len(set(refs)) != len(refs):
                fail(row, f"{field}含重复引用")
            if ident in refs:
                fail(row, f"{field}不能引用自身")
            missing = set(refs) - cards.keys()
            if missing:
                fail(row, f"{field}引用未知卡: {sorted(missing)}")
            if any(cards[other]["Type"] != row["Type"] for other in refs):
                fail(row, f"{field}不允许跨卡池引用")
        levels = row.get("RequiredCardLevels", [])
        if levels and len(levels) != len(row["RequiredCardIds"]):
            fail(row, "前置等级数量必须与前置卡一致")
        for parent, value in zip(row["RequiredCardIds"], levels):
            cap = cards[parent].get("MaxAcquisitions", 0)
            if not str(value).isdigit() or not 1 <= int(value) <= (cap or 2**31-1):
                fail(row, f"前置卡 {parent} 的等级无效")
        for other in row["ExcludedCardIds"]:
            excluded[ident].add(other)
            excluded[other].add(ident)

    # All members of a prerequisite closure must be obtainable together.
    closures = {}
    visiting = []

    def closure(ident):
        if ident in visiting:
            cycle = visiting[visiting.index(ident):] + [ident]
            fail(cards[ident], "依赖环: " + " -> ".join(cycle))
        if ident in closures:
            return closures[ident]
        visiting.append(ident)
        result = {ident}
        for required in cards[ident]["RequiredCardIds"]:
            result.update(closure(required))
        visiting.pop()
        for member in sorted(result):
            conflicts = result & excluded[member]
            if conflicts:
                fail(cards[ident], f"依赖链内部互斥（含卡牌自身）: {member} 与 {sorted(conflicts)}")
        closures[ident] = result
        return result

    for ident in cards:
        closure(ident)
