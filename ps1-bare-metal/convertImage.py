#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# from https://github.com/spicyjpeg/ps1-bare-metal/blob/main/tools/convertImage.py

"""PlayStation 1 image/texture data converter

A simple script to convert image files into either raw 16bpp RGB data as
expected by the PS1's GPU, or 4bpp or 8bpp indexed color data plus a separate
16bpp color palette. Requires PIL/Pillow and NumPy to be installed.
"""

__version__ = "0.1.1"
__author__  = "spicyjpeg"

import logging
from argparse import ArgumentParser, FileType, Namespace
import imagequant

import numpy
from numpy import bool_, ndarray
from PIL   import Image
import struct

## RGBA to 16bpp colorspace conversion

LOWER_ALPHA_BOUND: int = 32
UPPER_ALPHA_BOUND: int = 192

# Color 0x0000 is interpreted by the PS1 GPU as fully transparent, so black
# pixels must be changed to dark gray to prevent them from becoming transparent.
TRANSPARENT_COLOR: int = 0x0000
BLACK_COLOR:       int = 0x0421

def convertRGBAto16(
        inputData: ndarray
) -> tuple[ndarray, bool]:
    source: ndarray = inputData.astype("<H")

    r:    ndarray = ((source[:, :, 0] + 4) >> 3).clip(0, 31)
    g:    ndarray = ((source[:, :, 1] + 4) >> 3).clip(0, 31)
    b:    ndarray = ((source[:, :, 2] + 4) >> 3).clip(0, 31)
    data: ndarray = r | (g << 5) | (b << 10)

    # Process the alpha channel using a simple threshold algorithm.
    has_translucency = False
    if source.shape[2] == 4:
        alpha: ndarray = source[:, :, 3]
        valid_pixels = alpha >= LOWER_ALPHA_BOUND
        if (valid_pixels & (alpha < UPPER_ALPHA_BOUND)).any():
            has_translucency = True
            opaque_alpha = alpha >= UPPER_ALPHA_BOUND
            data = numpy.select(
                (
                    opaque_alpha & (data == 0), # black pixels
                    opaque_alpha, # Leave as-is
                    valid_pixels  # Set semitransparency flag
                ), (
                    numpy.array(0x0421, dtype='uint16'),
                    data,
                    data | 0x8000
                ),
                0
	        )
        else:
            data = numpy.where(valid_pixels, data | 0x8000, 0)
    else:
	    data |= 0x8000

    return data.reshape(source.shape[:-1]), has_translucency

## Indexed color image handling

def convertIndexedImage(
        imageObj: Image.Image, maxNumColors: int
) -> tuple[ndarray, ndarray, bool]:
    # PIL/Pillow doesn't provide a proper way to get the number of colors in a
    # palette, so here's an extremely ugly hack.
    colorDepth: int   = { "RGB": 3, "RGBA": 4 }[imageObj.palette.mode]
    clutData:   bytes = imageObj.palette.tobytes()
    numColors:  int   = len(clutData) // colorDepth

    if maxNumColors == 1:
        if numColors <= 16:
            maxNumColors = 16
        else:
            maxNumColors = 256

    if numColors > maxNumColors:
        raise RuntimeError(
            f"palette has too many entries ({numColors} > {maxNumColors})"
        )

    clut, has_translucency = convertRGBAto16(
        numpy.frombuffer(clutData, "B").reshape(( 1, numColors, colorDepth ))
    )

    # Pad the palette to 16 or 256 colors.
    padAmount: int = maxNumColors - numColors
    if padAmount:
        clut = numpy.c_[ clut, numpy.zeros(( 1, padAmount ), "<H") ]

    image: ndarray = numpy.asarray(imageObj, "B")
    if image.shape[1] % 2:
        image = numpy.c_[ image, numpy.zeros((image.shape[0], 1 ), "B") ]

    # Pack two pixels into each byte for 4bpp images.
    if maxNumColors <= 16:
        image = image[:, 0::2] | (image[:, 1::2] << 4)

        if image.shape[1] % 2:
            image = numpy.c_[ image, numpy.zeros(( image.shape[0], 1 ), "B") ]

    return image, clut, has_translucency

## Main

def createParser() -> ArgumentParser:
    parser = ArgumentParser(
        description = \
            "Converts an image file into raw 16bpp image data, or 4bpp or 8bpp "
            "indexed color data plus a 16bpp palette.",
        add_help    = False
    )

    group = parser.add_argument_group("Tool options")
    group.add_argument(
        "-h", "--help",
        action = "help",
        help   = "Show this help message and exit"
    )

    group = parser.add_argument_group("Conversion options")
    group.add_argument(
        "-b", "--bpp",
        type    = int,
        choices = ( 4, 8, 16 ),
        default = 0,
        help    = \
            "Use specified color depth (4/8bpp indexed or 16bpp raw, default "
            "autodetect, fallback to 16bpp)",
        metavar = "4|8|16"
    )
    group.add_argument(
        "-q", "--quantize",
        type    = int,
        help    = \
            "Quantize image with the given maximum number of colors before "
            "converting it",
        metavar = "colors"
    )
    def receive_poimg_arg(s: str):
        parts = s.split(":")
        assert len(parts) == 4
        return tuple(int(x) for x in parts)
    group.add_argument(
        "-p", "--poimg",
        type    = receive_poimg_arg,
        default = None,
        help    = \
            "Output in poimg format for poeng, specifying vram location [x:y:clutx:cluty]"
    )
    group.add_argument(
        "-s", "--sm64",
        action = "store_true",
        help    = \
            "Output in format for sm64-psx"
    )

    group = parser.add_argument_group("File paths")
    group.add_argument(
        "input",
        type = Image.open,
        help = "Path to input image file"
    )
    group.add_argument(
        "imageOutput",
        type = FileType("wb"),
        help = "Path to raw image data file to generate"
    )
    group.add_argument(
        "clutOutput",
        type  = FileType("wb"),
        nargs = "?",
        help  = "Path to raw palette data file to generate"
    )

    return parser

def main(args = None):
    parser: ArgumentParser = createParser()
    args:   Namespace      = parser.parse_args(args)

    logging.basicConfig(
        format = "{levelname}: {message}",
        style  = "{",
        level  = logging.INFO
    )

    img_rotated = False

    with args.input as image:
        image.load()

        if image.mode != "RGBA":
            image = image.convert("RGBA")

        if image.width % 2 != 0:
            image = image.transform((image.width + 1, image.height), Image.Transform.EXTENT, (0, 0, image.width, image.height))
        if image.width == 32 and image.height == 64:
            w, h = image.size
            rotated_pixels = []
            for x in range(0, image.width):
                for y in range(0, image.height):
                    rotated_pixels.append(image.getpixel((x, y)))
            newImage = Image.new(image.mode, (h, w))
            i = 0
            for y in range(0, newImage.height):
                for x in range(0, newImage.width):
                    newImage.putpixel((x, y), rotated_pixels[i])
                    i += 1
            image = newImage
            img_rotated = True
        #elif inputImage.width == 64 and inputImage.height == 64:
            #inputImage = inputImage.resize((32, 32), Image.Resampling.NEAREST)

        for y in range(0, image.height):
            for x in range(0, image.width):
                r, g, b, a = image.getpixel((x, y))
                if a < LOWER_ALPHA_BOUND:
                    image.putpixel((x, y), (0, 0, 0, 0))
                else:
                    r = min((r + 4) >> 3, 31) << 3
                    g = min((g + 4) >> 3, 31) << 3
                    b = min((b + 4) >> 3, 31) << 3
                    if a >= UPPER_ALPHA_BOUND:
                        a = 255
                    else:
                        a = 127
                    image.putpixel((x, y), (r, g, b, a))

        match image.mode, args.bpp:
            case "P", 0 | 4 | 8:
                if args.quantize is not None:
                    logging.warning("requantizing indexed color image")

                    image = imagequant.quantize_pil_image(image, dithering_level = 0, max_colors = args.quantize)
                else:
                    image = image

            case _, 4 | 8:
                numColors: int = args.quantize or (2 ** args.bpp)
                logging.info(f"quantizing image down to {numColors} colors")

                image = imagequant.quantize_pil_image(image, dithering_level = 0, max_colors = numColors)

            case "P", 16:
                if args.quantize is not None:
                    parser.error("--quantize is only valid in 4/8bpp mode")

                logging.warning("converting indexed color image back to RGBA")

                image = image.convert("RGBA")

            case _, 16:
                image = image.convert("RGBA")

            case _, 0:
                args.bpp = 16
                image = image.convert("RGBA")

    has_translucency = False
    if image.mode == "P":
        imageData, clutData, has_translucency = convertIndexedImage(image, 2 ** args.bpp)
    else:
        imageData, has_translucency = convertRGBAto16(numpy.asarray(image))
        clutData = None

    if args.clutOutput is not None:
        parser.error("path to palette data cannot be specified when outputting for sm64-psx")

    if image.width % 2:
        raise ValueError(f"texture width {image.width} is not divisible by 2")
    elif image.height % 2:
        raise ValueError(f"texture height {image.height} is not divisible by 2")
    arr = bytearray()
    arr += image.width.to_bytes(2, "little")
    arr += image.height.to_bytes(2, "little")
    arr += (1 if img_rotated else 0).to_bytes(1)
    arr += (1 if has_translucency else 0).to_bytes(1)
    arr += b"\0\0\0\0\0\0\0\0\0\0"
    if clutData is not None:
        arr += clutData.tobytes()
    arr += imageData.tobytes()
    with args.imageOutput as _file:
        _file.write(arr)

if __name__ == "__main__":
    main()
