"""Read the formal card exports and build a reviewable design draft."""

import hashlib
import json
import re
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
DRAFT_PATH = PROJECT / "data/Design/RogueCardGraph.json"
SEATS = {1: "Commander", 2: "Air", 3: "Ground"}
EXPORTS = {
    "cards": "data/Json/DT_GuLiStrikeRogueCards_Cards.json",
    "texts": "data/Json/DT_GuLiStrikeGameTexts_Texts.json",
    "effects": "data/Json/DT_GuLiStrikeRogueCards_Effects.json",
    "weapons": "data/Json/DT_GuLiStrikeRogueCards_GroundWeaponEffects.json",
}

sys.path.insert(0, str(PROJECT / "Tools/DataPipeline"))
from rogue_card_relationships import validate as validate_relationships


def digest(payload):
    encoded = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def plain_text(value):
    return re.sub(r"<(?:/?[A-Za-z][A-Za-z0-9_]*|/)>", "", value)


def preview_descriptions(row, template, effects, weapons):
    if row["Type"] == 3:
        definitions = [effects[ident] for ident in row["EffectIds"]]
        has_level_values = any(
            effect["Handler"] in ("ground/explosive_package", "ground/explosive_enhancement")
            for effect in definitions
        )
        levels = range(1, row["MaxAcquisitions"] + 1) if has_level_values else (1,)
    else:
        definitions = []
        levels = (1,)

    def arguments(level):
        if row["Type"] != 3:
            return [row["BonusCount"] or row["BonusPercent"] * 100]
        values = []
        for effect in definitions:
            handler = effect["Handler"]
            values.append(level)
            if not handler.startswith("ground/"):
                continue
            config = weapons[str(effect["ConfigId"])]
            if handler == "ground/fire_rate":
                values.append(config["FireRateBonusPercent"] * 100)
            elif handler == "ground/explosive_package":
                values.extend((
                    float(config["ExplosiveIntervals"][level - 1]),
                    config["ExplosiveDamageMultiplier"],
                    config["ExplosiveRadiusCentimeters"] / 100,
                ))
            elif handler == "ground/explosive_enhancement":
                values.extend(
                    float(config[field][level - 1]) * 100
                    for field in ("ExplosiveRadiusBonuses", "ExplosiveDamageBonuses")
                )
        return values

    previews = []
    for level in levels:
        values = arguments(level)

        def substitute(match):
            index = int(match[1])
            if index >= len(values):
                return match[0]
            return f"{values[index]:.2f}".rstrip("0").rstrip(".")

        previews.append(re.sub(r"\{(\d+)\}", substitute, template))
    return previews


def make_link(kind, source, target):
    if kind == "excludes":
        source, target = sorted((source, target))
    return {"kind": kind, "source": source, "target": target}


def load_catalog():
    tables = {
        name: json.loads((PROJECT / path).read_text(encoding="utf-8-sig"))
        for name, path in EXPORTS.items()
    }
    rows = {row["Id"]: row for row in tables["cards"]}
    if len(rows) != len(tables["cards"]):
        raise ValueError("正式卡表存在重复 ID。")

    def fail(row, message):
        raise ValueError(f"正式卡表 {row['Id']}：{message}")

    validate_relationships(rows, fail)
    texts = {row["TextId"]: row for row in tables["texts"]}
    effects = {str(row["Id"]): row for row in tables["effects"]}
    weapons = {str(row["Id"]): row for row in tables["weapons"]}
    cards, links, details = [], [], {}
    used_texts, exclusions = {}, set()
    for ident, row in sorted(rows.items()):
        title_key, description_key = row["TextIds"]
        title_row, description_row = texts[title_key], texts[description_key]
        used_texts[title_key] = title_row
        used_texts[description_key] = description_row
        description = plain_text(description_row["Content"])
        cards.append({
            "id": ident, "seat": SEATS[row["Type"]],
            "title": plain_text(title_row["Content"]), "description": description,
        })
        details[ident] = {
            "rowName": row["Name"], "textIds": row["TextIds"],
            "descriptionTemplate": description_row["Content"],
            "note": row["Note"], "maxAcquisitions": row["MaxAcquisitions"],
            "previews": preview_descriptions(row, description, effects, weapons),
            "handlers": [effects[ref]["Handler"] for ref in row["EffectIds"]],
        }
        links.extend(make_link("requires", parent, ident) for parent in row["RequiredCardIds"])
        exclusions.update(tuple(sorted((ident, other))) for other in row["ExcludedCardIds"])
    links.extend(make_link("excludes", a, b) for a, b in sorted(exclusions))
    return {
        "fingerprint": digest([rows, used_texts, effects, weapons]),
        "sources": EXPORTS, "cards": cards, "links": links, "details": details,
    }


def relationship_fields(graph):
    fields = {
        card["id"]: {"RequiredCardIds": [], "ExcludedCardIds": []}
        for card in graph["cards"]
    }
    for link in graph["links"]:
        source, target = link["source"], link["target"]
        if link["kind"] == "requires":
            fields[target]["RequiredCardIds"].append(source)
        else:
            fields[target]["ExcludedCardIds"].append(source)
            fields[source]["ExcludedCardIds"].append(target)
    for row in fields.values():
        for ids in row.values():
            ids.sort()
    return fields


def initial_graph(catalog):
    fields = relationship_fields(catalog)
    depths = {}

    def depth(ident):
        if ident not in depths:
            parents = fields[ident]["RequiredCardIds"]
            depths[ident] = max((depth(parent) + 1 for parent in parents), default=0)
        return depths[ident]

    cards = []
    for seat in SEATS.values():
        columns = {}
        for card in catalog["cards"]:
            if card["seat"] == seat:
                columns.setdefault(depth(card["id"]), []).append(card)
        tallest = max(map(len, columns.values()), default=0)
        for column, members in sorted(columns.items()):
            for index, card in enumerate(members):
                y = 36 + (index + (tallest - len(members)) / 2) * 110
                cards.append({**card, "x": 36 + column * 266, "y": y})
    return {"cards": cards, "links": catalog["links"]}


def validate_graph(graph, base):
    if not isinstance(graph, dict):
        raise ValueError("缺少卡牌节点图。")
    cards, links = graph.get("cards"), graph.get("links")
    if not isinstance(cards, list) or not isinstance(links, list):
        raise ValueError("节点或连线数据无效。")
    if len(cards) > 512 or len(links) > 4096:
        raise ValueError("草稿超过 512 张卡或 4096 条连线。")
    existing = {card["id"]: card for card in base["cards"]}
    by_id = {}
    for card in cards:
        if not isinstance(card, dict):
            raise ValueError("卡牌数据无效。")
        ident, seat = card.get("id"), card.get("seat")
        title, description = card.get("title"), card.get("description")
        if not isinstance(ident, str) or ident in by_id:
            raise ValueError("卡牌 ID 缺失或重复。")
        if seat not in SEATS.values() or (
            ident in existing and existing[ident]["seat"] != seat
        ):
            raise ValueError("卡牌席位无效。")
        if ident not in existing and not re.fullmatch(r"draft-[0-9a-f-]{36}", ident):
            raise ValueError(f"未知卡牌 ID {ident}。")
        if not isinstance(title, str) or not 0 < len(title.strip()) <= 80 or "\n" in title or "\r" in title:
            raise ValueError("卡名须为 1–80 个字符的单行文字。")
        if not isinstance(description, str) or len(description) > 2000:
            raise ValueError("描述最多 2000 个字符。")
        x, y = card.get("x"), card.get("y")
        if any(type(v) not in (int, float) or not -100000 <= v <= 100000 for v in (x, y)):
            raise ValueError("节点位置超出画布范围。")
        by_id[ident] = {
            "id": ident, "seat": seat, "title": title.strip(),
            "description": description.strip(), "x": x, "y": y,
        }

    normalized, seen = [], set()
    for link in links:
        if not isinstance(link, dict) or link.get("kind") not in ("requires", "excludes"):
            raise ValueError("未知连线类型。")
        source, target = link.get("source"), link.get("target")
        if any(not isinstance(ident, str) or ident not in by_id for ident in (source, target)):
            raise ValueError("连线引用了不存在的卡牌。")
        entry = make_link(link["kind"], source, target)
        key = tuple(entry.values())
        if key in seen:
            raise ValueError("存在重复连线。")
        seen.add(key)
        normalized.append(entry)
    result = {"cards": list(by_id.values()), "links": normalized}
    fields = relationship_fields(result)
    rows = {
        ident: {"Id": ident, "Type": card["seat"], **fields[ident]}
        for ident, card in by_id.items()
    }

    def fail(row, message):
        raise ValueError(f"{by_id[row['Id']]['title']}：{message}")

    validate_relationships(rows, fail)
    return result


def describe_changes(base, graph):
    before = {card["id"]: card for card in base["cards"]}
    after = {card["id"]: card for card in graph["cards"]}
    old_links, new_links = relationship_fields(base), relationship_fields(graph)
    added = [
        {**{key: value for key, value in card.items() if key not in ("x", "y")}, **new_links[ident]}
        for ident, card in after.items() if ident not in before
    ]
    removed = [card for ident, card in before.items() if ident not in after]
    updated = []
    for ident in before.keys() & after.keys():
        old, new = before[ident], after[ident]
        fields = {}
        for key in ("title", "description"):
            if old[key] != new[key]:
                fields[key] = {"before": old[key], "after": new[key]}
        for key in ("RequiredCardIds", "ExcludedCardIds"):
            if old_links[ident][key] != new_links[ident][key]:
                fields[key] = {"before": old_links[ident][key], "after": new_links[ident][key]}
        if fields:
            updated.append({
                "id": ident, "seat": new["seat"], "title": new["title"],
                "textIds": base["details"][ident]["textIds"], "fields": fields,
            })
    return {"added": added, "removed": removed, "updated": sorted(updated, key=lambda card: card["id"])}
