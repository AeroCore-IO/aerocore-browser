import math

def get_metrics(diag_inch, w_px, h_px):
    aspect_ratio = w_px / h_px
    # diag^2 = w^2 + h^2 = (h * aspect)^2 + h^2 = h^2 * (aspect^2 + 1)
    h_inch = diag_inch / math.sqrt(aspect_ratio**2 + 1)
    w_inch = h_inch * aspect_ratio
    diag_px = math.sqrt(w_px**2 + h_px**2)
    dpi = diag_px / diag_inch
    return w_inch, h_inch, dpi

w_px, h_px = 3840, 2160
w45, h45, dpi45 = get_metrics(45.88, w_px, h_px)
w92, h92, dpi92 = get_metrics(92.0, w_px, h_px)

print(f"Resolution: {w_px}x{h_px}")
print(f"45.88\": Width={w45:.2f}in, Height={h45:.2f}in, DPI={dpi45:.2f}")
print(f"92.00\": Width={w92:.2f}in, Height={h92:.2f}in, DPI={dpi92:.2f}")

ratio = 92.0 / 45.88
print(f"\nDiagonal Ratio: {ratio:.4f}")
print(f"DPI Ratio (45.88/92): {dpi45/dpi92:.4f}")
print(f"Size Ratio (92/45.88): {w92/w45:.4f}")

doubled_dpi = dpi92 * 2
print(f"\nExact Double DPI of 92\": {doubled_dpi:.2f}")
print(f"Difference from 45.88\" DPI: {abs(dpi45 - doubled_dpi):.4f}")
