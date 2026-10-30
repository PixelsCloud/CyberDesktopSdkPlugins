"""Native semantic registration against each approved object's visible bounds.

Coordinates are fractions of the individual object's alpha bounds. They are not
borrowed from another skin. PNG pixels remain intact; moving parts use regional
decode, with their original transforms recorded separately from screen fields.
"""

from ReviewLayout import apply_review

REGIONS = {
    3: {},
    4: {},
    5: {"readout0": [0.53, 0.16, 0.37, 0.18], "plot": [0.53, 0.16, 0.37, 0.18]},
    6: {"readout0": [0.68, 0.35, 0.29, 0.18], "plot": [0.68, 0.35, 0.29, 0.18]},
    7: {"world": [0.16, 0.12, 0.65, 0.60]},
    8: {"world": [0.10, 0.15, 0.61, 0.51]},
    9: {"world": [0.12, 0.10, 0.72, 0.58], "readout0": [0.23, 0.31, 0.48, 0.14]},
    10: {
        "title": [0.16, 0.12, 0.45, 0.12],
        "grid": [0.18, 0.27, 0.50, 0.51],
        "previous": [0.13, 0.12, 0.045, 0.12],
        "next": [0.62, 0.12, 0.045, 0.12],
    },
    11: {"day": [0.08, 0.32, 0.26, 0.39], "title": [0.59, 0.25, 0.32, 0.20], "weekday": [0.59, 0.64, 0.32, 0.15]},
    12: {
        "title": [0.28, 0.12, 0.43, 0.07],
        "day": [0.25, 0.22, 0.49, 0.23],
        "weekday": [0.26, 0.49, 0.46, 0.08],
        "lunar": [0.60, 0.73, 0.24, 0.13],
    },
    13: {
        "title": [0.16, 0.11, 0.68, 0.09],
        "weekdays": [0.12, 0.23, 0.76, 0.05],
        "grid": [0.12, 0.29, 0.76, 0.50],
        "previous": [0.06, 0.11, 0.08, 0.09],
        "next": [0.84, 0.11, 0.08, 0.09],
        "festival": [0.18, 0.86, 0.65, 0.055],
    },
    14: {"icon": [0.05, 0.14, 0.32, 0.61], "temperature": [0.46, 0.30, 0.28, 0.28], "city": [0.47, 0.63, 0.22, 0.10]},
    15: {
        "icon": [0.04, 0.20, 0.22, 0.60],
        "city": [0.29, 0.17, 0.22, 0.10],
        "temperature": [0.30, 0.32, 0.29, 0.39],
        "humidity": [0.68, 0.27, 0.14, 0.17],
        "wind": [0.67, 0.65, 0.19, 0.17],
    },
    16: {
        "city": [0.17, 0.12, 0.23, 0.07],
        "temperature": [0.15, 0.22, 0.30, 0.25],
        "icon": [0.43, 0.13, 0.34, 0.45],
        "humidity": [0.82, 0.22, 0.12, 0.10],
        "wind": [0.81, 0.47, 0.14, 0.10],
        "forecast0": [0.10, 0.76, 0.23, 0.18],
        "forecast1": [0.41, 0.76, 0.23, 0.18],
        "forecast2": [0.72, 0.76, 0.23, 0.18],
    },
    17: {"gauge0": [0.59, 0.23, 0.24, 0.51], "quality": [0.63, 0.76, 0.19, 0.10]},
    18: {"gauge0": [0.15, 0.23, 0.22, 0.52], "gauge1": [0.57, 0.22, 0.10, 0.53], "gauge2": [0.73, 0.22, 0.10, 0.53]},
    19: {"gauge0": [0.16, 0.25, 0.23, 0.42], "gauge1": [0.62, 0.26, 0.08, 0.43], "gauge2": [0.76, 0.26, 0.08, 0.43]},
    20: {"gauge0": [0.17, 0.24, 0.53, 0.47]},
    21: {"gauge0": [0.09, 0.20, 0.37, 0.60], "gauge1": [0.70, 0.28, 0.23, 0.48]},
    22: {
        "gauge0": [0.07, 0.27, 0.14, 0.49],
        "gauge1": [0.32, 0.27, 0.13, 0.49],
        "gauge2": [0.57, 0.27, 0.13, 0.49],
        "gauge3": [0.81, 0.27, 0.14, 0.49],
    },
    23: {
        "gauge0": [0.21, 0.20, 0.38, 0.30],
        "gauge1": [0.75, 0.14, 0.12, 0.32],
        "gauge2": [0.15, 0.76, 0.32, 0.13],
        "gauge3": [0.58, 0.76, 0.25, 0.13],
    },
    24: {
        "down": [0.18, 0.29, 0.20, 0.28],
        "up": [0.69, 0.29, 0.20, 0.28],
        "downHistory": [0.15, 0.65, 0.26, 0.13],
        "upHistory": [0.67, 0.65, 0.27, 0.13],
    },
    25: {"usage": [0.26, 0.45, 0.55, 0.11], "gauge": [0.37, 0.59, 0.30, 0.23], "free": [0.27, 0.84, 0.49, 0.07]},
    26: {"keyboard": [0.25, 0.32, 0.23, 0.22], "mouse": [0.74, 0.32, 0.17, 0.22]},
    27: {
        "photo": [0.10, 0.14, 0.56, 0.68],
        "counter": [0.70, 0.45, 0.25, 0.20],
        "previous": [0.67, 0.73, 0.10, 0.18],
        "toggle": [0.79, 0.73, 0.10, 0.18],
        "next": [0.90, 0.73, 0.08, 0.18],
    },
    28: {
        "cover": [0.04, 0.22, 0.18, 0.55],
        "title": [0.28, 0.27, 0.35, 0.16],
        "progress": [0.29, 0.72, 0.35, 0.025],
        "previous": [0.79, 0.35, 0.055, 0.32],
        "toggle": [0.86, 0.35, 0.055, 0.32],
        "next": [0.93, 0.35, 0.055, 0.32],
    },
    29: {
        "cover": [0.12, 0.09, 0.67, 0.40],
        "title": [0.13, 0.56, 0.66, 0.07],
        "progress": [0.15, 0.70, 0.61, 0.015],
        "previous": [0.11, 0.81, 0.23, 0.14],
        "toggle": [0.39, 0.81, 0.23, 0.14],
        "next": [0.69, 0.81, 0.23, 0.14],
    },
    30: {
        "cover": [0.08, 0.17, 0.34, 0.68],
        "title": [0.50, 0.28, 0.42, 0.12],
        "progress": [0.57, 0.64, 0.34, 0.02],
        "previous": [0.51, 0.75, 0.14, 0.17],
        "toggle": [0.68, 0.75, 0.14, 0.17],
        "next": [0.86, 0.75, 0.10, 0.17],
    },
    31: {"plot": [0.15, 0.37, 0.64, 0.34], "clock": [0.85, 0.32, 0.12, 0.22]},
    32: {"plot": [0.15, 0.30, 0.75, 0.43]},
    33: {"plot": [0.28, 0.30, 0.55, 0.40], "clock": [0.04, 0.35, 0.16, 0.25]},
    34: {"plot": [0.28, 0.34, 0.63, 0.39], "clock": [0.05, 0.31, 0.16, 0.31]},
    35: {"plot": [0.03, 0.22, 0.94, 0.62]},
    36: {"plot": [0.055, 0.24, 0.91, 0.61]},
    37: {"plot": [0.15, 0.28, 0.62, 0.43], "meter": [0.85, 0.34, 0.10, 0.21]},
    38: {"plot": [0.20, 0.14, 0.57, 0.58]},
    39: {"plot": [0.14, 0.25, 0.25, 0.43], "world": [0.10, 0.20, 0.77, 0.57]},
    40: {"plot": [0.09, 0.23, 0.21, 0.53], "world": [0.07, 0.17, 0.72, 0.68]},
    41: {},
    42: {"world": [0.14, 0.30, 0.74, 0.43]},
    43: {"glass": [0.28, 0.12, 0.34, 0.49], "title": [0.18, 0.72, 0.59, 0.13], "toggle": [0.15, 0.68, 0.70, 0.20]},
    44: {
        "glass": [0.11, 0.22, 0.15, 0.48],
        "title": [0.43, 0.35, 0.47, 0.27],
        "progress": [0.43, 0.71, 0.46, 0.025],
        "toggle": [0.91, 0.67, 0.08, 0.16],
        "reset": [0.11, 0.22, 0.15, 0.48],
    },
    45: {
        "title": [0.23, 0.43, 0.56, 0.14],
        "phase": [0.36, 0.34, 0.31, 0.06],
        "progress": [0.30, 0.67, 0.45, 0.02],
        "toggle": [0.23, 0.81, 0.22, 0.13],
        "reset": [0.61, 0.81, 0.24, 0.13],
    },
    46: {
        "title": [0.11, 0.31, 0.51, 0.38],
        "phase": [0.13, 0.18, 0.23, 0.08],
        "progress": [0.13, 0.76, 0.48, 0.025],
        "toggle": [0.77, 0.22, 0.19, 0.32],
        "reset": [0.77, 0.60, 0.19, 0.29],
    },
    47: {
        "down": [0.13, 0.33, 0.27, 0.25],
        "up": [0.69, 0.33, 0.25, 0.25],
        "downHistory": [0.12, 0.69, 0.26, 0.10],
        "upHistory": [0.68, 0.69, 0.24, 0.10],
    },
    48: {"title": [0.065, 0.37, 0.17, 0.20], "free": [0.43, 0.35, 0.42, 0.27], "progress": [0.44, 0.76, 0.48, 0.025]},
    49: {"plot": [0.21, 0.26, 0.75, 0.44]},
    50: {"plot": [0.035, 0.22, 0.94, 0.58]},
    51: {},
    52: {"plot": [0.14, 0.12, 0.65, 0.61], "footer0": [0.34, 0.79, 0.26, 0.10]},
    53: {"plot": [0.08, 0.22, 0.23, 0.50], "readout0": [0.52, 0.41, 0.34, 0.25], "footer0": [0.88, 0.50, 0.06, 0.16]},
    54: {"plot": [0.07, 0.15, 0.26, 0.67], "readout0": [0.37, 0.33, 0.43, 0.30], "footer0": [0.86, 0.43, 0.08, 0.14]},
    55: {"plot": [0.12, 0.07, 0.57, 0.48], "footer0": [0.66, 0.72, 0.25, 0.14]},
    56: {"plot": [0.19, 0.23, 0.57, 0.56], "readout0": [0.20, 0.24, 0.56, 0.22], "readout1": [0.20, 0.51, 0.56, 0.22]},
    57: {"plot": [0.14, 0.30, 0.49, 0.39], "readout0": [0.14, 0.30, 0.49, 0.39], "footer0": [0.68, 0.46, 0.08, 0.20]},
    58: {"plot": [0.10, 0.33, 0.48, 0.32], "readout0": [0.10, 0.33, 0.48, 0.32], "footer0": [0.77, 0.38, 0.18, 0.26]},
    59: {
        "plot": [0.13, 0.12, 0.46, 0.70],
        "readout0": [0.13, 0.12, 0.46, 0.22],
        "readout1": [0.13, 0.53, 0.46, 0.23],
        "readout2": [0.79, 0.65, 0.10, 0.07],
        "footer0": [0.28, 0.89, 0.39, 0.06],
    },
    60: {
        "plot": [0.23, 0.30, 0.63, 0.48],
        "readout0": [0.23, 0.30, 0.18, 0.48],
        "readout1": [0.48, 0.30, 0.18, 0.48],
        "readout2": [0.72, 0.30, 0.17, 0.48],
    },
    61: {
        "plot": [0.19, 0.09, 0.57, 0.73],
        "readout0": [0.19, 0.09, 0.57, 0.22],
        "readout1": [0.19, 0.60, 0.57, 0.21],
        "footer0": [0.27, 0.91, 0.50, 0.04],
    },
    62: {
        "plot": [0.25, 0.31, 0.71, 0.37],
        "readout0": [0.25, 0.31, 0.17, 0.37],
        "readout1": [0.52, 0.31, 0.17, 0.37],
        "readout2": [0.78, 0.31, 0.17, 0.37],
        "footer0": [0.75, 0.83, 0.18, 0.08],
    },
    63: {"plot": [0.14, 0.28, 0.53, 0.42]},
    64: {"plot": [0.11, 0.34, 0.71, 0.42]},
    65: {"world": [0.19, 0.28, 0.67, 0.43]},
    66: {
        "count": [0.17, 0.21, 0.30, 0.41],
        "unit": [0.18, 0.70, 0.18, 0.10],
        "title": [0.77, 0.22, 0.18, 0.10],
        "target": [0.75, 0.37, 0.20, 0.08],
    },
    67: {
        "count": [0.11, 0.32, 0.12, 0.30],
        "unit": [0.26, 0.36, 0.10, 0.17],
        "title": [0.51, 0.31, 0.28, 0.16],
        "target": [0.89, 0.29, 0.07, 0.43],
    },
    68: {"water": [0.29, 0.27, 0.48, 0.54], "care": [0.84, 0.38, 0.13, 0.26], "info": [0.84, 0.38, 0.13, 0.26]},
    69: {"pot": [0.10, 0.64, 0.30, 0.23], "care": [0.44, 0.65, 0.45, 0.21], "info": [0.44, 0.65, 0.45, 0.21]},
    70: {"water": [0.15, 0.28, 0.59, 0.53], "care": [0.73, 0.83, 0.18, 0.08], "info": [0.73, 0.83, 0.18, 0.08]},
    71: {"water": [0.20, 0.29, 0.40, 0.50], "care": [0.045, 0.30, 0.08, 0.13], "info": [0.045, 0.30, 0.08, 0.13]},
    72: {"pot": [0.26, 0.50, 0.15, 0.32], "care": [0.59, 0.50, 0.30, 0.25], "info": [0.59, 0.50, 0.30, 0.25]},
    73: {"pot": [0.07, 0.45, 0.16, 0.39], "care": [0.72, 0.69, 0.23, 0.12], "info": [0.72, 0.69, 0.23, 0.12]},
    74: {
        "play": [0.66, 0.69, 0.12, 0.15],
        "master": [0.53, 0.59, 0.40, 0.025],
        "source": [0.52, 0.69, 0.12, 0.15],
        "page": [0.81, 0.69, 0.12, 0.15],
        "row0": [0.53, 0.27, 0.38, 0.17],
        "toggle0": [0.66, 0.69, 0.12, 0.15],
        "slider0": [0.53, 0.59, 0.40, 0.025],
        "timer": [0.52, 0.69, 0.12, 0.15],
    },
    75: {
        "play": [0.42, 0.35, 0.22, 0.19],
        "master": [0.73, 0.61, 0.22, 0.02],
        "timer": [0.77, 0.35, 0.17, 0.15],
        "page": [0.91, 0.18, 0.06, 0.12],
        "row0": [0.44, 0.28, 0.20, 0.08],
        "row1": [0.44, 0.40, 0.20, 0.08],
        "row2": [0.44, 0.52, 0.20, 0.08],
        "row3": [0.44, 0.64, 0.20, 0.08],
        "toggle0": [0.41, 0.78, 0.12, 0.15],
        "toggle1": [0.55, 0.78, 0.12, 0.15],
        "toggle2": [0.70, 0.78, 0.12, 0.15],
        "toggle3": [0.84, 0.78, 0.12, 0.15],
        "slider0": [0.68, 0.29, 0.25, 0.018],
        "slider1": [0.68, 0.41, 0.25, 0.018],
        "slider2": [0.68, 0.53, 0.25, 0.018],
        "slider3": [0.68, 0.65, 0.25, 0.018],
    },
}

# Whole display interiors erase baked sample values, icons and graphs. Small
# display fields otherwise clear only their own registered rectangle.
PANELS = {
    5: [[0.52, 0.15, 0.39, 0.20]],
    6: [[0.67, 0.34, 0.31, 0.20]],
    7: [[0.20, 0.13, 0.60, 0.56]],
    8: [[0.11, 0.16, 0.60, 0.50]],
    9: [[0.14, 0.11, 0.67, 0.56]],
    10: [[0.17, 0.11, 0.51, 0.69]],
    11: [[0.08, 0.27, 0.27, 0.48], [0.59, 0.23, 0.33, 0.22], [0.59, 0.63, 0.33, 0.18]],
    12: [[0.24, 0.11, 0.50, 0.48], [0.60, 0.72, 0.25, 0.16]],
    13: [[0.12, 0.10, 0.76, 0.69], [0.16, 0.85, 0.69, 0.07]],
    14: [[0.46, 0.25, 0.30, 0.50]],
    15: [[0.29, 0.15, 0.31, 0.65], [0.67, 0.22, 0.20, 0.61]],
    16: [[0.15, 0.11, 0.28, 0.44], [0.81, 0.21, 0.15, 0.38], [0.10, 0.75, 0.84, 0.20]],
    17: [[0.59, 0.22, 0.25, 0.57]],
    18: [[0.15, 0.22, 0.22, 0.54], [0.57, 0.22, 0.10, 0.54], [0.73, 0.22, 0.10, 0.54], [0.89, 0.22, 0.10, 0.54]],
    19: [[0.16, 0.25, 0.40, 0.43], [0.62, 0.26, 0.08, 0.43], [0.76, 0.26, 0.08, 0.43], [0.90, 0.26, 0.07, 0.43]],
    20: [[0.17, 0.23, 0.54, 0.49]],
    21: [[0.09, 0.19, 0.37, 0.62], [0.70, 0.27, 0.24, 0.51]],
    22: [[0.07, 0.26, 0.14, 0.51], [0.32, 0.26, 0.13, 0.51], [0.57, 0.26, 0.13, 0.51], [0.81, 0.26, 0.14, 0.51]],
    23: [[0.21, 0.20, 0.38, 0.30], [0.75, 0.14, 0.12, 0.40], [0.15, 0.76, 0.32, 0.16], [0.58, 0.76, 0.25, 0.16]],
    24: [[0.15, 0.21, 0.27, 0.59], [0.67, 0.21, 0.27, 0.59]],
    25: [[0.25, 0.43, 0.56, 0.49]],
    26: [[0.25, 0.31, 0.23, 0.23], [0.74, 0.31, 0.17, 0.23]],
    27: [[0.69, 0.44, 0.27, 0.22]],
    28: [[0.28, 0.26, 0.36, 0.50]],
    29: [[0.13, 0.55, 0.67, 0.18]],
    30: [[0.50, 0.28, 0.44, 0.39]],
    31: [[0.15, 0.35, 0.64, 0.38], [0.85, 0.30, 0.12, 0.25]],
    32: [[0.15, 0.29, 0.75, 0.45]],
    33: [[0.28, 0.28, 0.55, 0.44], [0.04, 0.34, 0.17, 0.29]],
    34: [[0.28, 0.33, 0.63, 0.41], [0.05, 0.30, 0.16, 0.34]],
    35: [[0.03, 0.20, 0.94, 0.65]],
    36: [[0.055, 0.22, 0.91, 0.64]],
    37: [[0.15, 0.27, 0.62, 0.45], [0.85, 0.33, 0.11, 0.25]],
    38: [[0.20, 0.14, 0.57, 0.58]],
    39: [[0.12, 0.22, 0.30, 0.48], [0.61, 0.37, 0.18, 0.31], [0.87, 0.39, 0.10, 0.16]],
    40: [[0.08, 0.21, 0.23, 0.56], [0.58, 0.24, 0.20, 0.48], [0.86, 0.38, 0.12, 0.17]],
    42: [[0.14, 0.30, 0.74, 0.43]],
    43: [[0.18, 0.71, 0.59, 0.15]],
    44: [[0.43, 0.34, 0.48, 0.29]],
    45: [[0.23, 0.42, 0.57, 0.16]],
    46: [[0.11, 0.30, 0.52, 0.40]],
    47: [[0.12, 0.32, 0.28, 0.49], [0.68, 0.32, 0.27, 0.49]],
    48: [[0.43, 0.34, 0.52, 0.46]],
    49: [[0.21, 0.25, 0.75, 0.46]],
    50: [[0.035, 0.20, 0.94, 0.62]],
    52: [[0.14, 0.12, 0.65, 0.61], [0.34, 0.79, 0.26, 0.11]],
    53: [[0.08, 0.22, 0.23, 0.50], [0.51, 0.39, 0.44, 0.33]],
    54: [[0.07, 0.15, 0.26, 0.67], [0.36, 0.31, 0.45, 0.34], [0.85, 0.40, 0.10, 0.19]],
    55: [[0.12, 0.07, 0.57, 0.48], [0.66, 0.71, 0.25, 0.16]],
    56: [[0.19, 0.23, 0.57, 0.56]],
    57: [[0.14, 0.29, 0.64, 0.44]],
    58: [[0.10, 0.32, 0.48, 0.34], [0.76, 0.37, 0.19, 0.29]],
    59: [[0.13, 0.11, 0.46, 0.24], [0.13, 0.52, 0.46, 0.25], [0.78, 0.64, 0.12, 0.09], [0.27, 0.88, 0.41, 0.07]],
    60: [[0.23, 0.29, 0.18, 0.50], [0.48, 0.29, 0.18, 0.50], [0.72, 0.29, 0.17, 0.50]],
    61: [[0.19, 0.08, 0.57, 0.24], [0.19, 0.59, 0.57, 0.23], [0.27, 0.90, 0.50, 0.06]],
    62: [[0.25, 0.30, 0.17, 0.39], [0.52, 0.30, 0.17, 0.39], [0.78, 0.30, 0.17, 0.39], [0.75, 0.82, 0.18, 0.10]],
    63: [[0.13, 0.27, 0.55, 0.44]],
    64: [[0.10, 0.33, 0.73, 0.44]],
    65: [[0.19, 0.27, 0.67, 0.45]],
    66: [[0.17, 0.20, 0.31, 0.43], [0.77, 0.21, 0.18, 0.12], [0.75, 0.36, 0.20, 0.11]],
    67: [[0.10, 0.31, 0.13, 0.32], [0.51, 0.30, 0.28, 0.19], [0.88, 0.28, 0.09, 0.46]],
    69: [[0.44, 0.65, 0.45, 0.21]],
    72: [[0.59, 0.50, 0.30, 0.25]],
    73: [[0.72, 0.68, 0.23, 0.15]],
    74: [[0.53, 0.26, 0.40, 0.32]],
    75: [[0.43, 0.26, 0.51, 0.45]],
}


# Readouts are measured against the screen aperture, not the complete hardware.
# Six split-flap cells have independent centres; grouping pairs loses that alignment.
REGIONS[7]["world"] = [0.18, 0.097, 0.64, 0.60]
REGIONS[8]["world"] = [0.105, 0.18, 0.53, 0.47]
REGIONS[9].update(world=[0.185, 0.115, 0.59, 0.51], readout0=[0.265, 0.30, 0.43, 0.14])
REGIONS[53].update(
    plot=[0.095, 0.26, 0.20, 0.54], readout0=[0.52, 0.45, 0.32, 0.30], footer0=[0.885, 0.57, 0.075, 0.16]
)
PANELS[53] = [REGIONS[53]["plot"], [0.515, 0.44, 0.33, 0.32], [0.88, 0.45, 0.085, 0.30]]
REGIONS[54].update(
    plot=[0.065, 0.195, 0.27, 0.67], readout0=[0.375, 0.36, 0.425, 0.34], footer0=[0.85, 0.56, 0.095, 0.14]
)
PANELS[54] = [REGIONS[54]["plot"], [0.37, 0.35, 0.435, 0.36], [0.84, 0.40, 0.105, 0.31]]
REGIONS[58].update(plot=[0.105, 0.27, 0.49, 0.44], readout0=[0.105, 0.27, 0.49, 0.44], footer0=[0.78, 0.33, 0.17, 0.36])
PANELS[58] = [[0.10, 0.26, 0.50, 0.46], [0.77, 0.32, 0.18, 0.39]]
REGIONS[64]["plot"] = [0.18, 0.38, 0.65, 0.44]
PANELS[64] = [[0.18, 0.38, 0.65, 0.44]]
REGIONS[48]["title"] = [0.07, 0.34, 0.17, 0.26]
PANELS[48].append([0.065, 0.32, 0.18, 0.30])
for order, xs, y, w, h in [
    (60, [0.198, 0.298, 0.443, 0.538, 0.684, 0.780], 0.24, 0.061, 0.50),
    (62, [0.235, 0.338, 0.492, 0.590, 0.751, 0.854], 0.32, 0.080, 0.29),
]:
    for key in ["readout0", "readout1", "readout2"]:
        REGIONS[order].pop(key, None)
    PANELS[order] = []
    for i, x in enumerate(xs):
        REGIONS[order][f"cell{i}"] = [x, y, w, h]
        PANELS[order].append([x, y, w, h])
    if order == 62:
        REGIONS[order]["footer0"] = [0.74, 0.78, 0.20, 0.13]
        PANELS[order].append([0.73, 0.77, 0.21, 0.14])

REGIONS[11].update(title=[0.56, 0.27, 0.37, 0.20], weekday=[0.57, 0.66, 0.36, 0.17])
PANELS[11] = [[0.08, 0.27, 0.27, 0.48], [0.55, 0.26, 0.39, 0.22], [0.57, 0.65, 0.36, 0.19]]
REGIONS[59]["readout2"] = [0.80, 0.64, 0.13, 0.08]
PANELS[59] += [[0.785, 0.63, 0.16, 0.09]]
REGIONS[69].update(care=[0.43, 0.64, 0.48, 0.25], info=[0.43, 0.64, 0.48, 0.25])
REGIONS[72].update(care=[0.65, 0.51, 0.27, 0.28], info=[0.65, 0.51, 0.27, 0.28])
REGIONS[73].update(care=[0.72, 0.73, 0.23, 0.19], info=[0.72, 0.73, 0.23, 0.19])
PANELS[69] = [[0.42, 0.63, 0.50, 0.26]]
PANELS[72] = [[0.64, 0.50, 0.29, 0.29]]
PANELS[73] = [[0.71, 0.68, 0.25, 0.25]]
REGIONS[75].update(
    play=[0.44, 0.28, 0.08, 0.10],
    timer=[0.76, 0.28, 0.16, 0.10],
    page=[0.94, 0.28, 0.05, 0.10],
    master=[0.71, 0.74, 0.23, 0.015],
)
for i in range(4):
    REGIONS[75][f"row{i}"] = [0.44, 0.40 + i * 0.08, 0.20, 0.07]
    REGIONS[75][f"slider{i}"] = [0.70, 0.425 + i * 0.08, 0.25, 0.015]
PANELS[75] = [[0.43, 0.26, 0.54, 0.50]]
REGIONS[41].update(
    less=[0.17, 0.63, 0.075, 0.09],
    toggle=[0.255, 0.63, 0.075, 0.09],
    more=[0.34, 0.63, 0.075, 0.09],
    wind=[0.18, 0.755, 0.22, 0.018],
)
PANELS[41] = [[0.165, 0.62, 0.255, 0.18]]
REGIONS[54]["footer1"] = [0.85, 0.42, 0.095, 0.10]
REGIONS[55]["footer1"] = [0.66, 0.64, 0.25, 0.06]
PANELS[55].append([0.66, 0.63, 0.25, 0.07])
REGIONS[74]["scenery"] = [0.13, 0.23, 0.23, 0.48]
REGIONS[75]["scenery"] = [0.12, 0.25, 0.21, 0.42]
PANELS[34].append([0.045, 0.30, 0.18, 0.35])

# Actual-size readability audit: clear the complete old glyphs, then centre
# live information in the screen aperture rather than the source sample label.
PANELS[12][0] = [0.245, 0.105, 0.535, 0.485]
REGIONS[12].update(day=[0.26, 0.23, 0.49, 0.22], lunar=[0.59, 0.74, 0.25, 0.15])
REGIONS[11]["weekday"] = [0.57, 0.62, 0.36, 0.23]
PANELS[11][2] = [0.56, 0.61, 0.38, 0.25]
PANELS[13][0] = [0.09, 0.095, 0.78, 0.71]
REGIONS[13].update(title=[0.19, 0.115, 0.58, 0.08], previous=[0.10, 0.12, 0.065, 0.07], next=[0.79, 0.12, 0.065, 0.07])
REGIONS[10].update(
    title=[0.235, 0.105, 0.39, 0.115],
    previous=[0.17, 0.105, 0.045, 0.115],
    next=[0.645, 0.105, 0.045, 0.115],
    grid=[0.17, 0.245, 0.52, 0.605],
)
PANELS[10] = [[0.17, 0.10, 0.52, 0.75]]
REGIONS[26].update(keyboard=[0.25, 0.29, 0.23, 0.28], mouse=[0.74, 0.29, 0.17, 0.28])
PANELS[26] = [[0.25, 0.285, 0.23, 0.29], [0.74, 0.285, 0.17, 0.29]]
for order, panel in {
    28: [0.275, 0.25, 0.425, 0.56],
    29: [0.10, 0.54, 0.72, 0.20],
    30: [0.485, 0.275, 0.48, 0.415],
}.items():
    PANELS[order] = [panel]
    x, y, w, h = panel
    REGIONS[order]["title"] = [x + 0.015, y + 0.015, w - 0.03, h * 0.69]
# Register the interior of each physical key, not a second button above it.
REGIONS[28].update(
    previous=[0.785, 0.36, 0.052, 0.32],
    toggle=[0.85, 0.36, 0.052, 0.32],
    next=[0.925, 0.36, 0.052, 0.32],
    cover=[0.055, 0.23, 0.185, 0.52],
)
REGIONS[29].update(
    previous=[0.15, 0.795, 0.16, 0.10],
    toggle=[0.405, 0.795, 0.16, 0.10],
    next=[0.665, 0.795, 0.16, 0.10],
    cover=[0.105, 0.13, 0.64, 0.40],
)
REGIONS[30].update(
    previous=[0.515, 0.755, 0.10, 0.14],
    toggle=[0.675, 0.755, 0.10, 0.14],
    next=[0.835, 0.755, 0.10, 0.14],
    cover=[0.085, 0.19, 0.315, 0.60],
)
REGIONS[42]["world"] = [0.135, 0.255, 0.735, 0.45]
PANELS[42] = [[0.132, 0.24, 0.742, 0.48]]
REGIONS[45]["phase"] = [0.34, 0.35, 0.33, 0.07]
PANELS[45].append([0.33, 0.34, 0.35, 0.09])
PANELS[46].append([0.12, 0.16, 0.25, 0.13])
REGIONS[67].update(count=[0.105, 0.24, 0.16, 0.44], unit=[0.275, 0.32, 0.105, 0.24], title=[0.50, 0.29, 0.28, 0.21])
PANELS[67][0] = [0.102, 0.23, 0.165, 0.48]
PANELS[67].append([0.27, 0.31, 0.11, 0.25])
REGIONS[67]["target"] = [0.855, 0.30, 0.105, 0.40]
PANELS[67][2] = [0.852, 0.255, 0.11, 0.52]
PANELS[23].append([0.57, 0.70, 0.31, 0.22])
REGIONS[70]["info"] = [0.735, 0.80, 0.175, 0.13]
REGIONS[71]["info"] = [0.045, 0.25, 0.08, 0.42]
for i in range(4):
    REGIONS[75][f"row{i}"] = [0.445, 0.36 + i * 0.095, 0.19, 0.09]
    REGIONS[75][f"slider{i}"] = [0.66, 0.3975 + i * 0.095, 0.28, 0.015]
REGIONS[75].update(
    play=[0.44, 0.25, 0.06, 0.085],
    timer=[0.52, 0.25, 0.17, 0.085],
    master=[0.73, 0.29, 0.21, 0.015],
    page=[0.945, 0.25, 0.035, 0.085],
)

# Clear full sample labels, including their units and captions. Keep the bevels
# and hardware outside these measured glass interiors.
PANELS[14].append([0.83, 0.26, 0.12, 0.43])
PANELS[15] = [[0.275, 0.145, 0.325, 0.72], [0.66, 0.185, 0.21, 0.66]]
PANELS[16] = [[0.15, 0.11, 0.28, 0.47], [0.80, 0.19, 0.15, 0.40], [0.10, 0.735, 0.84, 0.215]]
PANELS[17] = [[0.59, 0.22, 0.25, 0.59]]
PANELS[19][0] = [0.16, 0.22, 0.40, 0.56]
REGIONS[19]["gauge0"] = [0.18, 0.24, 0.36, 0.50]
PANELS[21][1] = [0.70, 0.27, 0.24, 0.55]
PANELS[23] = [[0.21, 0.20, 0.38, 0.30], [0.74, 0.13, 0.16, 0.43], [0.14, 0.70, 0.35, 0.22], [0.55, 0.70, 0.32, 0.22]]
REGIONS[23]["gauge1"] = [0.74, 0.14, 0.16, 0.38]
REGIONS[23]["gauge3"] = [0.56, 0.73, 0.30, 0.17]
PANELS[25][0] = [0.21, 0.43, 0.65, 0.49]
PANELS[48][0] = [0.43, 0.25, 0.52, 0.55]
REGIONS[26].update(keyboardIcon=[0.17, 0.31, 0.055, 0.23], mouseIcon=[0.645, 0.33, 0.055, 0.27])
PANELS[26] += [[0.165, 0.29, 0.065, 0.29], [0.635, 0.29, 0.075, 0.40]]
PANELS[55] = [[0.12, 0.07, 0.57, 0.48], [0.64, 0.665, 0.27, 0.215]]
REGIONS[55].update(footer1=[0.65, 0.675, 0.26, 0.065], footer0=[0.65, 0.755, 0.26, 0.115])
REGIONS[13].update(weekdays=[0.10, 0.23, 0.76, 0.05], grid=[0.10, 0.29, 0.76, 0.50])
PANELS[75] = [[0.455, 0.31, 0.515, 0.35]]
for i, x in enumerate([0.415, 0.55, 0.685, 0.82]):
    REGIONS[75][f"toggle{i}"] = [x, 0.80, 0.08, 0.11]
    REGIONS[75][f"row{i}"] = [0.47, 0.315 + i * 0.085, 0.17, 0.08]
    REGIONS[75][f"slider{i}"] = [0.665, 0.3475 + i * 0.085, 0.29, 0.015]
REGIONS[75].update(
    transport=[0.095, 0.47, 0.26, 0.23],
    play=[0.11, 0.49, 0.06, 0.12],
    timer=[0.18, 0.50, 0.13, 0.10],
    page=[0.315, 0.50, 0.035, 0.10],
    master=[0.11, 0.65, 0.235, 0.015],
)
PANELS[74] = [[0.515, 0.26, 0.405, 0.33]]
REGIONS[74].update(
    source=[0.54, 0.705, 0.085, 0.10],
    toggle0=[0.675, 0.705, 0.085, 0.10],
    play=[0.675, 0.705, 0.085, 0.10],
    page=[0.81, 0.705, 0.085, 0.10],
    timer=[0.535, 0.285, 0.35, 0.20],
)
REGIONS[27].update(
    previous=[0.675, 0.765, 0.075, 0.12], toggle=[0.765, 0.765, 0.075, 0.12], next=[0.865, 0.765, 0.075, 0.12]
)
REGIONS[24].update(
    down=[0.195, 0.31, 0.22, 0.29],
    up=[0.715, 0.31, 0.22, 0.29],
    downIcon=[0.155, 0.31, 0.035, 0.29],
    upIcon=[0.675, 0.31, 0.035, 0.29],
)
PANELS[24] = [[0.15, 0.15, 0.27, 0.70], [0.67, 0.15, 0.27, 0.70]]
PANELS[47] = [[0.12, 0.19, 0.28, 0.62], [0.68, 0.19, 0.27, 0.62]]
REGIONS[47].update(downIcon=[0.20, 0.20, 0.12, 0.12], upIcon=[0.755, 0.20, 0.12, 0.12])
PANELS[29] = [[0.10, 0.54, 0.74, 0.20]]
# Spectrum widgets do not own a media session. Replace painted sample transport
# keys with the actual clock instead of advertising buttons that cannot work.
PANELS[31].append([0.83, 0.51, 0.145, 0.30])
REGIONS[31]["clock"] = [0.84, 0.36, 0.13, 0.31]

# Polygon apertures follow the original glass, including the album's overlapping
# control pod. The photo stays undistorted and cannot cover the hardware.
APERTURES = {
    27: (
        "photo",
        [
            (0.10, 0.09),
            (0.68, 0.09),
            (0.70, 0.16),
            (0.83, 0.16),
            (0.83, 0.385),
            (0.69, 0.385),
            (0.63, 0.48),
            (0.63, 0.75),
            (0.60, 0.835),
            (0.10, 0.835),
            (0.065, 0.73),
            (0.065, 0.18),
        ],
    ),
    28: (
        "cover",
        [
            (0.07, 0.18),
            (0.21, 0.18),
            (0.25, 0.32),
            (0.25, 0.72),
            (0.21, 0.90),
            (0.07, 0.90),
            (0.035, 0.72),
            (0.035, 0.32),
        ],
    ),
    29: (
        "cover",
        [
            (0.12, 0.10),
            (0.70, 0.10),
            (0.78, 0.17),
            (0.78, 0.51),
            (0.73, 0.55),
            (0.10, 0.55),
            (0.075, 0.50),
            (0.075, 0.15),
        ],
    ),
    30: (
        "cover",
        [
            (0.10, 0.13),
            (0.36, 0.13),
            (0.395, 0.23),
            (0.395, 0.80),
            (0.36, 0.895),
            (0.10, 0.895),
            (0.065, 0.80),
            (0.065, 0.23),
        ],
    ),
}
for order, (key, points) in APERTURES.items():
    xs, ys = zip(*points)
    REGIONS[order][key] = [min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)]


def registration(entry, artwork, reference):
    order = entry["order"]
    width, height = entry["native"]
    bx, by, bw, bh = artwork["bounds"]
    scale = min((width - 14) / bw, (height - 14) / bh)
    dx, dy = (width - bw * scale) / 2, (height - bh * scale) / 2

    def rect(r):
        x, y, w, h = r
        return [dx + x * bw * scale, dy + y * bh * scale, w * bw * scale, h * bh * scale]

    def source(r):
        x, y, w, h = r
        return [int(bx + x * bw), int(by + y * bh), max(1, int(w * bw)), max(1, int(h * bh))]

    regions = {k: rect(v) for k, v in REGIONS[order].items()}
    defaults = {
        k: (
            0x00E7EFFF if any(n in k for n in ["accent", "second", "sand", "color_top", "color_middle"]) else 0xF3F8FAFF
        )
        for k, v in reference.get("defaults", {}).items()
        if isinstance(v, int)
    }
    # The input controller has a system-font setting omitted by the reference
    # skin. Other controllers without a font setting use Skin's bundled default.
    if "font_family" in reference.get("defaults", {}) or order == 26:
        defaults["font_family"] = "Chakra Petch"
    result = dict(
        folder=reference["folder"],
        columns=entry["columns"],
        rows=entry["rows"],
        scene=[width, height],
        regions=regions,
        layers=[
            dict(name="body", file="body.png", source=artwork["bounds"], destination=[dx, dy, bw * scale, bh * scale])
        ],
        parts={},
        defaults=defaults,
        palette="terminal",
        screens=[rect(v) for v in PANELS.get(order, [])],
        styles={},
        armour=[],
        actuators=[],
    )
    if order == 69:
        result["defaults"]["show_info"] = 1
    if order in APERTURES:
        key, points = APERTURES[order]
        result["apertures"] = {key: [[dx + x * bw * scale, dy + y * bh * scale] for x, y in points]}
    if order in (27, 74, 75):
        result["buttonStyle"] = "embedded"
    if order == 53:
        result["defaults"]["seconds"] = 0
    if order == 20:
        result["defaults"]["primary"] = "cpu"
    if order == 42:
        result["defaults"]["text"] = "STREETLINK"
    if order in (18, 19):
        result["regions"]["gauge3"] = rect([0.89, 0.22, 0.10, 0.53] if order == 18 else [0.90, 0.26, 0.07, 0.43])
    if order == 23:
        result["regions"]["gauge2"] = rect([0.15, 0.76, 0.32, 0.15])
        result["screens"].append(rect([0.14, 0.70, 0.35, 0.22]))
    if order in (7, 8, 9, 38, 52, 53, 54, 55):
        key = "world" if order in (7, 8, 9) else "plot"
        result["screens"] = [rect(v) for v in PANELS.get(order, [])[1:]]
        result["roundScreens"] = [regions[key]]
        if order == 9:
            result["screens"] = [regions["readout0"]]
    if order == 54:
        result["screens"] = []
        result["roundScreens"] = []
    if order == 11:
        result["styles"]["title"] = {"size": 13, "color": 0xF3F8FAFF, "minimumSize": 11}
    for key in ("city", "phase", "weekday", "lunar", "footer0", "footer1", "target"):
        if key in regions:
            r = regions[key]
            result["styles"][key] = dict(size=13, color=0xD8E9EFFF, minimumSize=11, overflow="optional")
    for key, r in regions.items():
        if key.startswith("cell") or key in [
            "title",
            "day",
            "readout0",
            "readout1",
            "readout2",
            "temperature",
            "counter",
            "down",
            "up",
            "count",
            "free",
        ]:
            result["styles"][key] = dict(
                size=max(12, min(r[3] * 0.85, 36)),
                color=0xF3F8FAFF,
                role="display" if key not in ["title", "free"] else "",
                minimumSize=min(12, r[3] * 0.8),
                overflow=(
                    "fit"
                    if key.startswith(("cell", "readout"))
                    or key in ("day", "count", "temperature", "down", "up", "counter")
                    else "ellipsis"
                ),
            )
    if order == 11:
        result["styles"]["title"] = {"size": 13, "color": 0xF3F8FAFF, "minimumSize": 11}
    if order in (28, 29, 30):
        result["buttonStyle"] = "embedded"
        result["styles"]["title"].update(size=15, minimumSize=12, overflow="ellipsis")
        x, y, w, h = regions["title"]
        if h >= 28:
            result["regions"]["titleWithArtist"] = [x, y, w, h * 0.55]
            result["regions"]["artist"] = [x, y + h * 0.60, w, h * 0.40]
            result["styles"]["titleWithArtist"] = dict(result["styles"]["title"])
            result["styles"]["artist"] = dict(size=12, color=0xD8E9EFFF, minimumSize=11, overflow="ellipsis")
    if order == 13:
        result["styles"]["title"].update(size=22, minimumSize=18)
    if 31 <= order <= 38 or order in (49, 50):
        result["defaults"].update(
            color=0x00E7EFFF, color_middle=0x00E7EFFF, color_top=0x00E7EFFF, peak_color=0xF4CE38FF
        )
    if order == 14:
        for key, r in [("humidity", [0.81, 0.24, 0.15, 0.14]), ("wind", [0.81, 0.60, 0.15, 0.14])]:
            result["regions"][key] = rect(r)
            result["screens"].append(rect(r))
    if entry["rows"] == 1 and entry["columns"] >= 2 and order not in [43, 44, 69]:
        cut = 0.5
        if order in [11, 14, 17, 21, 47, 48, 53, 57, 58, 66, 74]:
            cut = 0.45
        result["horizontal"] = dict(
            left=dx, right=dx + bw * scale, cuts=[dx + cut * bw * scale], inset=7, preserveEndcaps=True
        )
    if order in [3, 4]:
        cards = (
            [(x, 0.29, 0.09, 0.61) for x in [0.20, 0.31, 0.45, 0.57, 0.71, 0.83]]
            if order == 3
            else [(x, 0.27, 0.112, 0.43) for x in [0.023, 0.155, 0.326, 0.458, 0.630, 0.767]]
        )
        result["exclusions"] = [rect(r) for r in cards]
        result["regions"]["world"] = [0, 0, width, height]
        result["toyGeometry"] = dict(size=[width, height], anchors=[], bodies=[])
        for i, r in enumerate(cards):
            p = rect(r)
            x, y, w, h = p
            result["parts"][f"card{i}"] = dict(
                file="body.png",
                source=source(r),
                blank=([0.12, 0.22, 0.80, 0.66] if order == 3 else [0.15, 0.16, 0.70, 0.68]),
            )
            result["regions"][f"digit{i}"] = p
            result["toyGeometry"]["anchors"].append([x + w / 2, y - 3])
            result["toyGeometry"]["bodies"].append([x + w / 2, y + h / 2, w, h])
        if order == 4:
            result["regions"]["date"] = rect([0.55, 0.86, 0.23, 0.08])
            result["screens"].append(rect([0.55, 0.835, 0.23, 0.12]))
            result["styles"]["date"] = dict(size=12, color=0xF3F8FAFF, minimumSize=11)
    if order in [41, 51]:
        rings = (
            [(0.57, y, 0.27, 0.09) for y in [0.20, 0.43, 0.69]]
            if order == 41
            else [(0.47, y, 0.39, 0.08) for y in [0.21, 0.41, 0.61]]
        )
        result["exclusions"] = [rect(r) for r in rings]
        result["regions"]["world"] = [0, 0, width, height]
        result["toyGeometry"] = dict(size=[width, height], anchors=[], bodies=[])
        for i, r in enumerate(rings):
            x, y, w, h = rect(r)
            result["parts"][f"charm{i}"] = dict(file="body.png", source=source(r))
            result["toyGeometry"]["anchors"].append([x + w / 2, y - 5])
            result["toyGeometry"]["bodies"].append([x + w / 2, y + h / 2, w, h])
        if order == 41:
            result["regions"]["readout0"] = rect([0.20, 0.26, 0.20, 0.24])
            result["screens"].append(rect([0.19, 0.26, 0.22, 0.25]))
    if order in (69, 72, 73):
        foliage = {69: [0.06, 0.02, 0.93, 0.55], 72: [0.00, 0.02, 0.85, 0.42], 73: [0.37, 0.12, 0.45, 0.53]}[order]
        result["regions"]["plant"] = rect(foliage)
        result["exclusions"] = [rect(foliage)]
        result["parts"]["foliage"] = dict(file="body.png", source=source(foliage))
        if order == 73:
            left = [0.00, 0.00, 0.38, 0.33]
            result["regions"]["plantLeft"] = rect(left)
            result["exclusions"].append(rect(left))
            result["parts"]["foliageLeft"] = dict(file="body.png", source=source(left))
        info = regions["info"]
        result["regions"]["care"] = [info[0], info[1], info[2] * 0.25, info[3]]
        result["regions"]["info"] = [info[0] + info[2] * 0.27, info[1], info[2] * 0.73, info[3]]
    if order in [68, 70, 71]:
        x, y, w, h = regions["water"]
        inset = min(w, h) * 0.07
        result["waterMask"] = dict(
            outline=[
                [x + inset, y],
                [x + w - inset, y],
                [x + w, y + inset],
                [x + w, y + h - inset],
                [x + w - inset, y + h],
                [x + inset, y + h],
                [x, y + h - inset],
                [x, y + inset],
            ],
            holes=[],
        )
    if order in (5, 6, 39, 40):
        result["defaults"] = {}
        result["physics"] = {"chambers": [], "lanes": []}
        if order == 5:
            for line in range(3):
                points = [
                    (0.245, 0.50 + line * 0.135),
                    (0.325, 0.50 + line * 0.135),
                    (0.39, 0.588 + line * 0.126),
                    (0.73, 0.588 + line * 0.126),
                ]
                result["physics"]["lanes"].append([[rect([x, y, 0, 0])[0], rect([x, y, 0, 0])[1]] for x, y in points])
        else:
            disks = {
                6: [(0.07, 0.15, 0.22, 0.62)],
                39: [(0.13, 0.37, 0.29, 0.38), (0.64, 0.46, 0.15, 0.24)],
                40: [(0.095, 0.23, 0.20, 0.50), (0.60, 0.27, 0.15, 0.41)],
            }[order]
            result["physics"]["chambers"] = [rect(r) for r in disks]
        if order in (39, 40):
            result["screens"] = []
            result["regions"]["readout0"] = rect([0.875, 0.51, 0.09, 0.11] if order == 39 else [0.88, 0.37, 0.09, 0.14])
            result["screens"].append(result["regions"]["readout0"])
            result["regions"]["signal"] = rect([0.36, 0.71, 0.23, 0.12] if order == 39 else [0.865, 0.61, 0.11, 0.10])
            result["screens"].append(result["regions"]["signal"])
            if order == 40:
                result["regions"]["meters"] = rect([0.405, 0.30, 0.075, 0.38])
                result["screens"].append(result["regions"]["meters"])
            result["regions"]["toggle"] = rect([0.47, 0.89, 0.07, 0.10] if order == 39 else [0.64, 0.44, 0.07, 0.17])
            if order == 39:
                result["regions"]["previous"] = rect([0.38, 0.89, 0.07, 0.10])
                result["regions"]["next"] = rect([0.57, 0.89, 0.07, 0.10])
    apply_review(order, result, rect)
    return result
