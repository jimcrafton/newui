"""Regenerates every diagram: python docs/tools/gen_all.py  (writes docs/diagrams/*.svg)."""
import gen_dev_diagrams_a, gen_dev_diagrams_b, gen_dev_diagrams_c, gen_dev_diagrams_d
import gen_user_diagrams_a, gen_user_diagrams_b, gen_user_diagrams_c, gen_user_diagrams_d

for module, calls in [
    (gen_dev_diagrams_a, ["architecture", "document_structure", "appearance_stack", "render_pipeline", "layer_ordering"]),
    (gen_dev_diagrams_b, ["shape_geometry", "curve_pipeline", "flattening", "pathfinder"]),
    (gen_dev_diagrams_c, ["stroke_variants", "brushes", "text_warp", "selection_frame", "undo_model"]),
    (gen_dev_diagrams_d, ["file_formats", "libraries", "clipping", "interaction", "snapping_hit"]),
    (gen_user_diagrams_a, ["window_layout", "tool_palette", "toolbar_menus", "selection_handles"]),
    (gen_user_diagrams_b, ["panels_1", "panels_2", "panels_3", "panels_4"]),
    (gen_user_diagrams_c, ["draw_shapes", "pen_pencil", "transform_tools", "align_distribute"]),
    (gen_user_diagrams_d, ["cut_tools", "gradient_width", "text_guide", "brushes_guide", "libraries_guide", "clip_artboards", "guides_snapping", "quickstart"]),
]:
    for name in calls:
        getattr(module, name)()
