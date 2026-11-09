#!/usr/bin/env python3
"""Turns the logos in tools/svg/ (Material Design Icons, Apache 2.0) into components/source_keys/logos.h: one A8
mask per logo, how much of each pixel the logo covers. The screen paints a mask in any theme colour, so a logo is
light on a dark key, dark on a light one and white on the blue one, with no picture to fetch.

    pip install cairosvg pillow
    python3 tools/make_logos.py
"""
import io
from pathlib import Path

import cairosvg
from PIL import Image

SIZE = 80  # pixels a side; the tile scales a logo down to its key
LOGOS = {  # the name a key uses: the file in tools/svg/
    'windows': 'microsoft-windows',
    'apple': 'apple',
    'playstation': 'sony-playstation',
    'nintendo-switch': 'nintendo-switch',
    'xbox': 'microsoft-xbox',
    'linux': 'linux',
}

here = Path(__file__).resolve().parent
out = here.parent / 'components' / 'source_keys' / 'logos.h'
lines = [
    '#pragma once',
    '// Made by tools/make_logos.py from Material Design Icons (Apache 2.0). Do not edit by hand.',
    '#include <cstdint>',
    '',
    'namespace esphome::source_keys {',
    '',
    f'static constexpr int LOGO_SIZE = {SIZE};',
    '',
    'struct Logo {',
    '  const char *name;',
    '  const uint8_t *alpha;  // LOGO_SIZE * LOGO_SIZE bytes, row by row',
    '};',
    '',
]
names = []
for name, file in LOGOS.items():
    svg = (here / 'svg' / f'{file}.svg').read_text().replace('<path ', '<path fill="#000000" ')
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=SIZE, output_height=SIZE)
    alpha = Image.open(io.BytesIO(png)).convert('RGBA').getchannel('A').tobytes()
    ident = 'LOGO_' + name.upper().replace('-', '_')
    names.append((name, ident))
    lines.append(f'static const uint8_t {ident}[] = {{')
    for i in range(0, len(alpha), 24):
        lines.append('  ' + ', '.join(f'0x{b:02x}' for b in alpha[i:i + 24]) + ',')
    lines.append('};')
    lines.append('')
lines.append('static const Logo LOGOS[] = {')
for name, ident in names:
    lines.append(f'  {{"{name}", {ident}}},')
lines += ['};', '', '}  // namespace esphome::source_keys', '']
out.write_text('\n'.join(lines))
print(f'{out}: {len(names)} logos of {SIZE}x{SIZE}')
