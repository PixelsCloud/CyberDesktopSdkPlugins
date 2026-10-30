"""Production registration for the revised tall displays and overhead ponds."""


def apply_review(order, result, rect):
    regions = result["regions"]
    if order in (15, 16):
        # Production glass is now blank; old sample-value masks would leave
        # conspicuous rectangular patches on the authored display surface.
        result["screens"] = []
    if order == 15:
        regions["city"] = rect([0.29, 0.14, 0.22, 0.18])
    if order == 10:
        regions.update(
            title=rect([0.23, 0.12, 0.38, 0.12]),
            previous=rect([0.14, 0.135, 0.045, 0.09]),
            next=rect([0.65, 0.135, 0.045, 0.09]),
            grid=rect([0.14, 0.26, 0.55, 0.62]),
        )
        result["screens"] = []
        result["styles"]["title"].update(size=17, minimumSize=14)
    if order == 18:
        for i, area in enumerate(
            [[0.13, 0.19, 0.22, 0.65], [0.415, 0.19, 0.15, 0.65], [0.615, 0.19, 0.15, 0.65], [0.815, 0.19, 0.15, 0.65]]
        ):
            regions[f"gauge{i}"] = rect(area)
        result["screens"] = []
        # Extend each empty glass pane equally. Metal endcaps keep their shape,
        # while both outer edges retain the Pixels inset on wider brick grids.
        left, _, width, _ = result["layers"][0]["destination"]
        result["horizontal"] = dict(
            left=left,
            right=left + width,
            cuts=[rect([x, 0, 0, 0])[0] for x in [0.24, 0.49, 0.69, 0.89]],
            inset=7,
            preserveEndcaps=True,
        )
    if order == 27:
        for key, x in [("previous", 0.674), ("toggle", 0.771), ("next", 0.867)]:
            regions[key] = rect([x, 0.785, 0.081, 0.152])
    if order == 67:
        regions["title"] = rect([0.46, 0.25, 0.36, 0.47])
        result["screens"].append(regions["title"])
        result["styles"]["title"].update(size=15, minimumSize=12)
    if order in (68, 70, 71):
        water, info = {
            68: ([0.115, 0.17, 0.64, 0.66], [0.82, 0.40, 0.15, 0.25]),
            70: ([0.12, 0.12, 0.76, 0.72], [0.35, 0.89, 0.30, 0.065]),
            71: ([0.11, 0.16, 0.81, 0.69], [0.82, 0.135, 0.15, 0.12]),
        }[order]
        regions.update(water=rect(water), info=rect(info), care=rect(info))
        x, y, w, h = regions["water"]
        cut = min(w, h) * 0.19
        result["waterMask"] = dict(
            outline=[
                [x + cut, y],
                [x + w - cut, y],
                [x + w, y + cut],
                [x + w, y + h - cut],
                [x + w - cut, y + h],
                [x + cut, y + h],
                [x, y + h - cut],
                [x, y + cut],
            ],
            holes=[],
        )
        result["screens"] = []
    if order in (5, 6, 39, 40):
        result["defaults"]["gain"] = 3.5
        if order == 6:
            # The small round display is a live spectrum, not a painted oscilloscope.
            regions["signal"] = rect([0.49, 0.35, 0.12, 0.30])
            result["screens"].append(regions["signal"])
        if order == 5:
            regions["signal"] = rect([0.53, 0.38, 0.36, 0.12])
            result["screens"].append(regions["signal"])
        if order in (39, 40):
            for key in ("previous", "toggle", "next"):
                regions.pop(key, None)
            if order == 39:
                # The revised artwork already replaces the keys with one structural rail.
                regions["meters"] = rect([0.875, 0.72, 0.09, 0.12])
                result["screens"].append(regions["meters"])
            else:
                result["screens"].append(rect([0.63, 0.42, 0.10, 0.23]))
