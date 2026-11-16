#!/usr/bin/env python3
"""Comprehensive test suite for Android Geometry & Drawing hooks (onDraw, dispatchDraw, onMeasure, onLayout)."""
import os
import sys
import types
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PYTHON_DIR = os.path.join(os.path.dirname(HERE), "Telegram", "SourceFiles", "extera", "python")
sys.path.insert(0, PYTHON_DIR)

if "_extera" not in sys.modules:
    _m = types.ModuleType("_extera")
    _m.log = lambda *a: None
    _m.toast = lambda *a: None
    _m.copy = lambda *a: None
    _m.send_message = lambda *a: None
    _m.settings_changed = lambda *a: None
    _m.plugins_changed = lambda *a: None
    _m.views_render_canvas = lambda *a: None
    _m.measure_text = lambda text, size, bold: float(len(str(text)) * size * 0.55)
    _m.ui_metrics = lambda: '{"dp": 2.0, "width": 1080, "height": 1920}'
    _m.theme_color = lambda name: 0
    sys.modules["_extera"] = _m

import _compat
import android_views
from android_views import (
    GeometryTranslator, MeasureSpec, Rect, RectF, Paint, Bitmap, Canvas,
    View, ViewGroup, FrameLayout, LinearLayout, TextView, ImageView,
    ChatMessageCell, BaseCell, DialogCell, ChatActivityEnterView, LayoutParams
)
import exd_java
import exd_android
from exd_android import XposedBridge, XC_MethodHook, XC_MethodReplacement
import base_plugin
from base_plugin import BasePlugin, MethodHook, MethodReplacement, VirtualParam


class DummyPlugin(BasePlugin):
    def __init__(self, plugin_id="dummy_test"):
        super().__init__()
        self._extera_attach(plugin_id)


class AndroidGeometryDrawingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        exd_android.install(_compat.SHIMS)

    def setUp(self):
        self.plugin = DummyPlugin("test_geo_plugin")
        # Ensure plugin is known to host context
        import extera_host
        rec = extera_host.Record(
            path="",
            meta={"id": self.plugin.id, "name": "Test Geometry Plugin"}
        )
        rec.instance = self.plugin
        extera_host._records[self.plugin.id] = rec

    def tearDown(self):
        self.plugin._extera_cleanup_resources()

    def test_rect_and_rectf(self):
        r = Rect(10, 20, 110, 220)
        self.assertEqual(r.width, 100)
        self.assertEqual(r.height, 200)
        self.assertTrue(r.contains(15, 25))
        self.assertFalse(r.contains(5, 5))

        rf = RectF(r)
        self.assertEqual(rf.width, 100.0)
        self.assertEqual(rf.height, 200.0)
        rf.offset(5.5, 10.5)
        self.assertEqual(rf.left, 15.5)
        self.assertEqual(rf.top, 30.5)

    def test_measure_spec(self):
        spec = MeasureSpec.makeMeasureSpec(350, MeasureSpec.EXACTLY)
        self.assertEqual(MeasureSpec.getMode(spec), MeasureSpec.EXACTLY)
        self.assertEqual(MeasureSpec.getSize(spec), 350)

        spec_at_most = MeasureSpec.makeMeasureSpec(800, MeasureSpec.AT_MOST)
        self.assertEqual(MeasureSpec.getMode(spec_at_most), MeasureSpec.AT_MOST)
        self.assertEqual(MeasureSpec.getSize(spec_at_most), 800)

        resolved = GeometryTranslator.resolve_size(500, spec_at_most)
        self.assertEqual(resolved, 500)
        resolved_over = GeometryTranslator.resolve_size(1000, spec_at_most)
        self.assertEqual(resolved_over, 800)

    def test_geometry_translator_bounds(self):
        left, top, right, bottom = 20, 30, 220, 130
        x, y, w, h = GeometryTranslator.android_to_desktop_bounds(left, top, right, bottom)
        self.assertEqual((x, y, w, h), (20, 30, 200, 100))

        l2, t2, r2, b2 = GeometryTranslator.desktop_to_android_bounds(x, y, w, h)
        self.assertEqual((l2, t2, r2, b2), (left, top, right, bottom))

    def test_view_measurement_and_layout(self):
        view = View()
        view.setMinimumWidth(150)
        view.setMinimumHeight(60)
        view.setPadding(10, 5, 10, 5)

        w_spec = MeasureSpec.makeMeasureSpec(400, MeasureSpec.AT_MOST)
        h_spec = MeasureSpec.makeMeasureSpec(200, MeasureSpec.AT_MOST)
        view.measure(w_spec, h_spec)

        self.assertEqual(view.getMeasuredWidth(), 150)
        self.assertEqual(view.getMeasuredHeight(), 60)

        view.layout(20, 30, 170, 90)
        self.assertEqual(view.getLeft(), 20)
        self.assertEqual(view.getTop(), 30)
        self.assertEqual(view.getRight(), 170)
        self.assertEqual(view.getBottom(), 90)
        self.assertEqual(view.getWidth(), 150)
        self.assertEqual(view.getHeight(), 60)

    def test_canvas_drawing_ops(self):
        bmp = Bitmap(400, 200)
        canvas = Canvas(bmp)
        paint = Paint()
        paint.setColor(0xFFFF0000)
        paint.setStyle(Paint.Style.FILL)

        canvas.save()
        canvas.translate(10, 20)
        canvas.drawRect(0, 0, 100, 50, paint)
        canvas.drawText("Test", 5, 25, paint)
        canvas.restore()

        self.assertEqual(len(bmp.ops), 5)
        self.assertEqual(bmp.ops[0]["op"], "save")
        self.assertEqual(bmp.ops[1]["op"], "translate")
        self.assertEqual(bmp.ops[2]["op"], "rect")
        self.assertEqual(bmp.ops[3]["op"], "text")
        self.assertEqual(bmp.ops[4]["op"], "restore")

    def test_view_on_measure_hook(self):
        hook_called = []

        class MeasureHook(MethodHook):
            def before_hooked_method(self, param):
                hook_called.append("before")
                # Modify width measure spec to force exact 500
                param.args[0] = MeasureSpec.makeMeasureSpec(500, MeasureSpec.EXACTLY)

            def after_hooked_method(self, param):
                hook_called.append("after")
                # Override measured dimension
                param.thisObject.setMeasuredDimension(555, 111)

        unhook = self.plugin.hook_method(View, "onMeasure", MeasureHook())
        self.assertTrue(str(unhook).startswith("exd_shim_"))

        view = View()
        view.setMinimumWidth(100)
        view.setMinimumHeight(50)
        view.measure(MeasureSpec.makeMeasureSpec(200, MeasureSpec.AT_MOST),
                     MeasureSpec.makeMeasureSpec(200, MeasureSpec.AT_MOST))

        self.assertEqual(hook_called, ["before", "after"])
        self.assertEqual(view.getMeasuredWidth(), 555)
        self.assertEqual(view.getMeasuredHeight(), 111)

        self.plugin.unhook_method(unhook)

    def test_view_on_layout_hook(self):
        layout_events = []

        class LayoutHook(MethodHook):
            def before_hooked_method(self, param):
                # param.args: [changed, left, top, right, bottom]
                changed, l, t, r, b = param.args
                layout_events.append(("before", changed, l, t, r, b))
                # Adjust right bound
                param.args[3] = r + 50

            def after_hooked_method(self, param):
                layout_events.append(("after", param.thisObject.getRight()))

        unhook = self.plugin.hook_method(View, "onLayout", LayoutHook())
        self.assertTrue(str(unhook).startswith("exd_shim_"))

        view = View()
        view.layout(10, 20, 110, 120)

        self.assertEqual(len(layout_events), 2)
        self.assertEqual(layout_events[0], ("before", True, 10, 20, 110, 120))
        self.plugin.unhook_method(unhook)

    def test_view_on_draw_hook(self):
        draw_events = []

        class DrawHook(MethodHook):
            def before_hooked_method(self, param):
                canvas = param.args[0]
                paint = Paint()
                paint.setColor(0xFF00FF00)
                canvas.drawRect(5, 5, 95, 45, paint)
                draw_events.append("custom_drawn_before")

            def after_hooked_method(self, param):
                canvas = param.args[0]
                paint = Paint()
                paint.setColor(0xFF0000FF)
                canvas.drawText("Overlay", 10, 20, paint)
                draw_events.append("custom_drawn_after")

        unhook = self.plugin.hook_method(View, "onDraw", DrawHook())
        self.assertTrue(str(unhook).startswith("exd_shim_"))

        bmp = Bitmap(200, 100)
        canvas = Canvas(bmp)
        view = View()
        view.layout(0, 0, 200, 100)
        view.draw(canvas)

        self.assertIn("custom_drawn_before", draw_events)
        self.assertIn("custom_drawn_after", draw_events)
        ops = [op["op"] for op in bmp.ops]
        self.assertIn("rect", ops)
        self.assertIn("text", ops)

        self.plugin.unhook_method(unhook)

    def test_viewgroup_dispatch_draw_hook(self):
        dispatch_events = []

        class DispatchDrawHook(MethodHook):
            def before_hooked_method(self, param):
                canvas = param.args[0]
                canvas.drawColor(0xFFEEEEEE)
                dispatch_events.append("background_under_children")

            def after_hooked_method(self, param):
                canvas = param.args[0]
                paint = Paint()
                canvas.drawCircle(50, 50, 20, paint)
                dispatch_events.append("overlay_over_children")

        unhook = self.plugin.hook_method(ViewGroup, "dispatchDraw", DispatchDrawHook())
        self.assertTrue(str(unhook).startswith("exd_shim_"))

        group = FrameLayout()
        child = TextView(text="Hello Child")
        child.setLayoutParams(LayoutParams(100, 40))
        group.addView(child)

        group.measure(MeasureSpec.makeMeasureSpec(300, MeasureSpec.EXACTLY),
                      MeasureSpec.makeMeasureSpec(200, MeasureSpec.EXACTLY))
        group.layout(0, 0, 300, 200)

        bmp = Bitmap(300, 200)
        canvas = Canvas(bmp)
        group.draw(canvas)

        self.assertEqual(dispatch_events, ["background_under_children", "overlay_over_children"])
        ops = [op["op"] for op in bmp.ops]
        self.assertIn("rect", ops)   # from drawColor
        self.assertIn("save", ops)   # from drawChild
        self.assertIn("text", ops)   # from child TextView
        self.assertIn("restore", ops)# from drawChild
        self.assertIn("circle", ops) # from overlay

        self.plugin.unhook_method(unhook)

    def test_chat_message_cell_xposed_bridge(self):
        """Tests XposedBridge.hookMethod and XC_MethodHook on ChatMessageCell."""
        cell_measured = []
        cell_drawn = []

        class CellMeasureXposed(XC_MethodHook):
            def beforeHookedMethod(self, param):
                w_spec, h_spec = param.args[0], param.args[1]
                w = MeasureSpec.getSize(w_spec)
                cell_measured.append(("xposed_before_measure", w))

            def afterHookedMethod(self, param):
                cell_measured.append(("xposed_after_measure", param.thisObject.getMeasuredWidth()))

        class CellDrawXposed(XC_MethodHook):
            def beforeHookedMethod(self, param):
                canvas = param.args[0]
                paint = Paint()
                paint.setColor(0xFFFFD700) # Gold badge
                canvas.drawCircle(15, 15, 10, paint)
                cell_drawn.append("gold_badge_drawn")

        # Use reflection / lookup to find method, exactly as real Android plugins do
        cell_cls = exd_java.lookup("org.telegram.ui.Cells.ChatMessageCell")
        self.assertIsNotNone(cell_cls)
        self.assertEqual(cell_cls, ChatMessageCell)

        m_measure = cell_cls.getMethod("onMeasure")
        m_draw = cell_cls.getMethod("onDraw")

        unhook_measure = XposedBridge.hookMethod(m_measure, CellMeasureXposed())
        unhook_draw = XposedBridge.hookMethod(m_draw, CellDrawXposed())

        self.assertIsNotNone(unhook_measure)
        self.assertIsNotNone(unhook_draw)

        # Instantiate cell and trigger lifecycle
        cell = ChatMessageCell()
        msg_obj = type("Msg", (), {"messageText": "Hello from Xposed Geometry!"})()
        cell.setMessageObject(msg_obj)

        w_spec = MeasureSpec.makeMeasureSpec(600, MeasureSpec.AT_MOST)
        h_spec = MeasureSpec.makeMeasureSpec(800, MeasureSpec.AT_MOST)
        cell.measure(w_spec, h_spec)
        cell.layout(0, 0, cell.getMeasuredWidth(), cell.getMeasuredHeight())

        bmp = Bitmap(cell.getWidth(), cell.getHeight())
        canvas = Canvas(bmp)
        cell.draw(canvas)

        self.assertEqual(len(cell_measured), 2)
        self.assertEqual(cell_measured[0][0], "xposed_before_measure")
        self.assertEqual(cell_measured[0][1], 600)
        self.assertEqual(cell_measured[1][0], "xposed_after_measure")

        self.assertIn("gold_badge_drawn", cell_drawn)
        ops = [op["op"] for op in bmp.ops]
        self.assertIn("circle", ops) # Gold badge
        self.assertIn("rrect", ops)  # Bubble background
        self.assertIn("text", ops)   # Message text

        unhook_measure.unhook()
        unhook_draw.unhook()


if __name__ == "__main__":
    unittest.main()
