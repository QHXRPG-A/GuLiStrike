---
name: excelize-cli
description: >
  基于 excelize (github.com/qax-os/excelize) 的 xlsx 命令行工具，用于创建、读取、编辑 Excel (.xlsx) 文件。
  当任务涉及 .xlsx 文件——查看/修改数据表、GuLiStrike 数据管线的 Excel 源表、CSV↔Excel 转换、
  批量写单元格、公式、样式（加粗/背景色/边框/对齐）、合并单元格、列宽行高、增删行列——时使用本技能。
  Use whenever an .xlsx file must be created, read, or edited; prefer this CLI over ad-hoc Python/openpyxl scripts.
  二进制: D:\UE_5.7\excelize-cli\bin\xlsx.exe（已在用户 PATH）。
---

# excelize-cli（xlsx 命令行工具）

`xlsx` 是用 Go 编写、直接链接本地 excelize 源码的单文件 CLI，无 Python 依赖、逐单元格操作精确、输出可机器解析（TSV/CSV/JSON）。

- 二进制：`D:\UE_5.7\excelize-cli\bin\xlsx.exe`（已加入用户 PATH；当前会话若未生效，用全路径）
- CLI 源码：`D:\UE_5.7\excelize-cli\main.go`（单文件，所有命令在此）
- excelize 库源码：`D:\UE_5.7\excelize`（go.mod 用 `replace` 指向它，跟踪 master）
- Go 工具链：`D:\UE_5.7\go\bin`（同样在用户 PATH）

## 命令速查

```
xlsx new <out.xlsx> [--sheet 名]                    新建工作簿（默认建 Sheet1）
xlsx info <file.xlsx>                              工作簿信息 JSON（各 sheet 实测行列数、used range）
xlsx sheets <file.xlsx> [--json]                   列出 sheet 名
xlsx read <file.xlsx> [--sheet S] [--range A1:C10] 读单元格
      [--format tsv|csv|json] [--raw] [--skip N] [--limit N]
xlsx write <file.xlsx> [--sheet S]                 写入（二选一）
      --cell A1 --value V [--type auto|string|int|float|bool|formula]
      --data-file cells.json                       批量：[{"cell":"A1","value":..,"type":..},..]
xlsx add-sheet <file> --name N                     加 sheet（并设为活动）
xlsx del-sheet <file> --name N                     删 sheet（活动 sheet 会先自动切换）
xlsx rename-sheet <file> --from A --to B           重命名
xlsx insert-rows <file> --sheet S --at 3 --count 2 在第 3 行前插入 2 行
xlsx delete-rows <file> --sheet S --at 3 --count 2 删除第 3 行起的 2 行
xlsx insert-cols <file> --sheet S --col B --count 1
xlsx delete-cols <file> --sheet S --col B --count 1
xlsx col-width <file> --sheet S --col A [--to F] --width 18
xlsx row-height <file> --sheet S --row 1 --height 24
xlsx style <file> --sheet S --range A1:F1 [样式flags]
xlsx merge-cell <file> --sheet S --range A1:B2
xlsx import-csv <in.csv> -o out.xlsx [--sheet S] [--delimiter ,|;|tab]
xlsx help / xlsx version
```

`--sheet` 省略时作用于**活动 sheet**（通常是你刚 add-sheet 或最后操作的）。flag 和文件路径顺序随意。所有编辑命令**原地保存**——重要文件先备份。

## style 的样式 flags

`--bold --italic --size 14 --font-color FF0000 --bg FFFF00 --halign left|center|right --valign top|middle|bottom --wrap --border`（颜色是 RRGGBB 十六进制，无 #；--border 是四边细黑框）

## 常用配方

读前 5 行看结构（TSV 直接可读）：
```bash
xlsx read Data/Items.xlsx --limit 5
```

读整表交给程序处理，用 JSON：
```bash
xlsx read Data/Items.xlsx --format json
```

批量写入（推荐：一次保存，避免多次打开保存；--data-file 是 JSON 数组）：
```bash
printf '[{"cell":"A1","value":"HP"},{"cell":"B2","value":42},{"cell":"C2","value":"=SUM(B2:B3)","type":"formula"}]' > cells.json
xlsx write Data/Items.xlsx --data-file cells.json
```

表头美化 + 首列加宽：
```bash
xlsx style Data/Items.xlsx --range A1:F1 --bold --bg D9E1F2 --halign center --border
xlsx col-width Data/Items.xlsx --col A --width 20
```

CSV 转 xlsx（含中文表头）：
```bash
xlsx import-csv items.csv -o Items.xlsx --sheet Data
```

## 类型与公式行为（重要）

- `--type auto`（默认）按内容推断：`true/false`→布尔，整数→int，小数→float，`=`开头→公式，其余→字符串。要强制存文本（如 `"42"`、`"001"`）用 `--type string`。
- **写入公式不会立即计算**：excelize 只存公式不执行计算，`xlsx read` 读到的公式单元格是空串，打开 Excel/WPS 后才会显示结果。不要因为读到空就以为写入失败。
- `read` 默认返回**格式化后的显示值**（日期会变成显示格式）；要原始存储值加 `--raw`。
- `info` 的 rows/cols 是按实际单元格内容测出来的，比 XML 里的 dimension 元素可靠。

## 与 GuLiStrike 数据管线的关系

本项目的 C++ 数据管线用 Excel 源表（前 3 行是元数据行，第 4 行起是数据）。改表时的安全做法：
1. `xlsx info` 确认 sheet 与行列规模；
2. `xlsx read --skip 3 --limit 10` 跳过元数据行核对数据；
3. 编辑用 `--data-file` 批量写或 insert/delete-rows 调整结构，元数据行（1-3 行）通常不要动。

## 维护（改代码后重建）

```bash
export PATH="/d/UE_5.7/go/bin:$PATH"
cd /d/UE_5.7/excelize-cli && go build -o bin/xlsx.exe .
```

excelize 源码更新（`git -c http.proxy=http://127.0.0.1:7897 pull` 于 D:\UE_5.7\excelize）后同样重建即可。改了 excelize API 签名导致编译失败时，先在 `D:\UE_5.7\excelize` 里 grep 确认新签名再改 main.go。
