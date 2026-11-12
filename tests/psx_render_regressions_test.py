"""Verify texture/font invariants without requiring Nintendo assets or an emulator."""
import json
from pathlib import Path
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from psx_static_mesh import Triangle, Vertex, compile_mesh, emit_list, read_combiners, select_geometry_variant, tessellate, uv_offsets


class RendererRegressionTests(unittest.TestCase):
    def test_file_select_front_faces_not_occluded_on_ps1(self):
        menu = (ROOT / "levels/menu/geo.c").read_text()
        level = (ROOT / "levels/menu/leveldata.c").read_text()
        self.assertIn("GEO_DISPLAY_LIST(LAYER_OPAQUE, dl_menu_save_button_fade_back)", menu)
        self.assertGreaterEqual(menu.count("#ifndef TARGET_PSX"), 2)
        self.assertIn("gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK | G_CULL_FRONT)", level)

    def test_warp_transition_textures_flipped_not_black_surround(self):
        transition = (ROOT / "src/game/screen_transition.c").read_text()
        self.assertEqual(transition.count("gfx_emit_screen_quad_vflip(centerTransX,"), 3)
        self.assertIn("gfx_emit_screen_quad(0, 0, XRES, centerTransY - texTransRadius + 1)", transition)

    def test_offline_tessellation_stays_within_overlay_budget(self):
        mesh = (ROOT / "tools/psx_static_mesh.py").read_text()
        pre = (ROOT / "tools/preprocess_graphics.py").read_text()
        self.assertIn("WORLD_EDGE_LIMITS = (512, 768, 1024, 2048, 4096, 8192, 32768)", mesh)
        self.assertIn("if native_size <= old_size:", mesh)
        self.assertIn('128 if "levels/menu/leveldata." in out_path else 256', pre)

    def test_shared_material_display_lists_preserve_original_commands(self):
        jit = (ROOT / "src/port/gfx/gfx_rsp_jit.c").read_text()
        self.assertIn("if(!was_useful) return false;", jit)
        self.assertIn("*(out++) = DL_PACK_OP(DL_CMD_END);", jit)
        coin = (ROOT / "actors/coin/model.inc.c").read_text()
        menu = (ROOT / "levels/menu/leveldata.c").read_text()
        self.assertIn("coin_seg3_dl_03007780", coin)
        self.assertIn("dl_tex_block_menu_save_button_base", menu)

    def test_rippling_paintings_restore_material_state_every_frame(self):
        painting = (ROOT / "src/game/paintings.c").read_text()
        self.assertIn("(imageCount + 9) * sizeof(Gfx)", painting)
        self.assertIn("gDPSetCombineMode(gfx++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);", painting)
        self.assertIn("gDPSetCombineMode(gfx++, G_CC_DECALRGB, G_CC_DECALRGB);", painting)
        self.assertIn("gSPSetGeometryMode(gfx++, G_LIGHTING | G_TEXTURE_GEN);", painting)

    def test_painting_material_state_is_restored_for_every_picture(self):
        painting = (ROOT / "levels/castle_inside/painting.inc.c").read_text()
        preamble_start = painting.index("static const Gfx inside_castle_seg7_dl_07021A48[]")
        preamble_end = painting.index("};", preamble_start)
        self.assertNotIn("gsSPLight(", painting[preamble_start:preamble_end])
        self.assertEqual(painting.count("gsSPDisplayList(inside_castle_seg7_dl_07021A48),"), 12)
        self.assertEqual(painting.count("gsSPLight(&inside_castle_seg7_lights_07021800.l, 1),"), 12)
        self.assertEqual(painting.count("gsSPLight(&inside_castle_seg7_lights_07021800.a, 2),"), 12)

    def test_power_meter_stays_below_coin_counter(self):
        hud = (ROOT / "src/game/hud.c").read_text()
        self.assertIn("static s16 power_meter_draw_y(void)", hud)
        self.assertIn("return sPowerMeterHUD.y - 60;", hud)
        self.assertIn("YRES - power_meter_draw_y() - 32 + 16", hud)
        self.assertIn("print_text(168, HUD_TOP_Y,", hud)

    def test_xa_loop_position_is_not_polled_every_frame(self):
        audio = (ROOT / "src/port/psx/audio_psx.c").read_text()
        self.assertIn("#define XA_POSITION_POLL_FRAMES 4", audio)
        self.assertIn("if(xa_position_poll_countdown != 0)", audio)
        self.assertIn("xa_position_poll_countdown = XA_POSITION_POLL_FRAMES - 1;", audio)
        self.assertIn("sfx_backend_tick();", audio)

    def test_completed_ps1_framebuffer_is_presented(self):
        source = (ROOT / "src/port/psx/gfx_framebuffers_psx.c").read_text()
        self.assertIn("selected_fb ^= 1;", source)
        self.assertIn("gp1_fbOffset(selected_fb ? 0 : XRES, 0)", source)
        self.assertNotIn("gp1_fbOffset(selected_fb? XRES: 0, 0)", source)

    def test_door_root_motion_survives_packed_animation_conversion(self):
        converter = (ROOT / "tools/mario_anims_converter.py").read_text()
        renderer = (ROOT / "src/game/rendering_graph_node.c").read_text()
        memory = (ROOT / "src/game/memory.c").read_text()
        mario = (ROOT / "src/game/mario.c").read_text()
        interaction = (ROOT / "src/game/interaction.c").read_text()
        for identifier in ("anim_09", "anim_0B", "anim_0C", "anim_26", "anim_5F", "anim_60", "anim_61", "anim_9C", "anim_9D"):
            self.assertIn(identifier, converter)
        self.assertIn("has_xzpos = True\n        has_ypos = True", converter)
        self.assertIn("is_anim_root_motion_preserved", renderer)
        self.assertIn("gCurAnimType == ANIM_TYPE_NO_TRANSLATION && is_anim_root_motion_preserved", renderer)
        self.assertIn("sample_mario_root_translation", mario)
        self.assertIn("mario_anim_data_bytes = segment_size - just_table_size", memory)
        self.assertIn("q32_find_wall_collision(&newMarioXq, &newMarioYq, &newMarioZq, q(60), q(50))", interaction)

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
        self.assertIn("append_poly(&packet, quad, sxy3, flags & ~PRIM_FLAG_TEXTURED, vertices)", renderer)

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
        self.assertIn("clip_and_draw_triangle(v0, v1, v2,", renderer)
        self.assertNotIn("needs_clip_tessellation", renderer)
        self.assertIn("PACKET_RESERVE_WORDS 64", buffers)
        self.assertIn("if(header > pool_end - reserve)", buffers)
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

    def test_power_meter_uses_one_consistent_coordinate_system(self):
        hud = (ROOT / "src/game/hud.c").read_text()
        self.assertIn("gfx_emit_screen_quad(sPowerMeterHUD.x, power_meter_draw_y() + 32,", hud)
        self.assertIn("gfx_emit_screen_quad(sPowerMeterHUD.x + 32, power_meter_draw_y() + 32,", hud)
        self.assertIn("gfx_emit_sprite(sPowerMeterHUD.x + 16, YRES - power_meter_draw_y() - 32 + 16)", hud)
        self.assertIn("if (sPowerMeterHUD.animation == POWER_METER_HIDDEN) {\n        return;", hud)
        self.assertNotIn("gfx_emit_screen_quad_vflip(sPowerMeterHUD.x", hud)

    def test_hud_packets_are_reserved_from_3d_geometry(self):
        pool = (ROOT / "src/port/psx/gfx_framebuffers_psx.c").read_text()
        renderer = (ROOT / "src/port/psx/gfx_dl_exec_psx.c").read_text()
        self.assertIn("#define PACKET_HUD_RESERVE_WORDS 1536", pool)
        self.assertIn("PACKET_RESERVE_WORDS + PACKET_HUD_RESERVE_WORDS", pool)
        self.assertIn("gfx_packet_begin_ui()", pool)
        self.assertIn("is_ortho && !is_2d_background", renderer)

    def test_gpu_optimizations_preserve_gradients_and_clip_only_offscreen(self):
        renderer = (ROOT / "src/port/psx/gfx_dl_exec_psx.c").read_text()
        self.assertIn("bool gouraud = gfx_psx_colors_vary(c0, c1, c2, c3, is_quad);", renderer)
        self.assertIn("if(gouraud) gfx_packet_append(packet, c1);", renderer)
        self.assertIn("if(!is_ortho && !gte_flags && !depth_straddles_clip", renderer)
        self.assertIn("gfx_psx_outside_viewport(", renderer)

    def test_lakitu_bridge_wait_has_a_guard(self):
        logic = (ROOT / "src/game/behaviors/camera_lakitu.inc.c").read_text()
        self.assertIn("o->oTimer <= 30 || gMarioState->action != ACT_READING_NPC_DIALOG", logic)
        self.assertIn("gMarioState->actionState = 8;", logic)



class StaticMeshTests(unittest.TestCase):
    def test_region_selection_keeps_only_the_requested_variant(self):
        source = "#ifdef VERSION_JP\nJapan\n#else\nInternational\n#endif\n"
        self.assertEqual(select_geometry_variant(source, {"VERSION_JP"}), "Japan\n")
        self.assertEqual(select_geometry_variant(source, {"VERSION_US"}), "International\n")

    def test_native_batch_keeps_vertex_indices_above_fifteen(self):
        points = [Vertex((i, 0, 0), (0, 0), (255, 255, 255)) for i in range(18)]
        triangles = [Triangle(tuple(points[i:i + 3]), (0, 0), "0") for i in range(0, 18, 3)]
        output, _ = emit_list("root", "const Gfx root[] = {", triangles)
        self.assertEqual(output.count("DL_CMD_SEG_VTX"), 1)
        self.assertIn("(15 << 16) | (16 << 8) | 17", output)

    def test_wrapped_uvs_share_an_interval_without_changing_tile_phase(self):
        points = (
            Vertex((0, 0, 0), (-20, 10), (0, 0, 0)),
            Vertex((100, 0, 0), (172, 10), (0, 0, 0)),
            Vertex((0, 100, 0), (-5, 30), (0, 0, 0)),
        )
        offset_u, offset_v = uv_offsets(points, (32, 32))
        self.assertEqual((offset_u, offset_v), (-32, 0))
        for point in points:
            self.assertEqual((point.uv[0] - offset_u) % 32, point.uv[0] % 32)

    def test_uv_split_is_shared_with_untextured_neighbor(self):
        a = Vertex((0, 0, 0), (-200, 0), (0, 0, 0))
        b = Vertex((512, 0, 0), (200, 0), (0, 0, 0))
        c = Vertex((0, 512, 0), (-200, 0), (0, 0, 0))
        d = Vertex((0, -512, 0), (0, 0), (0, 0, 0))
        untextured = Triangle((b, a, d), (0, 0), "0")
        wrapped = Triangle((a, b, c), (32, 0), "PRIM_FLAG_TEXTURED")
        pieces = tessellate({"untextured": [untextured], "wrapped": [wrapped]}, 1024)
        for group in pieces.values():
            self.assertIn((256, 0, 0), {v.xyz for tri in group for v in tri.vertices})
        self.assertEqual(len(pieces["untextured"]), 2)

    def test_static_lists_emit_native_commands_and_a_texture_preamble(self):
        source = """static const Vtx triangle[] = {
    {{{0, 0, 0}, 0, {-320, 0}, {0, 0, 127, 255}}},
    {{{512, 0, 0}, 0, {320, 0}, {0, 0, 127, 255}}},
    {{{0, 512, 0}, 0, {-320, 320}, {0, 0, 127, 255}}},
};
const Gfx root[] = {
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_MODULATERGB, G_CC_MODULATERGB),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture),
    gsSPVertex(triangle, 3, 0),
    gsSP1Triangle(0, 1, 2, 0),
    gsSPEndDisplayList(),
};
"""
        output = compile_mesh(source, {"texture": (32, 32, 0)}, read_combiners())
        self.assertIn("DL_CMD_STATIC_MESH) | (1)", output)
        self.assertIn("DL_CMD_SEG_TEX", output)
        self.assertIn("DL_CMD_STATIC_TRI", output)
        self.assertNotIn("gsSPVertex(", output)


if __name__ == "__main__":
    unittest.main()
