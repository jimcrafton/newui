"""ScreenShot: a screenshot from the app's `screenshot` command (a BMP of a RootView), read into memory without any imaging library."""
import struct


def differs(a, b, tolerance=40):
    """True when two (r, g, b) pixels are further apart than `tolerance` (sum of the channel differences)."""
    return sum(abs(x - y) for x, y in zip(a, b)) > tolerance


def dark(pixel, limit=240):
    """True for a pixel whose channels add up to less than `limit` (black text on white, say)."""
    return sum(pixel) < limit


class ScreenShot:
    """A screenshot (BMP) held in memory. pixel() and count() take SCREEN coordinates: `origin` is where the root view's top-left
    corner is on screen (App.screenshot() fills it in), so a point found with App.find() can be probed directly."""

    WHITE = (255, 255, 255)

    def __init__(self, data, origin=(0, 0)):
        if len(data) < 54 or data[:2] != b"BM":
            raise ValueError("not a BMP file (%d bytes)" % len(data))
        self._data = data
        self.origin = origin
        self._offset = struct.unpack_from("<I", data, 10)[0]
        self.width, height, _planes, bpp = struct.unpack_from("<iiHH", data, 18)
        if bpp not in (24, 32):
            raise ValueError("unsupported BMP depth: %d bits" % bpp)
        self._bottom_up = height > 0
        self.height = abs(height)
        self._bytes = bpp // 8
        self._stride = ((self.width * bpp + 31) // 32) * 4

    @classmethod
    def from_file(cls, path, origin=(0, 0)):
        with open(path, "rb") as f:
            return cls(f.read(), origin)

    def pixel(self, x, y):
        """(r, g, b) at screen point (x, y); white outside the picture."""
        x, y = int(x) - self.origin[0], int(y) - self.origin[1]
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return self.WHITE
        row = (self.height - 1 - y) if self._bottom_up else y
        i = self._offset + row * self._stride + x * self._bytes
        d = self._data
        return (d[i + 2], d[i + 1], d[i])

    def count(self, x0, y0, x1, y1, test, step=2):
        """How many pixels of the screen rectangle [x0, x1) x [y0, y1), sampled every `step`, satisfy test(pixel)."""
        n = 0
        for y in range(int(y0), int(y1), step):
            for x in range(int(x0), int(x1), step):
                if test(self.pixel(x, y)):
                    n += 1
        return n

    def changed(self, other, x0, y0, x1, y1, tolerance=40, step=3):
        """How many sampled pixels of the rectangle differ between this picture and `other`."""
        n = 0
        for y in range(int(y0), int(y1), step):
            for x in range(int(x0), int(x1), step):
                if differs(self.pixel(x, y), other.pixel(x, y), tolerance):
                    n += 1
        return n
