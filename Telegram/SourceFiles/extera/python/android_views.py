import json
import threading

import _compat
import _extera


# State

_ids = [0]
_bitmaps = [0]
_lock = threading.RLock()
_views = {}
_sheets = {}
_metrics = None
_metrics_lock = threading.Lock()


def _nid():
    with _lock:
        _ids[0] += 1
        return _ids[0]


def _native(name):
    return getattr(_extera, name, None)


def _send_update(op):
    native = _native("views_update")
    if native is None:
        return
    try:
        native(json.dumps(op, ensure_ascii=False))
    except Exception:
        pass


def _metrics_data():
    global _metrics
    with _metrics_lock:
        if _metrics is None:
            native = _native("ui_metrics")
            if native is None:
                _metrics = {"dp": 2.0, "width": 1080, "height": 1920}
            else:
                try:
                    _metrics = json.loads(native())
                except Exception:
                    _metrics = {"dp": 2.0, "width": 1080, "height": 1920}
        return _metrics


class _DisplaySize:
    @property
    def x(self):
        return int(_metrics_data().get("width", 1080))

    @property
    def y(self):
        return int(_metrics_data().get("height", 1920))


displaySize = _DisplaySize()


def dp(value):
    try:
        return int(round(float(value) * float(_metrics_data().get("dp", 2.0))))
    except Exception:
        return int(value)


def density():
    try:
        return float(_metrics_data().get("dp", 2.0))
    except Exception:
        return 2.0


def theme_color(name):
    native = _native("theme_color")
    if native is None:
        return 0
    try:
        return int(native(str(name)))
    except Exception:
        return 0


_owners = {}


def _scope(view_id):
    import extera_host
    return extera_host.owner_scope(_owners.get(view_id))


def dispatch_click(view_id):
    with _lock:
        view = _views.get(view_id)
    if view is None:
        return False
    from android_utils import safe_call
    with _scope(view_id):
        safe_call(view._click)
    return True


def dispatch_text_changed(view_id, text):
    with _lock:
        view = _views.get(view_id)
    if view is None:
        return False
    from android_utils import safe_call
    with _scope(view_id):
        for watcher in getattr(view, "_watchers", []):
            safe_call(getattr(watcher, "afterTextChanged", None), str(text))
    return True


def handle_sheet_closed(sheet_id):
    _sheets.pop(sheet_id, None)
    with _lock:
        for view in list(_views.values()):
            if view._sheet == sheet_id:
                view._sheet = None
                view._live = False
    return True


# Drawables

class GradientDrawable:
    RECTANGLE = 0
    OVAL = 1
    LINE = 2
    RING = 3
    LINEAR_GRADIENT = 0
    RADIAL_GRADIENT = 1
    SWEEP_GRADIENT = 2

    class Orientation:
        TOP_BOTTOM = "TOP_BOTTOM"
        TR_BL = "TR_BL"
        RIGHT_LEFT = "RIGHT_LEFT"
        BR_TL = "BR_TL"
        BOTTOM_TOP = "BOTTOM_TOP"
        BL_TR = "BL_TR"
        LEFT_RIGHT = "LEFT_RIGHT"
        TL_BR = "TL_BR"

    def __init__(self, orientation=None, colors=None):
        self.kind = "rounded"
        self.radius = 0
        self.color = None
        self.gradient = list(colors) if colors else None
        self.orientation = orientation
        self.stroke = None

    def setCornerRadius(self, radius):
        self.radius = float(radius)
        return self

    def setShape(self, shape):
        self.shape = shape
        return self

    def __getattr__(self, item):
        if item.startswith(("set", "mutate", "invalidate")):
            return lambda *args, **kwargs: self
        raise AttributeError(item)

    def setColor(self, color):
        self.color = int(color) if color is not None else None
        return self

    def setColors(self, colors):
        self.gradient = list(colors) if colors else None
        return self

    def setOrientation(self, orientation):
        self.orientation = orientation
        return self

    def setStroke(self, width, color):
        self.stroke = [float(width), int(color)]
        return self

    def _data(self):
        data = {"kind": "rounded", "radius": self.radius}
        if self.color is not None:
            data["color"] = self.color
        if self.gradient:
            data["gradient"] = [int(c) for c in self.gradient]
        if self.stroke:
            data["stroke"] = self.stroke
        return data


class ColorDrawable:
    def __init__(self, color):
        self.color = int(color)

    def _data(self):
        return {"kind": "flat", "color": self.color}


class BitmapDrawable:
    def __init__(self, resources=None, bitmap=None):
        self.bitmap = bitmap

    def _data(self):
        if self.bitmap is not None:
            self.bitmap._render()
            return {"kind": "bitmap", "bitmap": self.bitmap._id}
        return {"kind": "flat", "color": 0}


class Drawable:
    pass


class LayerDrawable:
    def __init__(self, layers=None):
        self.layers = list(layers or [])

    def setLayerInset(self, index, left, top, right, bottom):
        return self

    def _data(self):
        base = self.layers[0]._data() if self.layers else {"kind": "flat", "color": 0}
        return base


# Graphics

class Typeface(metaclass=_compat.LenientMeta):
    NORMAL = 0
    BOLD = 1
    ITALIC = 2
    BOLD_ITALIC = 3
    DEFAULT = 0
    DEFAULT_BOLD = 1
    MONOSPACE = 2
    SANS_SERIF = 3
    SERIF = 4


class Rect:
    _java_name = "android.graphics.Rect"

    def __init__(self, left=0, top=0, right=0, bottom=0):
        if isinstance(left, (Rect, RectF)):
            other = left
            self.left = int(other.left)
            self.top = int(other.top)
            self.right = int(other.right)
            self.bottom = int(other.bottom)
        else:
            self.left = int(left)
            self.top = int(top)
            self.right = int(right)
            self.bottom = int(bottom)

    @property
    def width(self):
        return max(0, self.right - self.left)

    @property
    def height(self):
        return max(0, self.bottom - self.top)

    def set(self, left, top, right, bottom):
        self.left = int(left)
        self.top = int(top)
        self.right = int(right)
        self.bottom = int(bottom)

    def offset(self, dx, dy):
        self.left += int(dx)
        self.top += int(dy)
        self.right += int(dx)
        self.bottom += int(dy)

    def contains(self, x, y):
        return self.left <= x < self.right and self.top <= y < self.bottom

    def isEmpty(self):
        return self.left >= self.right or self.top >= self.bottom

    def exactCenterX(self):
        return (self.left + self.right) * 0.5

    def exactCenterY(self):
        return (self.top + self.bottom) * 0.5

    def __repr__(self):
        return f"Rect({self.left}, {self.top}, {self.right}, {self.bottom})"


class RectF:
    _java_name = "android.graphics.RectF"

    def __init__(self, left=0, top=0, right=0, bottom=0):
        if isinstance(left, (Rect, RectF)):
            other = left
            self.left = float(other.left)
            self.top = float(other.top)
            self.right = float(other.right)
            self.bottom = float(other.bottom)
        else:
            self.left = float(left)
            self.top = float(top)
            self.right = float(right)
            self.bottom = float(bottom)

    @property
    def width(self):
        return max(0.0, self.right - self.left)

    @property
    def height(self):
        return max(0.0, self.bottom - self.top)

    def set(self, left, top, right, bottom):
        self.left = float(left)
        self.top = float(top)
        self.right = float(right)
        self.bottom = float(bottom)

    def offset(self, dx, dy):
        self.left += float(dx)
        self.top += float(dy)
        self.right += float(dx)
        self.bottom += float(dy)

    def contains(self, x, y):
        return self.left <= x < self.right and self.top <= y < self.bottom

    def isEmpty(self):
        return self.left >= self.right or self.top >= self.bottom

    def round(self, out_rect=None):
        out = out_rect if out_rect is not None else Rect()
        out.set(int(round(self.left)), int(round(self.top)),
                int(round(self.right)), int(round(self.bottom)))
        return out

    def roundOut(self, out_rect=None):
        import math
        out = out_rect if out_rect is not None else Rect()
        out.set(int(math.floor(self.left)), int(math.floor(self.top)),
                int(math.ceil(self.right)), int(math.ceil(self.bottom)))
        return out

    def __repr__(self):
        return f"RectF({self.left}, {self.top}, {self.right}, {self.bottom})"


class MeasureSpec:
    _java_name = "android.view.View$MeasureSpec"

    UNSPECIFIED = 0 << 30
    EXACTLY = 1 << 30
    AT_MOST = 2 << 30
    MODE_MASK = 0x3 << 30
    MODE_SHIFT = 30

    @staticmethod
    def makeMeasureSpec(size, mode):
        return (int(size) & ~MeasureSpec.MODE_MASK) | (int(mode) & MeasureSpec.MODE_MASK)

    @staticmethod
    def getMode(spec):
        return int(spec) & MeasureSpec.MODE_MASK

    @staticmethod
    def getSize(spec):
        return int(spec) & ~MeasureSpec.MODE_MASK

    @staticmethod
    def toString(spec):
        mode = MeasureSpec.getMode(spec)
        size = MeasureSpec.getSize(spec)
        name = "EXACTLY" if mode == MeasureSpec.EXACTLY else "AT_MOST" if mode == MeasureSpec.AT_MOST else "UNSPECIFIED"
        return f"MeasureSpec: {name} {size}"


class GeometryTranslator:
    """Bridges Android layout/drawing coordinates with Desktop Linux/Windows/macOS."""

    @staticmethod
    def dp_to_desktop(dp_value, dp_scale=None, scale=None):
        effective_scale = dp_scale if dp_scale is not None else (scale if scale is not None else density())
        try:
            return int(round(float(dp_value) * float(effective_scale)))
        except Exception:
            return int(dp_value)

    @staticmethod
    def desktop_to_dp(px_value, dp_scale=None, scale=None):
        effective_scale = dp_scale if dp_scale is not None else (scale if scale is not None else density())
        try:
            return float(px_value) / (float(effective_scale) if float(effective_scale) > 0 else 1.0)
        except Exception:
            return float(px_value)

    @staticmethod
    def android_to_desktop_bounds(left, top, right, bottom):
        x = int(left)
        y = int(top)
        width = max(0, int(right - left))
        height = max(0, int(bottom - top))
        return x, y, width, height

    @staticmethod
    def desktop_to_android_bounds(x, y, width, height):
        left = int(x)
        top = int(y)
        right = int(x + width)
        bottom = int(y + height)
        return left, top, right, bottom

    @staticmethod
    def make_measure_spec(size, mode=MeasureSpec.EXACTLY):
        return MeasureSpec.makeMeasureSpec(int(size), mode)

    @staticmethod
    def get_size(spec):
        return MeasureSpec.getSize(spec)

    @staticmethod
    def get_mode(spec):
        return MeasureSpec.getMode(spec)

    @staticmethod
    def resolve_size(size, measure_spec):
        result = int(size)
        spec_mode = MeasureSpec.getMode(measure_spec)
        spec_size = MeasureSpec.getSize(measure_spec)
        if spec_mode == MeasureSpec.EXACTLY:
            result = spec_size
        elif spec_mode == MeasureSpec.AT_MOST:
            result = min(result, spec_size)
        return result


class Paint(metaclass=_compat.LenientMeta):
    _java_name = "android.graphics.Paint"
    ANTI_ALIAS_FLAG = 1

    def __getattr__(self, item):
        if item.startswith("__"):
            raise AttributeError(item)
        return lambda *args, **kwargs: self

    class Style:
        FILL = 0
        STROKE = 1
        FILL_AND_STROKE = 2

    class Align:
        LEFT = 0
        CENTER = 1
        RIGHT = 2

    def __init__(self, flags=0):
        self.style = Paint.Style.FILL
        self.color = 0xFF000000
        self.stroke_width = 1.0
        self.text_size = 16.0
        self.text_align = Paint.Align.LEFT
        self.typeface = Typeface.DEFAULT
        self.alpha = 255

    def setStyle(self, style):
        self.style = style
        return self

    def getStyle(self):
        return self.style

    def setColor(self, color):
        self.color = int(color)
        self.alpha = (self.color >> 24) & 0xFF
        return self

    def getColor(self):
        return self.color

    def setAlpha(self, alpha):
        self.alpha = max(0, min(255, int(alpha)))
        self.color = (self.color & 0x00FFFFFF) | (self.alpha << 24)
        return self

    def getAlpha(self):
        return self.alpha

    def setStrokeWidth(self, width):
        self.stroke_width = float(width)
        return self

    def getStrokeWidth(self):
        return self.stroke_width

    def setTextSize(self, size):
        self.text_size = float(size)
        return self

    def getTextSize(self):
        return self.text_size

    def setTextAlign(self, align):
        self.text_align = align
        return self

    def setTypeface(self, typeface):
        self.typeface = typeface if typeface is not None else Typeface.DEFAULT
        return self

    def measureText(self, text):
        native = _native("measure_text")
        if native is None:
            return float(len(str(text)) * self.text_size * 0.55)
        try:
            return float(native(str(text), int(self.text_size),
                                int(self.typeface) == Typeface.DEFAULT_BOLD))
        except Exception:
            return float(len(str(text)) * self.text_size * 0.55)

    def _data(self):
        return {
            "style": int(self.style),
            "color": int(self.color),
            "stroke_width": self.stroke_width,
            "text_size": self.text_size,
            "text_align": int(self.text_align),
            "bold": int(self.typeface) == Typeface.DEFAULT_BOLD,
        }


class Bitmap:
    _java_name = "android.graphics.Bitmap"

    class Config:
        ARGB_8888 = 0
        RGB_565 = 1
        ALPHA_8 = 2

    def __init__(self, width, height, config=None):
        self._id = None
        self.width = int(width)
        self.height = int(height)
        self.ops = []
        self._rendered = False

    @staticmethod
    def createBitmap(width, height, config=None):
        return Bitmap(width, height, config)

    def getWidth(self):
        return self.width

    def getHeight(self):
        return self.height

    def _render(self):
        if self._rendered:
            return
        self._rendered = True
        with _lock:
            _bitmaps[0] += 1
            self._id = _bitmaps[0]
        native = _native("views_render_canvas")
        if native is None:
            return
        try:
            native(self._id, self.width, self.height,
                   json.dumps(self.ops, ensure_ascii=False))
        except Exception:
            pass


class Canvas:
    _java_name = "android.graphics.Canvas"

    def __init__(self, bitmap=None):
        if bitmap is None:
            bitmap = Bitmap(1080, 1920)
        self._bitmap = bitmap
        self._paint = None
        self._save_count = 1

    @property
    def _ops(self):
        return getattr(self._bitmap, "ops", [])

    @property
    def ops(self):
        return getattr(self._bitmap, "ops", [])

    def _op(self, data):
        data["paint"] = self._paint._data() if getattr(self, "_paint", None) else None
        self._bitmap.ops.append(data)

    def _draw(self, paint):
        self._paint = paint

    def getWidth(self):
        return getattr(self._bitmap, "width", 0)

    def getHeight(self):
        return getattr(self._bitmap, "height", 0)

    def save(self):
        self._save_count += 1
        self._op({"op": "save"})
        return self._save_count - 1

    def restore(self):
        if self._save_count > 1:
            self._save_count -= 1
            self._op({"op": "restore"})

    def getSaveCount(self):
        return self._save_count

    def translate(self, dx, dy):
        self._op({"op": "translate", "dx": float(dx), "dy": float(dy)})

    def scale(self, sx, sy, px=0.0, py=0.0):
        if px != 0.0 or py != 0.0:
            self.translate(px, py)
            self._op({"op": "scale", "sx": float(sx), "sy": float(sy)})
            self.translate(-px, -py)
        else:
            self._op({"op": "scale", "sx": float(sx), "sy": float(sy)})

    def rotate(self, degrees, px=0.0, py=0.0):
        if px != 0.0 or py != 0.0:
            self.translate(px, py)
            self._op({"op": "rotate", "deg": float(degrees)})
            self.translate(-px, -py)
        else:
            self._op({"op": "rotate", "deg": float(degrees)})

    def clipRect(self, *args):
        if len(args) == 1 and hasattr(args[0], "left"):
            rect = args[0]
            l, t, r, b = rect.left, rect.top, rect.right, rect.bottom
        elif len(args) >= 4:
            l, t, r, b = args[:4]
        else:
            return
        self._op({"op": "clip", "rect": [float(l), float(t), float(r), float(b)]})

    def drawColor(self, color):
        paint = Paint()
        paint.setColor(color)
        paint.setStyle(Paint.Style.FILL)
        self._draw(paint)
        w = float(self.getWidth() or 1080)
        h = float(self.getHeight() or 1920)
        self._op({"op": "rect", "rect": [0.0, 0.0, w, h]})

    def drawRoundRect(self, *args):
        if len(args) == 4:
            rect, rx, ry, paint = args
            self._draw(paint)
            self._op({"op": "rrect", "rect": [float(rect.left), float(rect.top), float(rect.right), float(rect.bottom)],
                      "rx": float(rx), "ry": float(ry)})
        elif len(args) >= 7:
            l, t, r, b, rx, ry, paint = args[:7]
            self._draw(paint)
            self._op({"op": "rrect", "rect": [float(l), float(t), float(r), float(b)],
                      "rx": float(rx), "ry": float(ry)})

    def drawRect(self, *args):
        if len(args) == 2:
            rect, paint = args
            self._draw(paint)
            self._op({"op": "rect", "rect": [float(rect.left), float(rect.top), float(rect.right), float(rect.bottom)]})
        elif len(args) >= 5:
            l, t, r, b, paint = args[:5]
            self._draw(paint)
            self._op({"op": "rect", "rect": [float(l), float(t), float(r), float(b)]})

    def drawCircle(self, cx, cy, radius, paint):
        self._draw(paint)
        self._op({"op": "circle", "cx": float(cx), "cy": float(cy), "radius": float(radius)})

    def drawLine(self, x1, y1, x2, y2, paint):
        self._draw(paint)
        self._op({"op": "line", "from": [float(x1), float(y1)], "to": [float(x2), float(y2)]})

    def drawArc(self, rect_or_coords, start, sweep, use_center, paint):
        self._draw(paint)
        if hasattr(rect_or_coords, "left"):
            rect = rect_or_coords
            l, t, r, b = rect.left, rect.top, rect.right, rect.bottom
        else:
            l, t, r, b = rect_or_coords
        self._op({"op": "arc", "rect": [float(l), float(t), float(r), float(b)],
                  "start": float(start), "sweep": float(sweep)})

    def drawText(self, text, x, y, paint):
        self._draw(paint)
        self._op({"op": "text", "text": str(text), "x": float(x), "y": float(y)})

    def drawBitmap(self, bitmap, left, top, paint=None):
        if paint:
            self._draw(paint)
        bitmap._render()
        self._op({"op": "bitmap", "bitmap": bitmap._id,
                  "x": float(left), "y": float(top)})


# Views

class _UiClassMeta(type):
    def __getattr__(cls, item):
        if item.startswith("__"):
            raise AttributeError(item)
        created = _compat.LenientMeta(item, (), {})
        setattr(cls, item, created)
        return created


class _ThemeMeta(type):
    def __getattr__(cls, item):
        if item.startswith("key_"):
            return "windowFg"
        if item.startswith("__"):
            raise AttributeError(item)
        return _compat.Stub("Theme." + item)


class Gravity:
    LEFT = 3
    TOP = 48
    RIGHT = 5
    BOTTOM = 80
    CENTER = 17
    CENTER_VERTICAL = 16
    CENTER_HORIZONTAL = 1
    NO_GRAVITY = 0
    START = 8388611
    END = 8388613
    FILL = 119
    FILL_HORIZONTAL = 7
    FILL_VERTICAL = 112


class LayoutParams:
    MATCH_PARENT = -1
    WRAP_CONTENT = -2

    def __init__(self, width=-2, height=-2, gravity=None):
        self.width = width
        self.height = height
        self.gravity = gravity
        self.margins = [0, 0, 0, 0]

    def setMargins(self, left, top, right, bottom):
        self.margins = [int(left), int(top), int(right), int(bottom)]
        return self


class LayoutHelper:
    MATCH_PARENT = -1
    WRAP_CONTENT = -2

    @staticmethod
    def _size(value):
        value = float(value or 0)
        return int(value) if value < 0 else int(dp(value))

    @staticmethod
    def _build(width, height, gravity, margins):
        params = LayoutParams(LayoutHelper._size(width), LayoutHelper._size(height), gravity)
        if margins:
            values = [int(dp(float(m or 0))) for m in (list(margins) + [0, 0, 0, 0])[:4]]
            params.setMargins(*values)
        return params

    @staticmethod
    def createLinear(width=-1, height=-2, *args):
        args = list(args)
        weight = 0.0
        gravity = None
        if args and isinstance(args[0], float):
            weight = args.pop(0)
        if len(args) in (1, 5):
            gravity = int(args.pop(0))
        params = LayoutHelper._build(width, height, gravity, args)
        params.weight = weight
        return params

    @staticmethod
    def createFrame(width=-1, height=-2, *args):
        args = list(args)
        gravity = None
        if len(args) in (1, 5):
            gravity = int(args.pop(0))
        return LayoutHelper._build(width, height, gravity, args)

    @staticmethod
    def createRelative(width=-1, height=-2, *args):
        return LayoutHelper._build(width, height, None, list(args)[:4])

    @staticmethod
    def createScroll(width=-1, height=-2, gravity=None, *args):
        return LayoutHelper._build(width, height, gravity, list(args)[:4])

    createLinearRelatively = createLinear
    createFrameRelatively = createFrame


class ViewPropertyAnimator:
    def __init__(self, view):
        self._view = view
        self._alpha = None
        self._tx = None
        self._ty = None
        self._duration = None

    def alpha(self, value):
        self._alpha = float(value)
        return self

    def translationX(self, value):
        self._tx = float(value)
        return self

    def translationY(self, value):
        self._ty = float(value)
        return self

    def setDuration(self, ms):
        self._duration = int(ms)
        return self

    def setInterpolator(self, i):
        return self

    def withLayer(self):
        return self

    def start(self):
        self._view._apply({
            "op": "animate",
            "alpha": self._alpha,
            "tx": self._tx,
            "ty": self._ty,
            "duration": self._duration if self._duration is not None else 200,
        })

    def cancel(self):
        self._view._apply({"op": "animate_cancel"})


class View(metaclass=_UiClassMeta):
    _java_name = "android.view.View"
    VISIBLE = 0
    INVISIBLE = 4
    GONE = 8
    MeasureSpec = MeasureSpec

    def __getattr__(self, item):
        if item.startswith("__") or item.startswith("_"):
            raise AttributeError(item)
        return _compat.lenient(type(self).__name__, item)

    class OnClickListener:
        def onClick(self, view):
            pass

    @classmethod
    def getMethod(cls, name="", *args):
        import exd_java
        return exd_java.shim_method(cls, name) or exd_java._missing(f"{getattr(cls, '_java_name', cls.__name__)}.{name}")

    @classmethod
    def getDeclaredMethod(cls, name="", *args):
        import exd_java
        return exd_java.shim_method(cls, name) or exd_java._missing(f"{getattr(cls, '_java_name', cls.__name__)}.{name}")

    def __init__(self, context=None):
        self._id = _nid()
        try:
            import extera_host
            _owners[self._id] = extera_host.owner_now()
        except Exception:
            _owners[self._id] = "<unknown>"
        with _lock:
            _views[self._id] = self
        self._children = []
        self._parent = None
        self._click = None
        self._sheet = None
        self._live = False

        # Android & Desktop geometry state
        self._left = 0
        self._top = 0
        self._right = 0
        self._bottom = 0
        self._measured_width = 0
        self._measured_height = 0
        self._min_width = 0
        self._min_height = 0
        self._padding = [0, 0, 0, 0] # left, top, right, bottom
        self._layout_params = None

        self._props = {
            "type": "view",
            "visible": True,
            "visibility": 0,
            "padding": None,
            "bg": None,
        }

    def _set_prop(self, key, value, op=None):
        self._props[key] = value
        if op:
            op["id"] = self._id
            self._apply(op)

    def _apply(self, op):
        if self._live:
            op["id"] = self._id
            _send_update(op)

    def _data(self):
        data = dict(self._props)
        if data.get("type") == "view" and type(self).onDraw is not View.onDraw:
            canvas = Canvas()
            try:
                self.onDraw(canvas)
            except Exception:
                pass
            ops = getattr(canvas, "_ops", None) or (getattr(canvas._bitmap, "ops", []) if hasattr(canvas, "_bitmap") else [])
            if ops:
                data["ops"] = ops
                data["type"] = "canvas_view"
        return data

    # Geometry getters & setters
    def getLeft(self):
        return int(self._left)

    def getTop(self):
        return int(self._top)

    def getRight(self):
        return int(self._right)

    def getBottom(self):
        return int(self._bottom)

    def getWidth(self):
        return max(0, int(self._right - self._left))

    def getHeight(self):
        return max(0, int(self._bottom - self._top))

    def getX(self):
        return float(self.getLeft())

    def getY(self):
        return float(self.getTop())

    def getMeasuredWidth(self):
        return int(self._measured_width)

    def getMeasuredHeight(self):
        return int(self._measured_height)

    def setMeasuredDimension(self, measured_width, measured_height):
        self._measured_width = max(0, int(measured_width))
        self._measured_height = max(0, int(measured_height))

    def getSuggestedMinimumWidth(self):
        return int(self._min_width)

    def getSuggestedMinimumHeight(self):
        return int(self._min_height)

    def setMinimumWidth(self, min_width):
        self._min_width = int(min_width)

    def setMinimumHeight(self, min_height):
        self._min_height = int(min_height)

    def getPaddingLeft(self):
        return int(self._padding[0])

    def getPaddingTop(self):
        return int(self._padding[1])

    def getPaddingRight(self):
        return int(self._padding[2])

    def getPaddingBottom(self):
        return int(self._padding[3])

    def setPadding(self, left, top, right, bottom):
        self._padding = [int(left), int(top), int(right), int(bottom)]
        self._props["padding"] = list(self._padding)
        self._apply({"op": "padding", "value": self._props["padding"]})

    def getLayoutParams(self):
        return self._layout_params

    def setLayoutParams(self, params):
        self._layout_params = params
        if params is None:
            return
        gravity = getattr(params, "gravity", None)
        self._apply({"op": "params",
                     "w": int(getattr(params, "width", -2)),
                     "h": int(getattr(params, "height", -2)),
                     "gravity": int(gravity) if gravity is not None else None})

    def getLocationOnScreen(self, location):
        x, y = self.getLeft(), self.getTop()
        curr = self._parent
        while curr:
            x += curr.getLeft()
            y += curr.getTop()
            curr = curr._parent
        location[0] = int(x)
        location[1] = int(y)

    def getLocationInWindow(self, location):
        self.getLocationOnScreen(location)

    # Measurement lifecycle (hookable via Xposed)
    def measure(self, width_measure_spec, height_measure_spec):
        self.onMeasure(int(width_measure_spec), int(height_measure_spec))
        if self._measured_width == 0 and self._measured_height == 0:
            w = GeometryTranslator.resolve_size(self.getSuggestedMinimumWidth(), width_measure_spec)
            h = GeometryTranslator.resolve_size(self.getSuggestedMinimumHeight(), height_measure_spec)
            self.setMeasuredDimension(w, h)

    def onMeasure(self, width_measure_spec, height_measure_spec):
        min_w = max(self.getSuggestedMinimumWidth(), self.getPaddingLeft() + self.getPaddingRight())
        min_h = max(self.getSuggestedMinimumHeight(), self.getPaddingTop() + self.getPaddingBottom())
        w = GeometryTranslator.resolve_size(min_w, width_measure_spec)
        h = GeometryTranslator.resolve_size(min_h, height_measure_spec)
        self.setMeasuredDimension(w, h)

    # Layout lifecycle (hookable via Xposed)
    def layout(self, l, t, r, b):
        l, t, r, b = int(l), int(t), int(r), int(b)
        changed = (self._left != l or self._top != t or self._right != r or self._bottom != b)
        self._left = l
        self._top = t
        self._right = r
        self._bottom = b
        self.onLayout(changed, l, t, r, b)
        x, y, w, h = GeometryTranslator.android_to_desktop_bounds(l, t, r, b)
        if self._live:
            self._apply({"op": "geometry", "x": x, "y": y, "w": w, "h": h})

    def onLayout(self, changed, left, top, right, bottom):
        pass

    # Drawing lifecycle (hookable via Xposed)
    def draw(self, canvas):
        self.onDraw(canvas)
        if isinstance(self, ViewGroup):
            self.dispatchDraw(canvas)

    def onDraw(self, canvas):
        bg = self._props.get("bg")
        if bg and isinstance(bg, dict):
            w = self.getWidth() or self.getMeasuredWidth()
            h = self.getHeight() or self.getMeasuredHeight()
            color = bg.get("color")
            if color is not None:
                paint = Paint()
                paint.setColor(color)
                paint.setStyle(Paint.Style.FILL)
                radius = bg.get("radius", 0)
                if radius > 0:
                    canvas.drawRoundRect(0, 0, w, h, radius, radius, paint)
                else:
                    canvas.drawRect(0, 0, w, h, paint)

    def dispatchDraw(self, canvas):
        pass

    def invalidate(self):
        pass

    def postInvalidate(self):
        pass

    def requestLayout(self):
        pass

    def forceLayout(self):
        pass

    def setVisibility(self, visibility):
        self._set_prop("visibility", int(visibility), {"op": "visibility", "value": int(visibility)})

    def setAlpha(self, alpha):
        self._apply({"op": "alpha", "value": float(alpha)})

    def setTranslationX(self, value):
        self._apply({"op": "translation_x", "value": float(value)})

    def setTranslationY(self, value):
        self._apply({"op": "translation_y", "value": float(value)})

    def setOnTouchListener(self, listener):
        self._touch = listener

    def setClipChildren(self, value):
        pass

    def setBackgroundColor(self, color):
        self._props["bg"] = ColorDrawable(color)._data()
        self._apply({"op": "bg", "value": self._props["bg"]})

    def setBackground(self, drawable):
        data = drawable._data() if hasattr(drawable, "_data") else None
        self._props["bg"] = data
        self._apply({"op": "bg", "value": data})

    def setBackgroundDrawable(self, drawable):
        self.setBackground(drawable)

    def setOnClickListener(self, listener):
        self._click = listener
        self._props["clickable"] = True

    def animate(self):
        return ViewPropertyAnimator(self)

    def bringToFront(self):
        self._apply({"op": "bring_to_front"})

    def post(self, action, delay=0):
        from android_utils import run_on_ui_thread
        run_on_ui_thread(action, delay)
        return True

    def setTag(self, tag):
        self._tag = tag

    def getTag(self):
        return getattr(self, "_tag", None)

    def getId(self):
        return self._id

    def setId(self, view_id):
        pass

    def setEnabled(self, enabled):
        self._props["enabled"] = bool(enabled)
        self._apply({"op": "enabled", "value": bool(enabled)})

    def getContext(self):
        from android_utils import get_context
        return get_context()

    def getParent(self):
        return self._parent


class ViewGroup(View):
    _java_name = "android.view.ViewGroup"

    def __init__(self, context=None):
        super().__init__(context)
        self._props["type"] = "viewgroup"

    def setGravity(self, gravity):
        self._props["child_gravity"] = int(gravity)
        self._apply({"op": "child_gravity", "value": int(gravity)})

    def addView(self, child, params=None, index=None):
        if child is None:
            return
        if isinstance(params, int):
            index, params = params, None
        if child._parent is not None and child._parent is not self:
            child._parent.removeView(child)
        child._parent = self
        child._layout_params = params
        entry = {"view": child, "params": params}
        if index is None or index >= len(self._children):
            self._children.append(entry)
        else:
            self._children.insert(max(0, index), entry)
        if self._live:
            child._sheet = self._sheet
            child._live = True
            payload = {
                "op": "add_view",
                "parent": self._id,
                "index": self._children.index(entry),
                "view": _node(child, entry["params"]),
            }
            _send_update(payload)

    def removeView(self, child):
        for entry in list(self._children):
            if entry["view"] is child:
                self._children.remove(entry)
                child._parent = None
                child._live = False
                child._sheet = None
                self._apply_children()

    def removeAllViews(self):
        for entry in self._children:
            entry["view"]._parent = None
            entry["view"]._live = False
            entry["view"]._sheet = None
        self._children = []
        self._apply_children()

    def _apply_children(self):
        if self._live:
            _send_update({"op": "children", "id": self._id,
                          "children": [_child(e) for e in self._children]})

    def getChildAt(self, index):
        if 0 <= index < len(self._children):
            return self._children[index]["view"]
        return None

    def getChildCount(self):
        return len(self._children)

    def indexOfChild(self, child):
        for i, entry in enumerate(self._children):
            if entry["view"] is child:
                return i
        return -1

    # ViewGroup Measurement
    def measureChild(self, child, parent_width_spec, parent_height_spec):
        lp = getattr(child, "_layout_params", None)
        child_w = getattr(lp, "width", LayoutParams.WRAP_CONTENT) if lp else LayoutParams.WRAP_CONTENT
        child_h = getattr(lp, "height", LayoutParams.WRAP_CONTENT) if lp else LayoutParams.WRAP_CONTENT
        cw_spec = self.getChildMeasureSpec(parent_width_spec,
            self.getPaddingLeft() + self.getPaddingRight(), child_w)
        ch_spec = self.getChildMeasureSpec(parent_height_spec,
            self.getPaddingTop() + self.getPaddingBottom(), child_h)
        child.measure(cw_spec, ch_spec)

    def measureChildWithMargins(self, child, parent_width_spec, width_used, parent_height_spec, height_used):
        lp = getattr(child, "_layout_params", None)
        margins = getattr(lp, "margins", [0, 0, 0, 0]) or [0, 0, 0, 0]
        child_w = getattr(lp, "width", LayoutParams.WRAP_CONTENT) if lp else LayoutParams.WRAP_CONTENT
        child_h = getattr(lp, "height", LayoutParams.WRAP_CONTENT) if lp else LayoutParams.WRAP_CONTENT
        cw_spec = self.getChildMeasureSpec(parent_width_spec,
            self.getPaddingLeft() + self.getPaddingRight() + margins[0] + margins[2] + width_used, child_w)
        ch_spec = self.getChildMeasureSpec(parent_height_spec,
            self.getPaddingTop() + self.getPaddingBottom() + margins[1] + margins[3] + height_used, child_h)
        child.measure(cw_spec, ch_spec)

    def measureChildren(self, width_spec, height_spec):
        for entry in self._children:
            child = entry["view"] if isinstance(entry, dict) else entry
            if getattr(child, "_props", {}).get("visibility", 0) != View.GONE:
                self.measureChild(child, width_spec, height_spec)

    @staticmethod
    def getChildMeasureSpec(spec, padding, child_dimension):
        spec_mode = MeasureSpec.getMode(spec)
        spec_size = MeasureSpec.getSize(spec)
        size = max(0, spec_size - padding)

        result_size = 0
        result_mode = 0

        if spec_mode == MeasureSpec.EXACTLY:
            if child_dimension >= 0:
                result_size = child_dimension
                result_mode = MeasureSpec.EXACTLY
            elif child_dimension == LayoutParams.MATCH_PARENT:
                result_size = size
                result_mode = MeasureSpec.EXACTLY
            elif child_dimension == LayoutParams.WRAP_CONTENT:
                result_size = size
                result_mode = MeasureSpec.AT_MOST
        elif spec_mode == MeasureSpec.AT_MOST:
            if child_dimension >= 0:
                result_size = child_dimension
                result_mode = MeasureSpec.EXACTLY
            elif child_dimension == LayoutParams.MATCH_PARENT:
                result_size = size
                result_mode = MeasureSpec.AT_MOST
            elif child_dimension == LayoutParams.WRAP_CONTENT:
                result_size = size
                result_mode = MeasureSpec.AT_MOST
        else: # UNSPECIFIED
            if child_dimension >= 0:
                result_size = child_dimension
                result_mode = MeasureSpec.EXACTLY
            elif child_dimension == LayoutParams.MATCH_PARENT:
                result_size = 0
                result_mode = MeasureSpec.UNSPECIFIED
            elif child_dimension == LayoutParams.WRAP_CONTENT:
                result_size = 0
                result_mode = MeasureSpec.UNSPECIFIED

        return MeasureSpec.makeMeasureSpec(result_size, result_mode)

    def onMeasure(self, width_measure_spec, height_measure_spec):
        max_w = 0
        max_h = 0
        for entry in self._children:
            child = entry["view"] if isinstance(entry, dict) else entry
            if getattr(child, "_props", {}).get("visibility", 0) != View.GONE:
                self.measureChildWithMargins(child, width_measure_spec, 0, height_measure_spec, 0)
                lp = getattr(child, "_layout_params", None)
                margins = getattr(lp, "margins", [0, 0, 0, 0]) or [0, 0, 0, 0]
                max_w = max(max_w, child.getMeasuredWidth() + margins[0] + margins[2])
                max_h = max(max_h, child.getMeasuredHeight() + margins[1] + margins[3])

        total_w = max_w + self.getPaddingLeft() + self.getPaddingRight()
        total_h = max_h + self.getPaddingTop() + self.getPaddingBottom()
        w = GeometryTranslator.resolve_size(max(total_w, self.getSuggestedMinimumWidth()), width_measure_spec)
        h = GeometryTranslator.resolve_size(max(total_h, self.getSuggestedMinimumHeight()), height_measure_spec)
        self.setMeasuredDimension(w, h)

    def onLayout(self, changed, left, top, right, bottom):
        parent_left = self.getPaddingLeft()
        parent_right = max(parent_left, right - left - self.getPaddingRight())
        parent_top = self.getPaddingTop()
        parent_bottom = max(parent_top, bottom - top - self.getPaddingBottom())

        for entry in self._children:
            child = entry["view"] if isinstance(entry, dict) else entry
            if getattr(child, "_props", {}).get("visibility", 0) == View.GONE:
                continue
            width = child.getMeasuredWidth()
            height = child.getMeasuredHeight()
            lp = getattr(child, "_layout_params", None)
            margins = getattr(lp, "margins", [0, 0, 0, 0]) or [0, 0, 0, 0]
            gravity = getattr(lp, "gravity", Gravity.TOP | Gravity.LEFT) or (Gravity.TOP | Gravity.LEFT)

            h_grav = gravity & (Gravity.CENTER_HORIZONTAL | Gravity.RIGHT | Gravity.LEFT)
            v_grav = gravity & (Gravity.CENTER_VERTICAL | Gravity.BOTTOM | Gravity.TOP)

            if h_grav == Gravity.CENTER_HORIZONTAL:
                child_left = parent_left + (parent_right - parent_left - width) // 2 + margins[0] - margins[2]
            elif h_grav == Gravity.RIGHT:
                child_left = parent_right - width - margins[2]
            else:
                child_left = parent_left + margins[0]

            if v_grav == Gravity.CENTER_VERTICAL:
                child_top = parent_top + (parent_bottom - parent_top - height) // 2 + margins[1] - margins[3]
            elif v_grav == Gravity.BOTTOM:
                child_top = parent_bottom - height - margins[3]
            else:
                child_top = parent_top + margins[1]

            child.layout(child_left, child_top, child_left + width, child_top + height)

    def dispatchDraw(self, canvas):
        for entry in self._children:
            child = entry["view"] if isinstance(entry, dict) else entry
            if getattr(child, "_props", {}).get("visibility", 0) != View.GONE:
                self.drawChild(canvas, child, 0)

    def drawChild(self, canvas, child, drawing_time=0):
        canvas.save()
        canvas.translate(child.getLeft(), child.getTop())
        child.draw(canvas)
        canvas.restore()


class LinearLayout(ViewGroup):
    _java_name = "android.widget.LinearLayout"
    VERTICAL = 0
    HORIZONTAL = 1

    class LayoutParams(LayoutParams):
        pass

    def __init__(self, context=None):
        super().__init__(context)
        self._props["type"] = "linearlayout"
        self._props["orientation"] = 0

    def setOrientation(self, orientation):
        self._set_prop("orientation", int(orientation), {"op": "orientation", "value": int(orientation)})

    def getOrientation(self):
        return int(self._props.get("orientation", 0))

    def onMeasure(self, width_measure_spec, height_measure_spec):
        vertical = (self.getOrientation() == 0)
        total_w = 0
        total_h = 0
        for entry in self._children:
            child = entry["view"] if isinstance(entry, dict) else entry
            if getattr(child, "_props", {}).get("visibility", 0) == View.GONE:
                continue
            self.measureChildWithMargins(child, width_measure_spec, 0, height_measure_spec, 0)
            lp = getattr(child, "_layout_params", None)
            margins = getattr(lp, "margins", [0, 0, 0, 0]) or [0, 0, 0, 0]
            cw = child.getMeasuredWidth() + margins[0] + margins[2]
            ch = child.getMeasuredHeight() + margins[1] + margins[3]
            if vertical:
                total_h += ch
                total_w = max(total_w, cw)
            else:
                total_w += cw
                total_h = max(total_h, ch)

        total_w += self.getPaddingLeft() + self.getPaddingRight()
        total_h += self.getPaddingTop() + self.getPaddingBottom()
        w = GeometryTranslator.resolve_size(max(total_w, self.getSuggestedMinimumWidth()), width_measure_spec)
        h = GeometryTranslator.resolve_size(max(total_h, self.getSuggestedMinimumHeight()), height_measure_spec)
        self.setMeasuredDimension(w, h)

    def onLayout(self, changed, left, top, right, bottom):
        vertical = (self.getOrientation() == 0)
        curr_x = self.getPaddingLeft()
        curr_y = self.getPaddingTop()

        for entry in self._children:
            child = entry["view"] if isinstance(entry, dict) else entry
            if getattr(child, "_props", {}).get("visibility", 0) == View.GONE:
                continue
            width = child.getMeasuredWidth()
            height = child.getMeasuredHeight()
            lp = getattr(child, "_layout_params", None)
            margins = getattr(lp, "margins", [0, 0, 0, 0]) or [0, 0, 0, 0]

            if vertical:
                child_left = curr_x + margins[0]
                child_top = curr_y + margins[1]
                child.layout(child_left, child_top, child_left + width, child_top + height)
                curr_y += height + margins[1] + margins[3]
            else:
                child_left = curr_x + margins[0]
                child_top = curr_y + margins[1]
                child.layout(child_left, child_top, child_left + width, child_top + height)
                curr_x += width + margins[0] + margins[2]


class FrameLayout(ViewGroup):
    _java_name = "android.widget.FrameLayout"

    class LayoutParams(LayoutParams):
        pass

    def __init__(self, context=None):
        super().__init__(context)
        self._props["type"] = "framelayout"


class ScrollView(ViewGroup):
    _java_name = "android.widget.ScrollView"

    def __init__(self, context=None):
        super().__init__(context)
        self._props["type"] = "scrollview"

    def scrollTo(self, x, y):
        self._apply({"op": "scroll_to", "x": int(x), "y": int(y)})


class TextView(View):
    _java_name = "android.widget.TextView"

    def __init__(self, context=None, text=None):
        super().__init__(context)
        self._props.update({
            "type": "textview",
            "text": text if text is not None else "",
            "color": None,
            "size": None,
            "bold": False,
            "gravity": None,
            "single_line": False,
            "max_lines": None,
        })

    def setText(self, text):
        self._set_prop("text", str(text if text is not None else ""),
                       {"op": "text", "value": str(text if text is not None else "")})

    def set_text(self, text):
        self.setText(text)

    def append(self, text):
        self.setText((self._props.get("text") or "") + str(text))

    def setTextColor(self, color):
        self._set_prop("color", int(color), {"op": "color", "value": int(color)})

    def setTextSize(self, unit_or_size, size=None):
        if size is None:
            size = unit_or_size
        elif int(unit_or_size or 0) == 0:
            self.setTextSizePx(size)
            return
        self._set_prop("size", dp(size), {"op": "size", "value": dp(size)})

    def setTextSizePx(self, px):
        self._set_prop("size", int(px), {"op": "size", "value": int(px)})

    def setTypeface(self, typeface, style=None):
        if style is not None:
            typeface = style
        try:
            bold = int(typeface) in (Typeface.DEFAULT_BOLD, Typeface.BOLD) if typeface is not None else False
        except Exception:
            bold = False
        self._set_prop("bold", bold, {"op": "bold", "value": bold})

    def setGravity(self, gravity):
        self._set_prop("gravity", int(gravity), {"op": "gravity", "value": int(gravity)})

    def setSingleLine(self, single=True):
        self._set_prop("single_line", bool(single), {"op": "single_line", "value": bool(single)})

    def setLines(self, lines):
        self._props["max_lines"] = int(lines)
        self._apply({"op": "max_lines", "value": int(lines)})

    def setMaxLines(self, lines):
        self.setLines(lines)

    def setLineSpacing(self, add, mult):
        self._apply({"op": "line_spacing", "add": float(add), "mult": float(mult)})

    def getText(self):
        return self._props.get("text", "")

    def onMeasure(self, width_measure_spec, height_measure_spec):
        text = str(self.getText() or "")
        size_px = self._props.get("size") or int(14 * density())
        bold = bool(self._props.get("bold", False))
        native = _native("measure_text")
        if native is not None:
            try:
                tw = int(native(text, int(size_px), bold))
            except Exception:
                tw = int(len(text) * size_px * 0.55)
        else:
            tw = int(len(text) * size_px * 0.55)
        th = int(size_px * 1.3)
        total_w = tw + self.getPaddingLeft() + self.getPaddingRight()
        total_h = th + self.getPaddingTop() + self.getPaddingBottom()
        w = GeometryTranslator.resolve_size(max(total_w, self.getSuggestedMinimumWidth()), width_measure_spec)
        h = GeometryTranslator.resolve_size(max(total_h, self.getSuggestedMinimumHeight()), height_measure_spec)
        self.setMeasuredDimension(w, h)

    def onDraw(self, canvas):
        super().onDraw(canvas)
        text = str(self.getText() or "")
        if text:
            paint = Paint()
            color = self._props.get("color")
            if color is not None:
                paint.setColor(color)
            size = self._props.get("size")
            if size is not None:
                paint.setTextSize(size)
            if self._props.get("bold"):
                paint.setTypeface(Typeface.DEFAULT_BOLD)
            x = float(self.getPaddingLeft())
            y = float(self.getPaddingTop() + (self.getHeight() or int(paint.getTextSize() * 1.2)) * 0.75)
            canvas.drawText(text, x, y, paint)


class EditText(TextView):
    _java_name = "android.widget.EditText"

    def __init__(self, context=None):
        super().__init__(context)
        self._props["type"] = "edittext"
        self._props["hint"] = ""
        self._watchers = []

    def setHint(self, hint):
        self._set_prop("hint", str(hint), {"op": "hint", "value": str(hint)})

    def addTextChangedListener(self, watcher):
        if watcher is not None:
            self._watchers.append(watcher)


class ImageView(View):
    _java_name = "android.widget.ImageView"

    def __init__(self, context=None):
        super().__init__(context)
        self._props.update({"type": "imageview", "bitmap": None})
        self._bitmap_obj = None

    def setImageBitmap(self, bitmap):
        if bitmap is None:
            return
        self._bitmap_obj = bitmap
        bitmap._render()
        self._set_prop("bitmap", bitmap._id, {"op": "bitmap", "value": bitmap._id})

    def getImageBitmap(self):
        return self._bitmap_obj

    def onMeasure(self, width_measure_spec, height_measure_spec):
        bw = getattr(self._bitmap_obj, "width", 0) if self._bitmap_obj else 0
        bh = getattr(self._bitmap_obj, "height", 0) if self._bitmap_obj else 0
        total_w = bw + self.getPaddingLeft() + self.getPaddingRight()
        total_h = bh + self.getPaddingTop() + self.getPaddingBottom()
        w = GeometryTranslator.resolve_size(max(total_w, self.getSuggestedMinimumWidth()), width_measure_spec)
        h = GeometryTranslator.resolve_size(max(total_h, self.getSuggestedMinimumHeight()), height_measure_spec)
        self.setMeasuredDimension(w, h)

    def onDraw(self, canvas):
        super().onDraw(canvas)
        if self._bitmap_obj:
            canvas.drawBitmap(self._bitmap_obj, float(self.getPaddingLeft()), float(self.getPaddingTop()))


class BaseCell(ViewGroup):
    _java_name = "org.telegram.ui.Cells.BaseCell"

    def __init__(self, context=None):
        super().__init__(context)


class ChatMessageCell(BaseCell):
    _java_name = "org.telegram.ui.Cells.ChatMessageCell"

    def __init__(self, context=None):
        super().__init__(context)
        self.messageObject = None
        self.pinned = False

    def setMessageObject(self, messageObject, pinned=False):
        self.messageObject = messageObject
        self.pinned = pinned
        self.requestLayout()

    def getMessageObject(self):
        return self.messageObject

    def onMeasure(self, width_measure_spec, height_measure_spec):
        text = ""
        if self.messageObject:
            text = getattr(self.messageObject, "messageText", "") or getattr(getattr(self.messageObject, "messageOwner", None), "message", "")
        scale = density()
        base_w = int(240 * scale)
        base_h = int(50 * scale)
        if text:
            spec_w = GeometryTranslator.get_size(width_measure_spec) or int(400 * scale)
            base_w = min(int(spec_w * 0.85), int(len(str(text)) * 8 * scale) + int(40 * scale))
            lines = max(1, len(str(text)) // 35 + 1)
            base_h = int((24 + lines * 18) * scale)
        total_w = base_w + self.getPaddingLeft() + self.getPaddingRight()
        total_h = base_h + self.getPaddingTop() + self.getPaddingBottom()
        w = GeometryTranslator.resolve_size(max(total_w, self.getSuggestedMinimumWidth()), width_measure_spec)
        h = GeometryTranslator.resolve_size(max(total_h, self.getSuggestedMinimumHeight()), height_measure_spec)
        self.setMeasuredDimension(w, h)

    def onLayout(self, changed, left, top, right, bottom):
        super().onLayout(changed, left, top, right, bottom)

    def onDraw(self, canvas):
        super().onDraw(canvas)
        paint = Paint()
        paint.setColor(0xFF2B5278)
        paint.setStyle(Paint.Style.FILL)
        w = float(self.getWidth() or self.getMeasuredWidth() or 200)
        h = float(self.getHeight() or self.getMeasuredHeight() or 60)
        scale = density()
        canvas.drawRoundRect(0.0, 0.0, w, h, float(12 * scale), float(12 * scale), paint)
        if self.messageObject:
            text = getattr(self.messageObject, "messageText", "") or getattr(getattr(self.messageObject, "messageOwner", None), "message", "")
            if text:
                text_paint = Paint()
                text_paint.setColor(0xFFFFFFFF)
                text_paint.setTextSize(float(14 * scale))
                canvas.drawText(str(text)[:60], float(12 * scale), float(26 * scale), text_paint)

    def dispatchDraw(self, canvas):
        super().dispatchDraw(canvas)


class DialogCell(BaseCell):
    _java_name = "org.telegram.ui.Cells.DialogCell"

    def buildLayout(self, *args, **kwargs):
        return None

    def onMeasure(self, width_measure_spec, height_measure_spec):
        scale = density()
        spec_w = GeometryTranslator.get_size(width_measure_spec) or int(360 * scale)
        w = GeometryTranslator.resolve_size(spec_w, width_measure_spec)
        h = GeometryTranslator.resolve_size(int(72 * scale), height_measure_spec)
        self.setMeasuredDimension(w, h)

    def onDraw(self, canvas):
        super().onDraw(canvas)

    def dispatchDraw(self, canvas):
        super().dispatchDraw(canvas)


class TitleCell(BaseCell):
    _java_name = "org.telegram.ui.Components.FolderBottomSheet$TitleCell"

    def __init__(self, context=None):
        super().__init__(context)
        self.selectedCount = 0

    def setSelectedCount(self, count, *args):
        self.selectedCount = int(count or 0)
        self.requestLayout()

    def onMeasure(self, width_measure_spec, height_measure_spec):
        scale = density()
        w = GeometryTranslator.resolve_size(int(200 * scale), width_measure_spec)
        h = GeometryTranslator.resolve_size(int(48 * scale), height_measure_spec)
        self.setMeasuredDimension(w, h)


class TextInfoPrivacyCell(BaseCell):
    _java_name = "org.telegram.ui.Cells.TextInfoPrivacyCell"

    def __init__(self, context=None):
        super().__init__(context)
        self._text = ""

    def setText(self, text, *args):
        self._text = str(text or "")
        return self

    def getText(self):
        return self._text


class ChatAvatarContainer(FrameLayout):
    _java_name = "org.telegram.ui.Components.ChatAvatarContainer"

    def getBotVerificationDrawable(self, *args):
        return None


class SwapAnimatedEmojiDrawable:
    _java_name = "org.telegram.ui.Components.AnimatedEmojiDrawable$SwapAnimatedEmojiDrawable"

    def __init__(self, *args):
        self.color = 0

    def setColor(self, color, *args):
        self.color = color
        return self


class AnimatedEmojiDrawable:
    _java_name = "org.telegram.ui.Components.AnimatedEmojiDrawable"
    SwapAnimatedEmojiDrawable = SwapAnimatedEmojiDrawable


class FolderBottomSheet:
    _java_name = "org.telegram.ui.Components.FolderBottomSheet"
    TitleCell = TitleCell

    def __init__(self, *args, **kwargs):
        pass

    def dismiss(self, *args):
        pass

    def updateCount(self, *args):
        pass

    def onJoinButtonClicked(self, *args):
        pass


class ChatActivityEnterView(FrameLayout):
    _java_name = "org.telegram.ui.Components.ChatActivityEnterView"

    def __init__(self, context=None):
        super().__init__(context)
        self._text = ""

    def onMeasure(self, width_measure_spec, height_measure_spec):
        scale = density()
        spec_w = GeometryTranslator.get_size(width_measure_spec) or int(400 * scale)
        w = GeometryTranslator.resolve_size(spec_w, width_measure_spec)
        h = GeometryTranslator.resolve_size(int(52 * scale), height_measure_spec)
        self.setMeasuredDimension(w, h)

    def onDraw(self, canvas):
        super().onDraw(canvas)

    def dispatchDraw(self, canvas):
        super().dispatchDraw(canvas)


class TextWatcher:
    def beforeTextChanged(self, s, start, before, count):
        pass

    def onTextChanged(self, s, start, before, count):
        pass

    def afterTextChanged(self, s):
        pass


class DecelerateInterpolator:
    def __init__(self, *args):
        pass


class OvershootInterpolator:
    def __init__(self, *args):
        pass


def _child(entry):
    return {
        "params": _params(entry["params"]),
        "view": _node(entry["view"], entry["params"]),
    }


def _params(params):
    if params is None:
        return {}
    gravity = getattr(params, "gravity", None)
    return {
        "w": int(getattr(params, "width", -2)),
        "h": int(getattr(params, "height", -2)),
        "gravity": int(gravity) if gravity is not None else None,
        "margins": [int(m) for m in getattr(params, "margins", [0, 0, 0, 0])],
    }


def _node(view, params=None):
    data = view._data()
    data["id"] = view._id
    data["children"] = [_child(e) for e in view._children]
    return data


# Sheets

class _SheetHandle:
    def __init__(self, sheet_id):
        self._sheet_id = sheet_id

    def setCanDismissWithSwipe(self, value):
        return self

    def setAllowNestedScroll(self, value):
        return self

    def setOnDismissListener(self, listener):
        _sheets[self._sheet_id] = {"dismiss": listener}
        return self

    def dismiss(self):
        native = _native("views_dismiss_sheet")
        if native is not None:
            try:
                native(self._sheet_id)
            except Exception:
                pass

    def onDismiss(self):
        entry = _sheets.pop(self._sheet_id, None)
        if entry and entry.get("dismiss"):
            from android_utils import safe_call
            safe_call(entry["dismiss"])


def _show_sheet(root, style):
    with _lock:
        _ids[0] += 1
        sheet_id = _ids[0]
    tree = _node(root)
    payload = {"id": sheet_id, "style": style, "root": tree}
    _mark_live(root, sheet_id)
    native = _native("views_show_sheet")
    if native is None:
        return None
    try:
        native(json.dumps(payload, ensure_ascii=False))
    except Exception:
        return None
    return _SheetHandle(sheet_id)


def _mark_live(view, sheet_id):
    view._sheet = sheet_id
    view._live = True
    for entry in view._children:
        _mark_live(entry["view"], sheet_id)


class BottomSheet(metaclass=_UiClassMeta):
    def __getattr__(self, item):
        if item.startswith("__") or item.startswith("_"):
            raise AttributeError(item)
        return _compat.lenient("BottomSheet", item)

    class Builder:
        def __init__(self, context=None, dark=None, *args):
            self._view = None
            self._title = None

        def setCustomView(self, view):
            self._view = view
            return self

        def setTitle(self, title):
            self._title = title
            return self

        def create(self):
            return self

        def __getattr__(self, item):
            if item.startswith("_"):
                raise AttributeError(item)
            return lambda *args, **kwargs: self

        def show(self):
            if self._view is None:
                return None
            return _show_sheet(self._view, "sheet")

    def __init__(self, *args):
        pass


def _android_listener(listener):
    if listener is None:
        return None
    on_click = getattr(listener, "onClick", None)
    if callable(on_click):
        return lambda dialog, which: on_click(dialog, which)
    if callable(listener):
        return lambda dialog, which: listener(dialog, which)
    return None


class AlertDialog(metaclass=_UiClassMeta):
    BUTTON_POSITIVE = -1
    BUTTON_NEGATIVE = -2
    BUTTON_NEUTRAL = -3

    class Builder:
        def __init__(self, context=None, *args):
            self._view = None
            self._title = ""
            self._message = None
            self._items = None
            self._items_listener = None
            self._buttons = []
            self._cancelable = True
            self._dismiss = None
            self._native = None

        def setView(self, view, *args):
            self._view = view
            return self

        def setTitle(self, title):
            self._title = str(title or "")
            return self

        def setMessage(self, message):
            self._message = str(message or "")
            return self

        def setItems(self, items, listener=None, *args):
            self._items = [str(x) for x in (items or [])]
            self._items_listener = listener
            return self

        def setPositiveButton(self, text, listener=None):
            self._buttons.append(("positive", str(text or ""), listener))
            return self

        def setNegativeButton(self, text, listener=None):
            self._buttons.append(("negative", str(text or ""), listener))
            return self

        def setNeutralButton(self, text, listener=None):
            self._buttons.append(("neutral", str(text or ""), listener))
            return self

        def setCancelable(self, value):
            self._cancelable = bool(value)
            return self

        def setOnDismissListener(self, listener):
            self._dismiss = listener
            return self

        def __getattr__(self, item):
            if item.startswith("_"):
                raise AttributeError(item)
            return lambda *args, **kwargs: self

        def create(self):
            return self

        def getButton(self, which):
            return _compat.Stub("AlertDialog.button")

        def dismiss(self):
            if self._native is not None:
                try:
                    self._native.dismiss()
                except Exception:
                    pass

        def show(self):
            if self._view is not None:
                return _show_sheet(self._view, "alert")
            import ui.alert as alert
            builder = alert.AlertDialogBuilder(None)
            if self._title:
                builder.set_title(self._title)
            if self._message:
                builder.set_message(self._message)
            if self._items:
                builder.set_items(self._items, _android_listener(self._items_listener))
            for kind, text, listener in self._buttons:
                wrapped = _android_listener(listener)
                if kind == "positive":
                    builder.set_positive_button(text, wrapped)
                elif kind == "negative":
                    builder.set_negative_button(text, wrapped)
                else:
                    builder.set_neutral_button(text, wrapped)
            builder.set_cancelable(self._cancelable)
            if self._dismiss is not None:
                on_dismiss = getattr(self._dismiss, "onDismiss", self._dismiss)
                builder.set_on_dismiss_listener(lambda *args: on_dismiss(self))
            self._native = builder
            builder.show()
            return self

    def __init__(self, *args):
        pass


class _Theme(metaclass=_ThemeMeta):
    key_windowBackgroundWhiteBlackText = "windowFg"
    key_windowBackgroundWhiteGrayText = "windowSubFg"
    key_windowBackgroundWhiteValueText = "windowFg"
    key_windowBackgroundWhiteBlueText = "windowActiveTextFg"
    key_windowBackgroundWhiteBlueText4 = "windowActiveTextFg"
    key_featuredStickers_addButton = "windowActiveTextFg"
    key_chats_actionBackground = "windowActiveTextFg"
    key_windowBackgroundWhite = "windowBg"
    key_actionBarDefault = "titleFg"

    @staticmethod
    def getColor(key):
        try:
            return theme_color(key)
        except Exception:
            return 0

    @staticmethod
    def getCurrentThemeName():
        return "desktop"

    def __getattr__(self, item):
        if item.startswith("key_"):
            return "windowFg"
        if item.startswith("__"):
            raise AttributeError(item)
        return _compat.Stub("Theme." + item)


Theme = _Theme()
