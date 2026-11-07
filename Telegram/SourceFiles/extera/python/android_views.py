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


class RectF:
    def __init__(self, left=0, top=0, right=0, bottom=0):
        self.left = float(left)
        self.top = float(top)
        self.right = float(right)
        self.bottom = float(bottom)


class Paint(metaclass=_compat.LenientMeta):
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

    def setStyle(self, style):
        self.style = style
        return self

    def setColor(self, color):
        self.color = int(color)
        return self

    def setStrokeWidth(self, width):
        self.stroke_width = float(width)
        return self

    def setTextSize(self, size):
        self.text_size = float(size)
        return self

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
    def __init__(self, bitmap):
        self._bitmap = bitmap

    def _op(self, data):
        data["paint"] = self._paint._data() if getattr(self, "_paint", None) else None
        self._bitmap.ops.append(data)

    def _draw(self, paint):
        self._paint = paint

    def drawRoundRect(self, rect, rx, ry, paint):
        self._draw(paint)
        self._op({"op": "rrect", "rect": [rect.left, rect.top, rect.right, rect.bottom],
                  "rx": float(rx), "ry": float(ry)})

    def drawRect(self, rect, paint):
        self._draw(paint)
        self._op({"op": "rect", "rect": [rect.left, rect.top, rect.right, rect.bottom]})

    def drawCircle(self, cx, cy, radius, paint):
        self._draw(paint)
        self._op({"op": "circle", "cx": float(cx), "cy": float(cy), "radius": float(radius)})

    def drawLine(self, x1, y1, x2, y2, paint):
        self._draw(paint)
        self._op({"op": "line", "from": [float(x1), float(y1)], "to": [float(x2), float(y2)]})

    def drawArc(self, rect, start, sweep, use_center, paint):
        self._draw(paint)
        self._op({"op": "arc", "rect": [rect.left, rect.top, rect.right, rect.bottom],
                  "start": float(start), "sweep": float(sweep)})

    def drawText(self, text, x, y, paint):
        self._draw(paint)
        self._op({"op": "text", "text": str(text), "x": float(x), "y": float(y)})

    def drawBitmap(self, bitmap, left, top, paint=None):
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
    VISIBLE = 0
    INVISIBLE = 4
    GONE = 8

    def __getattr__(self, item):
        if item.startswith("__") or item.startswith("_"):
            raise AttributeError(item)
        return _compat.lenient(type(self).__name__, item)

    class OnClickListener:
        def onClick(self, view):
            pass

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
        return dict(self._props)

    def setVisibility(self, visibility):
        visible = visibility == 0
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

    def setLayoutParams(self, params):
        if params is None:
            return
        gravity = getattr(params, "gravity", None)
        self._apply({"op": "params",
                     "w": int(getattr(params, "width", -2)),
                     "h": int(getattr(params, "height", -2)),
                     "gravity": int(gravity) if gravity is not None else None})

    def setBackgroundColor(self, color):
        self._props["bg"] = ColorDrawable(color)._data()
        self._apply({"op": "bg", "value": self._props["bg"]})

    def setBackground(self, drawable):
        data = drawable._data() if hasattr(drawable, "_data") else None
        self._props["bg"] = data
        self._apply({"op": "bg", "value": data})

    def setBackgroundDrawable(self, drawable):
        self.setBackground(drawable)

    def setPadding(self, left, top, right, bottom):
        self._props["padding"] = [int(left), int(top), int(right), int(bottom)]
        self._apply({"op": "padding", "value": self._props["padding"]})

    def setOnClickListener(self, listener):
        self._click = listener

    def animate(self):
        return ViewPropertyAnimator(self)

    def invalidate(self):
        pass

    def postInvalidate(self):
        pass

    def requestLayout(self):
        pass

    def forceLayout(self):
        pass

    def measure(self, width_spec, height_spec):
        pass

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
    def __init__(self, context=None):
        View.__init__(self, context)
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


class LinearLayout(ViewGroup):
    VERTICAL = 0
    HORIZONTAL = 1

    class LayoutParams(LayoutParams):
        pass

    def __init__(self, context=None):
        ViewGroup.__init__(self, context)
        self._props["type"] = "linearlayout"
        self._props["orientation"] = 0

    def setOrientation(self, orientation):
        self._set_prop("orientation", int(orientation), {"op": "orientation", "value": int(orientation)})


class FrameLayout(ViewGroup):
    class LayoutParams(LayoutParams):
        pass

    def __init__(self, context=None):
        ViewGroup.__init__(self, context)
        self._props["type"] = "framelayout"


class ScrollView(ViewGroup):
    def __init__(self, context=None):
        ViewGroup.__init__(self, context)
        self._props["type"] = "scrollview"

    def scrollTo(self, x, y):
        self._apply({"op": "scroll_to", "x": int(x), "y": int(y)})


class TextView(View):
    def __init__(self, context=None, text=None):
        View.__init__(self, context)
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


class EditText(TextView):
    def __init__(self, context=None):
        TextView.__init__(self, context)
        self._props["type"] = "edittext"
        self._props["hint"] = ""
        self._watchers = []

    def setHint(self, hint):
        self._set_prop("hint", str(hint), {"op": "hint", "value": str(hint)})

    def addTextChangedListener(self, watcher):
        if watcher is not None:
            self._watchers.append(watcher)


class ImageView(View):
    def __init__(self, context=None):
        View.__init__(self, context)
        self._props.update({"type": "imageview", "bitmap": None})

    def setImageBitmap(self, bitmap):
        if bitmap is None:
            return
        bitmap._render()
        self._set_prop("bitmap", bitmap._id, {"op": "bitmap", "value": bitmap._id})


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
