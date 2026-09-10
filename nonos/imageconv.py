#!/usr/bin/env python3
"""
Конвертер изображений в цветные прямоугольные символы (ANSI 16 цветов + жирность).
Использует полублоки (▀) для вертикального сжатия.
Поддерживает дизеринг (--dither) для улучшения качества в 16-цветной палитре.
Требуется Python 3 и Pillow.
"""

import argparse
import sys
from PIL import Image, ImageEnhance

# 16 цветов ANSI в RGB
ANSI_COLORS = {
    'black':        (0, 0, 0),
    'red':          (128, 0, 0),
    'green':        (0, 128, 0),
    'yellow':       (128, 128, 0),
    'blue':         (0, 0, 128),
    'magenta':      (128, 0, 128),
    'cyan':         (0, 128, 128),
    'white':        (192, 192, 192),
    'bright_black': (128, 128, 128),
    'bright_red':   (255, 0, 0),
    'bright_green': (0, 255, 0),
    'bright_yellow':(255, 255, 0),
    'bright_blue':  (0, 0, 255),
    'bright_magenta':(255, 0, 255),
    'bright_cyan':  (0, 255, 255),
    'bright_white': (255, 255, 255),
}

ANSI_COLOR_LIST = list(ANSI_COLORS.values())
FG_CODES = [30, 31, 32, 33, 34, 35, 36, 37, 90, 91, 92, 93, 94, 95, 96, 97]
BG_CODES = [40, 41, 42, 43, 44, 45, 46, 47, 100, 101, 102, 103, 104, 105, 106, 107]

def closest_ansi_color(r, g, b):
    best_idx = 0
    best_dist = float('inf')
    for i, (cr, cg, cb) in enumerate(ANSI_COLOR_LIST):
        dr = r - cr
        dg = g - cg
        db = b - cb
        dist = dr*dr + dg*dg + db*db
        if dist < best_dist:
            best_dist = dist
            best_idx = i
    return best_idx

def get_terminal_size():
    try:
        import shutil
        cols, rows = shutil.get_terminal_size()
        return cols, rows
    except:
        return 80, 24

def img_to_ansi_ascii(image_path, width=None, height=None, use_full_block=False, vibrant=False, dither=False):
    img = Image.open(image_path).convert("RGB")
    orig_w, orig_h = img.size

    # Усиление цвета (если нужно)
    if vibrant:
        enhancer = ImageEnhance.Color(img)
        img = enhancer.enhance(1.4)
        enhancer = ImageEnhance.Contrast(img)
        img = enhancer.enhance(1.2)
        enhancer = ImageEnhance.Brightness(img)
        img = enhancer.enhance(1.1)

    # Определение целевого размера
    if width is None and height is None:
        cols, rows = get_terminal_size()
        cols = max(10, cols - 2)
        rows = max(5, rows - 2)
        aspect = orig_w / orig_h
        if use_full_block:
            target_w = cols
            target_h = int(target_w / aspect)
            if target_h > rows:
                target_h = rows
                target_w = int(target_h * aspect)
        else:
            target_h = rows * 2
            target_w = int(target_h * aspect)
            if target_w > cols:
                target_w = cols
                target_h = int(target_w / aspect)
            if target_h % 2 != 0:
                target_h -= 1
    else:
        if width is None:
            target_w = int(height * (orig_w / orig_h))
            target_h = height
        elif height is None:
            target_h = int(width / (orig_w / orig_h))
            target_w = width
        else:
            target_w = width
            target_h = height
        if not use_full_block:
            target_h *= 2

    img = img.resize((target_w, target_h), Image.Resampling.LANCZOS)

    # Дизеринг (если включён)
    if dither:
        # Создаём палитру из 16 цветов ANSI
        palette = [c for color in ANSI_COLOR_LIST for c in color]  # 48 значений
        # Дополняем до 256 цветов (остальные чёрные)
        palette += [0, 0, 0] * (256 - 16)
        pal_img = Image.new("P", (1, 1))
        pal_img.putpalette(palette)
        # Квантуем с дизерингом
        img = img.quantize(colors=16, palette=pal_img, dither=Image.FLOYDSTEINBERG)
        # Теперь img — палитровое изображение, каждый пиксель — индекс (0-15)
        pixels = img.load()
        # Для палитрового изображения pixels[x,y] возвращает индекс
        get_color_index = lambda x, y: pixels[x, y]
    else:
        # Без дизеринга — используем RGB и поиск ближайшего цвета
        pixels = img.load()
        get_color_index = lambda x, y: closest_ansi_color(*pixels[x, y])

    output = []
    if use_full_block:
        for y in range(target_h):
            line = ""
            for x in range(target_w):
                idx = get_color_index(x, y)
                line += f"\033[{BG_CODES[idx]}m  \033[0m"
            output.append(line)
    else:
        for y in range(0, target_h, 2):
            line = ""
            for x in range(target_w):
                idx_top = get_color_index(x, y)
                if y + 1 < target_h:
                    idx_bot = get_color_index(x, y + 1)
                else:
                    idx_bot = 0  # чёрный

                if idx_bot >= 8:
                    line += f"\033[{BG_CODES[idx_top]}m\033[1;{FG_CODES[idx_bot]}m▀\033[0m"
                else:
                    line += f"\033[{BG_CODES[idx_top]}m\033[{FG_CODES[idx_bot]}m▀\033[0m"
            output.append(line)

    return "\n".join(output)

def main():
    parser = argparse.ArgumentParser(description="Convert image to ASCII art using ANSI 16 colors + bold for bright colors.")
    parser.add_argument("image", help="Path to image file.")
    parser.add_argument("-w", "--width", type=int, help="Width in characters.")
    parser.add_argument("-H", "--height", type=int, help="Height in characters.")
    parser.add_argument("-f", "--full", action="store_true", help="Use full block (█) instead of half block.")
    parser.add_argument("-v", "--vibrant", action="store_true", help="Increase saturation and contrast.")
    parser.add_argument("-d", "--dither", action="store_true", help="Apply Floyd-Steinberg dithering for better quality in 16-color palette.")
    parser.add_argument("-o", "--output", help="Output file.")
    args = parser.parse_args()

    try:
        art = img_to_ansi_ascii(args.image, args.width, args.height, args.full, args.vibrant, args.dither)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(art)
    else:
        print(art)

if __name__ == "__main__":
    main()
