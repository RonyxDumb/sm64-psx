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
        modulation = (ROOT / "src/port/gfx/gfx_texture_modulation.h").read_text()
        self.assertIn("if(decal_overlay)", modulation)
        self.assertIn("return 0x00808080u;", modulation)
        self.assertIn("append_poly(&packet, v3, sxy3, flags & ~PRIM_FLAG_TEXTURED, storage)", renderer)

    def test_two_sided_billboards_survive_nclip(self):
        jit = (ROOT / "src/port/gfx/gfx_rsp_jit.c").read_text()
        dl = (ROOT / "src/port/psx/gfx_dl_exec_psx.c").read_text()
        self.assertIn("flags |= PRIM_FLAG_TWO_SIDED", jit)
        self.assertIn("!(flags & PRIM_FLAG_TWO_SIDED)", dl)
        self.assertIn("G_CULL_BACK | G_CULL_FRONT", jit)
        coin = (ROOT / "actors/coin/model.inc.c").read_text()
        self.assertIn("gsSPClearGeometryMode(G_LIGHTING)", coin)

    def test_floor_uv_wrap_uses_modulo_not_saturation(self):
        jit = (ROOT / "src/port/gfx/gfx_rsp_jit.c").read_text()
        self.assertIn("gfx_wrap_texel_psx(u, tex->width)", jit)
        self.assertIn("gfx_wrap_texel_psx(v, tex->height)", jit)
        self.assertNotIn("clamp(u + 128, 0, 255)", jit)

    def test_levels_do_not_exhaust_gpu_packet_pool(self):
        renderer = (ROOT / "src/port/psx/gfx_dl_exec_psx.c").read_text()
        buffers = (ROOT / "src/port/psx/gfx_framebuffers_psx.c").read_text()
        self.assertIn("#define SPARING_TESSELLATION 3", renderer)
        self.assertIn("PACKET_RESERVE_WORDS 64", buffers)
        self.assertIn("if(header > pool_end - PACKET_RESERVE_WORDS)", buffers)
        self.assertIn("packet.header == discarded_packet", buffers)
        self.assertIn("assert(ot_z < OT_LEN)", buffers)

    def test_title_logo_depth_is_isolated_global_texture_brightness_is_not(self):
        model = (ROOT / "levels/intro/leveldata.c").read_text()
        renderer = (ROOT / "src/port/psx/gfx_dl_exec_psx.c").read_text()
        jit = (ROOT / "src/port/gfx/gfx_rsp_jit.c").read_text()
        flags = (ROOT / "src/port/gfx/gfx.h").read_text()
        self.assertIn("gsSPSetGeometryMode(G_PORT_TITLE_LOGO)", model)
        self.assertIn("gsSPClearGeometryMode(G_PORT_TITLE_LOGO)", model)
        self.assertIn("if(geometry_mode & G_PORT_TITLE_LOGO) flags |= PRIM_FLAG_TITLE_LOGO;", jit)
        self.assertIn("PRIM_FLAG_TITLE_LOGO", flags)
        self.assertIn("gfx_psx_material_color(vtx[0].color.as_u32, textured, decal_overlay)", renderer)
        self.assertIn("#include <port/gfx/gfx_texture_modulation.h>", renderer)
        self.assertNotIn("textured && (flags & PRIM_FLAG_TITLE_LOGO)", renderer)
        self.assertIn("sort_z = depth_sum / (v3 ? 4 : 3);", renderer)
        self.assertIn("if(!is_ortho && (flags & PRIM_FLAG_TITLE_LOGO))", renderer)

    def test_lakitu_bridge_wait_has_a_guard(self):
        logic = (ROOT / "src/game/behaviors/camera_lakitu.inc.c").read_text()
        self.assertIn("o->oTimer <= 30 || gMarioState->action != ACT_READING_NPC_DIALOG", logic)
        self.assertIn("gMarioState->actionState = 8;", logic)


if __name__ == "__main__":
    unittest.main()
