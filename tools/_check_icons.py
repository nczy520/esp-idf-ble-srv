import flet as ft
import inspect
sig = inspect.signature(ft.Tabs.__init__)
for name, param in sig.parameters.items():
    if name not in ('self', 'key', 'ref', 'expand', 'expand_loose', 'col', 'opacity', 'tooltip', 'badge', 'visible', 'disabled', 'rtl', 'adaptive', 'width', 'height', 'left', 'top', 'right', 'bottom', 'align', 'margin', 'rotate', 'scale', 'offset', 'flip', 'transform', 'aspect_ratio', 'animate_opacity', 'animate_size', 'animate_position', 'animate_align', 'animate_margin', 'animate_rotation', 'animate_scale', 'animate_offset', 'size_change_interval', 'on_size_change', 'on_animation_end', 'data'):
        print(f"  {name}: default={param.default}")
