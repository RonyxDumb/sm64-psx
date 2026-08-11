#pragma once
#include "../gd_types.h"
#include "../gdf.h"
#include <port/gfx/gfx.h>

#define GDF_JOINT_WEIGHT(index_, amount_) (GdfJointWeight) {.index = (index_), .amount = (amount_) * ONE / 100}
#define GDF_MATERIAL(rf, gf, bf) (Color) {.r = (rf) * 255, .g = (gf) * 255, .b = (bf) * 255}
#define GDF_SCALE(xf, yf, zf) (ShortVec) {.vx = (xf) * ONE, .vy = (yf) * ONE, .vz = (zf) * ONE}
#define GDF_ROTATION(xf, yf, zf) (ShortVec) {.vx = (short) ((xf) * 65536 / 360), .vy = (short) ((yf) * 65536 / 360), .vz = (short) ((zf) * 65536 / 360)}
#define GDF_POSITION(xf, yf, zf) (ShortVec) {.vx = (xf), .vy = (yf), .vz = (zf)}

extern struct GdVtxData mario_Face_VtxInfo;
extern struct GdFaceData mario_Face_FaceInfo;

extern struct GdVtxData vtx_mario_eye_right;
extern struct GdFaceData faces_mario_eye_right;

extern struct GdVtxData vtx_mario_eye_left;
extern struct GdFaceData faces_mario_eye_left;

extern struct GdVtxData vtx_mario_eyebrow_right;
extern struct GdFaceData faces_mario_eyebrow_right;

extern struct GdVtxData vtx_mario_eyebrow_left;
extern struct GdFaceData faces_mario_eyebrow_left;

extern struct GdVtxData vtx_mario_mustache;
extern struct GdFaceData faces_mario_mustache;

extern Color mario_face_materials[];
extern Color mario_eye_materials[];
extern Color mario_eyebrow_mustache_materials[];

extern GdfJointList mario_face_joint_list;
extern GdfJointList mario_eyebrow_right_joint_list;
extern GdfJointList mario_eyebrow_left_joint_list;
extern GdfJointList mario_mustache_joint_list;
