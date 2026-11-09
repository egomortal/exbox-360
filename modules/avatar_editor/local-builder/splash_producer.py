"""Locally generate splash PNGs from the user's verified original Xbox logo.

No artwork is embedded. The centered white layout is an authored static
composition, not a reconstruction of the animated retail boot sequence.
"""
from pathlib import Path
import hashlib
import io
import struct
import zlib
from PIL import Image

INPUT_SHA256 = '2881b4e94e386d6e15b03dc4e229ac006e3897e36d38b21c14a151198c0081fb'
INPUT_BYTES = 35835
INPUT_SIZE = (671, 416)
OUTPUTS = {'assets/ui/loading-1080p.png': (1920, 1080),
           'assets/ui/loading-2160p.png': (3840, 2160)}
EXPECTED = {'assets/ui/loading-1080p.png': '78766e28aeada1d891b063a37ed624401d7b3707d1e1a95dc308d50968f43f79', 'assets/ui/loading-2160p.png': '4ba13b919b018a380ae0821885c4d69e50b25a091e5dd4d66ddbce05f6dc256c'}
MAX_ENCODED_BYTES = 16 * 1024 * 1024


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_logo(original):
    path = Path(original)
    if not path.is_file() or path.stat().st_size != INPUT_BYTES:
        raise ValueError('Supply the supported original splash_360.png in the originals folder.')
    data = path.read_bytes()
    if sha256(data) != INPUT_SHA256:
        raise ValueError('Unsupported splash_360.png: source SHA256 differs.')
    with Image.open(io.BytesIO(data)) as image:
        if image.format != 'PNG' or image.size != INPUT_SIZE or getattr(image, 'n_frames', 1) != 1:
            raise ValueError('Unsupported splash logo dimensions or image format.')
        image.load()
        return image.convert('RGBA')


def compose(logo, size):
    """Center the full logo within the half-width region, preserving aspect."""
    width, height = size
    if size not in OUTPUTS.values():
        raise ValueError('Supported splash sizes are 1920x1080 and 3840x2160.')
    target_width = width // 2
    target_height = round(logo.height * target_width / logo.width)
    if target_height > height * 2 // 3:
        target_height = height * 2 // 3
        target_width = round(logo.width * target_height / logo.height)
    scaled = logo.convert('RGBA').resize((target_width, target_height), Image.Resampling.LANCZOS)
    left, top = (width - target_width) // 2, (height - target_height) // 2
    canvas = Image.new('RGBA', size, (255, 255, 255, 255))
    canvas.alpha_composite(scaled, (left, top))
    return canvas.convert('RGB'), [left, top, target_width, target_height]


def encode_png(image):
    """RGB8, noninterlaced, filter 0 for the existing bounded native decoder."""
    if image.mode != 'RGB' or image.size not in OUTPUTS.values():
        raise ValueError('Unsupported splash image.')
    width, height = image.size
    pixels = image.tobytes()
    stride = width * 3
    filtered = b''.join(b'\0' + pixels[y * stride:(y + 1) * stride] for y in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    encoded = b'\x89PNG\r\n\x1a\n'
    encoded += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    encoded += chunk(b'IDAT', zlib.compress(filtered, 9))
    encoded += chunk(b'IEND', b'')
    if len(encoded) > MAX_ENCODED_BYTES:
        raise ValueError('Generated splash exceeds the encoded size bound.')
    return encoded


def generate(original):
    logo = read_logo(original)
    result = {relative: encode_png(compose(logo, size)[0]) for relative, size in OUTPUTS.items()}
    if EXPECTED and {name: sha256(data) for name, data in result.items()} != EXPECTED:
        raise ValueError('Splash producer environment differs from validated output hashes.')
    return result
