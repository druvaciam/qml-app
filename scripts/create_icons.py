import os
from PIL import Image

src_path = r"C:\Users\druvaciam\.gemini\antigravity-ide\brain\c9fc8711-457f-465e-9b69-7911ca0121e6\qml_commander_icon_1787795483644.jpg"
out_dir = r"d:\proha\google\qml-app\resources"

os.makedirs(out_dir, exist_ok=True)

img = Image.open(src_path).convert("RGBA")

# Ensure perfectly square
w, h = img.size
min_dim = min(w, h)
left = (w - min_dim) // 2
top = (h - min_dim) // 2
img_cropped = img.crop((left, top, left + min_dim, top + min_dim))

# Save high-res PNGs
png_512 = img_cropped.resize((512, 512), Image.Resampling.LANCZOS)
png_256 = img_cropped.resize((256, 256), Image.Resampling.LANCZOS)
png_64 = img_cropped.resize((64, 64), Image.Resampling.LANCZOS)
png_32 = img_cropped.resize((32, 32), Image.Resampling.LANCZOS)

png_256.save(os.path.join(out_dir, "app_icon.png"), format="PNG")
png_32.save(os.path.join(out_dir, "app_icon_32.png"), format="PNG")

# Save multi-size ICO
icon_sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]
png_512.save(os.path.join(out_dir, "app_icon.ico"), format="ICO", sizes=icon_sizes)

print("Icons created successfully in", out_dir)
