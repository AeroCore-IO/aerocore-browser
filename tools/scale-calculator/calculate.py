import math

def progress_between(value, start, end):
    if end <= start:
        return 1.0 if value >= end else 0.0
    return max(0.0, min(1.0, (value - start) / (end - start)))

def lerp(start, end, t):
    return start + (end - start) * max(0.0, min(1.0, t))

def detect_profile(w, h, diagonal_inches):
    dpi = math.hypot(w, h) / diagonal_inches
    return {"w": w, "h": h, "diagonal": diagonal_inches, "dpi": dpi}

def calculate_outputs(profile):
    w = profile["w"]
    h = profile["h"]
    diag = profile["diagonal"]
    dpi = profile["dpi"]
    
    kHandheldDiagonalInches = 7.4
    kCouchAnchorDiagonalInches = 45.88
    kLargeScreenEndDiagonalInches = 85.0
    
    kHandheldPixelDiagonal = math.hypot(1280, 800) # 1509.4369
    kCouchAnchorPixelDiagonal = math.hypot(3840, 2160) # 4405.8143
    
    pixel_diag = math.hypot(w, h)
    
    diag_progress = math.pow(progress_between(diag, kHandheldDiagonalInches, kCouchAnchorDiagonalInches), 1.20)
    res_progress = math.pow(progress_between(pixel_diag, kHandheldPixelDiagonal, kCouchAnchorPixelDiagonal), 1.10)
    
    curve_progress = max(0.0, min(1.0, (diag_progress * 0.80) + (res_progress * 0.20)))
    
    # App UI Scale
    app_scale = lerp(1.00, 2.20, curve_progress)
    # Page Zoom
    zoom = lerp(1.10, 1.90, curve_progress)
    # Min Font
    min_font = round(lerp(17.0, 23.0, curve_progress))
    
    if diag > 45.88:
        ext = math.pow(progress_between(diag, kCouchAnchorDiagonalInches, kLargeScreenEndDiagonalInches), 1.10)
        res_weight = progress_between(pixel_diag, kHandheldPixelDiagonal, kCouchAnchorPixelDiagonal)
        
        # Extension for scale
        max_delta_scale = lerp(0.12, 0.15, res_weight)
        app_scale += max_delta_scale * ext
        
        # Extension for zoom
        max_delta_zoom = lerp(0.08, 0.10, res_weight)
        zoom += max_delta_zoom * ext
        
        # Extension for font
        min_font += round(lerp(1.0, 3.0, res_weight) * ext)
        
    return {
        "dpi": dpi,
        "app_scale": max(1.0, min(2.4, app_scale)),
        "zoom": max(1.05, min(2.2, zoom)),
        "min_font": max(16, min(28, int(min_font)))
    }

anchors = [
    (1280, 800, 7.4, "Steam Deck OLED"),
    (3840, 2160, 45.88, "4K Anchor")
]

print("| Device | Resolution | Diagonal (in) | DPI | App UI Scale | Page Zoom | Min Font |")
print("| :--- | :--- | :--- | :--- | :--- | :--- | :--- |")
for w, h, d, name in anchors:
    res = calculate_outputs(detect_profile(w, h, d))
    print(f"| {name} | {w}x{h} | {d} | {res['dpi']:.2f} | {res['app_scale']:.2f} | {res['zoom']:.2f} | {res['min_font']} |")

print("\n")

resolutions = [
    (1280, 720),
    (1920, 1080),
    (2560, 1440),
    (3840, 2160)
]
diagonals = [32, 43, 55, 65, 75, 85, 92]

print("| Resolution \\ Diagonal | 32\" | 43\" | 55\" | 65\" | 75\" | 85\" | 92\" |")
print("| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |")

for w, h in resolutions:
    row = [f"{w}x{h}"]
    for d in diagonals:
        res = calculate_outputs(detect_profile(w, h, d))
        # Format: DPI/Scale/Zoom/Font
        cell = f"{res['dpi']:.0f}<br>{res['app_scale']:.2f}<br>{res['zoom']:.2f}<br>{res['min_font']}"
        row.append(cell)
    print("| " + " | ".join(row) + " |")
