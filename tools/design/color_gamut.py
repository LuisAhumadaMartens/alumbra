import math

COLOR_SPACES = ("srgb", "display-p3", "rec2020")
PRECISION = 5
JND = 0.02
EPSILON = 0.0001

LINEAR_TO_XYZ = {
    "srgb": (
        (506752 / 1228815, 87881 / 245763, 12673 / 70218),
        (87098 / 409605, 175762 / 245763, 12673 / 175545),
        (7918 / 409605, 87881 / 737289, 1001167 / 1053270),
    ),
    "display-p3": (
        (608311 / 1250200, 189793 / 714400, 198249 / 1000160),
        (35783 / 156275, 247089 / 357200, 198249 / 2500400),
        (0.0, 32229 / 714400, 5220557 / 5000800),
    ),
    "rec2020": (
        (63426534 / 99577255, 20160776 / 139408157, 47086771 / 278816314),
        (26158966 / 99577255, 472592308 / 697040785, 8267143 / 139408157),
        (0.0, 19567812 / 697040785, 295819943 / 278816314),
    ),
}
XYZ_TO_LINEAR_SRGB = (
    (12831 / 3959, -329 / 214, -1974 / 3959),
    (-851781 / 878810, 1648619 / 878810, 36519 / 878810),
    (705 / 12673, -2585 / 12673, 705 / 667),
)
XYZ_TO_LMS = (
    (0.8190224379967030, 0.3619062600528904, -0.1288737815209879),
    (0.0329836539323885, 0.9292868615863434, 0.0361446663506424),
    (0.0481771893596242, 0.2642395317527308, 0.6335478284694309),
)
LMS_TO_OKLAB = (
    (0.2104542683093140, 0.7936177747023054, -0.0040720430116193),
    (1.9779985324311684, -2.4285922420485799, 0.4505937096174110),
    (0.0259040424655478, 0.7827717124575296, -0.8086757549230774),
)
OKLAB_TO_LMS = (
    (1.0, 0.3963377773761749, 0.2158037573099136),
    (1.0, -0.1055613458156586, -0.0638541728258133),
    (1.0, -0.0894841775298119, -1.2914855480194092),
)
LMS_TO_XYZ = (
    (1.2268798758459243, -0.5578149944602171, 0.2813910456659647),
    (-0.0405757452148008, 1.1122868032803170, -0.0717110580655164),
    (-0.0763729366746601, -0.4214933324022432, 1.5869240198367816),
)
REC2020_ALPHA = 1.09929682680944
REC2020_BETA = 0.018053968510807


def multiply(matrix, vector):
    return tuple(sum(row[index] * vector[index] for index in range(3)) for row in matrix)


def cube_root(value):
    return math.copysign(abs(value) ** (1 / 3), value)


def srgb_decode(value):
    magnitude = abs(value)
    if magnitude <= 0.04045:
        return value / 12.92
    return math.copysign(((magnitude + 0.055) / 1.055) ** 2.4, value)


def srgb_encode(value):
    magnitude = abs(value)
    if magnitude <= 0.0031308:
        return value * 12.92
    return math.copysign(1.055 * magnitude ** (1 / 2.4) - 0.055, value)


def rec2020_decode(value):
    magnitude = abs(value)
    if magnitude < REC2020_BETA * 4.5:
        return value / 4.5
    return math.copysign(
        ((magnitude + REC2020_ALPHA - 1) / REC2020_ALPHA) ** (1 / 0.45), value)


def to_xyz(color_space, components):
    if color_space not in COLOR_SPACES:
        raise ValueError(f"unsupported color space {color_space!r}")
    decode = rec2020_decode if color_space == "rec2020" else srgb_decode
    return multiply(LINEAR_TO_XYZ[color_space], [decode(part) for part in components])


def xyz_to_srgb(xyz):
    return tuple(srgb_encode(part) for part in multiply(XYZ_TO_LINEAR_SRGB, xyz))


def xyz_to_oklch(xyz):
    lms = [cube_root(part) for part in multiply(XYZ_TO_LMS, xyz)]
    lightness, a, b = multiply(LMS_TO_OKLAB, lms)
    return lightness, math.hypot(a, b), math.atan2(b, a)


def oklch_to_xyz(lightness, chroma, hue):
    lms = multiply(OKLAB_TO_LMS, (lightness, chroma * math.cos(hue), chroma * math.sin(hue)))
    return multiply(LMS_TO_XYZ, [part ** 3 for part in lms])


def delta_eok(first, second):
    def lab(xyz):
        lightness, chroma, hue = xyz_to_oklch(xyz)
        return lightness, chroma * math.cos(hue), chroma * math.sin(hue)

    return math.dist(lab(first), lab(second))


def in_gamut(rgb, tolerance=0.000075):
    return all(-tolerance <= part <= 1 + tolerance for part in rgb)


def clip(rgb):
    return tuple(min(1.0, max(0.0, part)) for part in rgb)


def srgb_to_xyz(rgb):
    return to_xyz("srgb", rgb)


def gamut_map(xyz):
    lightness, chroma, hue = xyz_to_oklch(xyz)
    if lightness >= 1:
        return (1.0, 1.0, 1.0)
    if lightness <= 0:
        return (0.0, 0.0, 0.0)
    rgb = xyz_to_srgb(xyz)
    if in_gamut(rgb):
        return clip(rgb)
    clipped = clip(rgb)
    if delta_eok(srgb_to_xyz(clipped), xyz) < JND:
        return clipped
    minimum, maximum, minimum_in_gamut = 0.0, chroma, True
    while maximum - minimum > EPSILON:
        middle = (minimum + maximum) / 2
        current = oklch_to_xyz(lightness, middle, hue)
        candidate = xyz_to_srgb(current)
        if minimum_in_gamut and in_gamut(candidate):
            minimum = middle
            continue
        clipped = clip(candidate)
        error = delta_eok(srgb_to_xyz(clipped), current)
        if error < JND:
            if JND - error < EPSILON:
                return clipped
            minimum_in_gamut = False
            minimum = middle
        else:
            maximum = middle
    return clipped


def srgb_fallback(color_space, components):
    if color_space == "srgb":
        return tuple(float(part) + 0.0 for part in components)
    mapped = gamut_map(to_xyz(color_space, [float(part) for part in components]))
    return tuple(round(min(1.0, max(0.0, part)), PRECISION) + 0.0 for part in mapped)
