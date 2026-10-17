"""Key names for App.key(): "ctrl+shift+z", "enter", "f5", "delete" ... -> Windows virtual-key codes."""

VK = {
    "backspace": 0x08, "tab": 0x09, "enter": 0x0D, "return": 0x0D, "shift": 0x10, "ctrl": 0x11, "control": 0x11,
    "alt": 0x12, "menu": 0x12, "pause": 0x13, "esc": 0x1B, "escape": 0x1B, "space": 0x20,
    "pageup": 0x21, "pagedown": 0x22, "end": 0x23, "home": 0x24, "left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28,
    "insert": 0x2D, "delete": 0x2E, "del": 0x2E,
}
for _i in range(26):
    VK[chr(ord("a") + _i)] = ord("A") + _i
for _i in range(10):
    VK[str(_i)] = ord("0") + _i
for _i in range(1, 13):
    VK["f%d" % _i] = 0x6F + _i

# keys that need KEYEVENTF_EXTENDEDKEY, or the arrow keys arrive as numpad keys
EXTENDED = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E}


def parse_chord(chord):
    """"ctrl+shift+z" -> ([VK_CONTROL, VK_SHIFT], VK_Z). An int is a bare virtual-key code. Raises ValueError."""
    if isinstance(chord, int):
        return [], chord
    parts = [p.strip().lower() for p in chord.split("+") if p.strip()]
    if not parts:
        raise ValueError("empty key chord")
    for part in parts:
        if part not in VK:
            raise ValueError("unknown key name %r in %r" % (part, chord))
    # every part but the last is held down; the last is the key pressed (it may itself be a modifier: "shift")
    return [VK[p] for p in parts[:-1]], VK[parts[-1]]
