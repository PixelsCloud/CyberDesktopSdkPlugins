"""Record reviewed native previews and advance only a verified, published item."""

import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
DESIGN = ROOT / "design/steampunk-desk-suite"


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def update(mode, folder, notes):
    task = json.loads((DESIGN / "implementation-task.json").read_text(encoding="utf-8"))
    entry = next(item for item in task["entries"] if item["folder"] == folder)
    logs = ROOT / "build/relwithdebinfo/Testing/steampunk-desk" / folder
    result = json.loads((logs / "result.json").read_text(encoding="utf-8-sig"))
    assert result["id"] == entry["id"] and result["status"] == "passed"
    if mode == "review":
        assert notes, "Record what was inspected"
        preview = ROOT / "plugins/steampunk-desk/packages" / folder / "preview.png"
        result["visualReview"] = "passed"
        result["visualEvidence"] = {"preview": preview.relative_to(ROOT).as_posix(),
                                    "sha256": hashlib.sha256(preview.read_bytes()).hexdigest(),
                                    "notes": notes, "method": "Inspect actual native output against approved design"}
        write(logs / "result.json", result)
        print(f"PASS {folder} native visual review recorded")
        return
    assert mode == "finish"
    assert result["published"] and result["visualReview"] == "passed"
    assert all(item["state"] == "passed" for item in task["entries"] if item["order"] < entry["order"])
    package = ROOT / "build/dist/plugin-packages" / (entry["id"] + ".cdpkg")
    assert package.is_file()
    entry["state"] = "passed"
    entry["status"] = "complete"
    entry["gates"] = dict.fromkeys(entry["gates"], True)
    entry["evidence"] = {"result": (logs / "result.json").relative_to(ROOT).as_posix(),
                         "preview": result["visualEvidence"]["preview"],
                         "package": package.relative_to(ROOT).as_posix(),
                         "packageSha256": hashlib.sha256(package.read_bytes()).hexdigest(),
                         "nativeDpiRenders": result["nativeDpiRenders"], "visualReview": "passed", "published": True}
    task["completed"] = sum(item["state"] == "passed" for item in task["entries"])
    next_item = next((item for item in task["entries"] if item["state"] != "passed"), None)
    if next_item:
        task["currentOrder"] = next_item["order"]
        next_item["state"] = "in_progress"
    else:
        task["currentOrder"] = None
        task["status"] = "complete"
    write(DESIGN / "implementation-task.json", task)
    lines = ["Steamworks 插件逐项实施任务", "", f"进度：{task['completed']} / 45 个通过并发布。统一标签：蒸汽朋克。",
             "顺序：实现一个 → 专项测试 → 原生截图对照 → SDK/打包校验 → 发布 → 再做下一个。",
             "编译固定 build/relwithdebinfo，最多 10 个逻辑核心 / 10 个并行作业。发布仅 build/dist。",
             "原有插件和用户数据保留；完整验收要求、每项证据见 implementation-task.json。", ""]
    states = {"passed": "已验证并发布", "in_progress": "进行中", "pending": "待做"}
    lines += [f"{item['order']:02d}. [{states[item['state']]}] {item['name']} → {item['id']}" for item in task["entries"]]
    (DESIGN / "implementation-task.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"PASS {folder}; sequential task now {task['completed']}/45")


if __name__ == "__main__":
    update(sys.argv[1], sys.argv[2], " ".join(sys.argv[3:]))
