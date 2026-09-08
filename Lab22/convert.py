from PIL import Image
im = Image.open("cat.jpg").convert("RGB").resize((240, 320))
px = list(im.getdata())
with open("cat_img.h", "w") as f:
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write("const uint16_t cat_img[240*320] = {\n")
    for i, (r, g, b) in enumerate(px):
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        v = ((v << 8) | (v >> 8)) & 0xFFFF      # byte-swap for the panel
        f.write(f"0x{v:04X},")
        if i % 16 == 15: f.write("\n")
    f.write("};\n")