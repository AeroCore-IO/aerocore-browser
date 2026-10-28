import math

def clamp(value, start, end):
    return max(start, min(value, end))

def progress_between(value, start, end):
    if end <= start:
        return 1.0 if value >= end else 0.0
    return clamp((value - start) / (end - start), 0.0, 1.0)

def lerp(start, end, t):
    return start + ((end - start) * clamp(t, 0.0, 1.0))

def get_profile(w, h, diagonal):
    # 16:9 check (not used in formulas but good for context)
    dpi = math.sqrt(w**2 + h**2) / diagonal
    return {"w": w, "h": h, "diagonal": diagonal, "dpi": dpi}

def readability_curve_progress(profile):
    kHandheldDiagonalInches = 7.4
    kCouchAnchorDiagonalInches = 45.88
    kHandheldPixelDiagonal = 1509.44 # sqrt(1280^2 + 800^2)
    kCouchAnchorPixelDiagonal = 4405.81 # sqrt(3840^2 + 2160^2)
    kHandheldReferenceDpi = 203.0
    kCouchReferenceDpi = 96.04
    
    diagonal_progress = math.pow(progress_between(profile["diagonal"], kHandheldDiagonalInches, kCouchAnchorDiagonalInches), 1.20)
    pixel_diagonal = math.hypot(profile["w"], profile["h"])
    resolution_progress = math.pow(progress_between(pixel_diagonal, kHandheldPixelDiagonal, kCouchAnchorPixelDiagonal), 1.10)
    
    return clamp((diagonal_progress * 0.80) + (resolution_progress * 0.20), 0.0, 1.0)

def extended_large_screen_progress(profile):
    kCouchAnchorDiagonalInches = 45.88
    kLargeScreenEndDiagonalInches = 85.0
    return math.pow(progress_between(profile["diagonal"], kCouchAnchorDiagonalInches, kLargeScreenEndDiagonalInches), 1.10)

def automatic_tv_app_ui_scale(profile):
    curve_progress = readability_curve_progress(profile)
    scale = lerp(1.00, 2.20, curve_progress)
    if profile["diagonal"] > 45.88:
        scale = lerp(2.20, 2.35, extended_large_screen_progress(profile))
    return clamp(scale, 1.00, 2.40)

def automatic_tv_zoom_factor(profile):
    curve_progress = readability_curve_progress(profile)
    zoom = lerp(1.10, 1.90, curve_progress)
    if profile["diagonal"] > 45.88:
        zoom = lerp(1.90, 2.00, extended_large_screen_progress(profile))
    return clamp(zoom, 1.05, 2.20)

def automatic_tv_minimum_font_size(profile):
    curve_progress = readability_curve_progress(profile)
    minimum_font_size = round(lerp(17.0, 23.0, curve_progress))
    if profile["diagonal"] > 45.88:
        minimum_font_size = round(lerp(23.0, 26.0, extended_large_screen_progress(profile)))
    return clamp(minimum_font_size, 16, 28)

anchors = [
    (1280, 800, 7.4),
    (3840, 2160, 45.88)
]

resolutions = [1280, 1920, 2560, 3840]
diagonals = [32, 43, 55, 65, 75, 85, 92]

print("### 1) Device Anchors")
print("| Device | Resolution | Diagonal (in) | DPI | App UI Scale | Page Zoom | Min Font |")
print("| :--- | :--- | :--- | :--- | :--- | :--- | :--- |")
for w, h, d in anchors:
    p = get_profile(w, h, d)
    name = "Steam Deck OLED" if d == 7.4 else "4K Anchor"
    print(f"| {name} | {w}x{h} | {d} | {p['dpi']:.2f} | {automatic_tv_app_ui_scale(p):.2f} | {automatic_tv_zoom_factor(p):.2f} | {automatic_tv_minimum_font_size(p)} |")

print("\n### 2) 16:9 Matrix")
print("| Resolution | Diagonal (in) | DPI | App UI Scale | Page Zoom | Min Font |")
print("| :--- | :--- | :--- | :--- | :--- | :--- | :--- |")
for w in resolutions:
    h = int(w * 9 / 16)
    for d in diagonals:
        p = get_profile(w, h, d)
        print(f"| {w}x{h} | {d}\" | {p['dpi']:.2f} | {automatic_tv_app_ui_scale(p):.2f} | {automatic_tv_zoom_factor(p):.2f} | {automatic_tv_minimum_font_size(p)} |")
