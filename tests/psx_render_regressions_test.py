"""Verify texture/font invariants without requiring Nintendo assets or an emulator."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RendererRegressionTests(unittest.TestCase):
    def test_us_dialog_font_is_sideways_and_runtime_rotates_it(self):
        assets = json.loads((ROOT / "assets.json").read_text())
        glyph = assets["textures/segment2/font_graphics.05900.ia4.png"]
        self.assertEqual(glyph[:2], [16, 8])
        backend = (ROOT / "src/port/gfx/gfx_global_dl.c").read_text()
        self.assertIn("gfx_emit_dialog_glyph(void)", backend)
        self.assertIn("list->psx[i].u = (i & 1) ? 0 : last_tex->width;", backend)
        self.assertIn("list->psx[i].v = (i & 2) ? 0 : last_tex->height;", backend)
        text = (ROOT / "src/game/ingame_menu.c").read_text()
        self.assertIn("gfx_emit_dialog_glyph();", text)

    def test_decal_overlay_preserves_texture_colors(self):
        renderer = (ROOT / "src/port/psx/gfx_dl_exec_psx.c").read_text()
        self.assertIn("decal_overlay = textured && (flags & PRIM_FLAG_DECAL)", renderer)
        self.assertIn("decal_overlay ? 0x00808080 :", renderer)
        self.assertIn("append_poly(&packet, v3, sxy3, flags & ~PRIM_FLAG_TEXTURED, storage)", renderer)

    def test_lakitu_bridge_wait_has_a_guard(self):
        logic = (ROOT / "src/game/behaviors/camera_lakitu.inc.c").read_text()
        self.assertIn("o->oTimer <= 30 || gMarioState->action != ACT_READING_NPC_DIALOG", logic)
        self.assertIn("gMarioState->actionState = 8;", logic)


if __name__ == "__main__":
    unittest.main()
