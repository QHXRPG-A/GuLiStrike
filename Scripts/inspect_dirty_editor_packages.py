import json

import unreal


OUTPUT_PATH = "D:/UE5.7/test1/Progress/CommanderDirtyPackagesBeforeRestart.json"


def package_names(packages):
    return sorted(package.get_path_name() for package in packages if package)


result = {"content": [], "maps": [], "errors": []}
try:
    result["content"] = package_names(
        unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    )
except Exception as error:
    result["errors"].append("content: " + str(error))
try:
    result["maps"] = package_names(
        unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    )
except Exception as error:
    result["errors"].append("maps: " + str(error))

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
