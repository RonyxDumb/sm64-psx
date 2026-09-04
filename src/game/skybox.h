#ifndef SKYBOX_H
#define SKYBOX_H

#include <PR/ultratypes.h>
#include <PR/gbi.h>

/*
 * On PS1 this preloads the small metadata record for the selected skybox from
 * EXT.DAT before rendering begins. On other targets it is a no-op.
 */
void skybox_preload(s8 background);

Gfx *create_skybox_facing_cameraq(s8 player, s8 background, q32 fovq,
                                  q32 posXq, q32 posYq, q32 posZq,
                                  q32 focXq, q32 focYq, q32 focZq);

#endif // SKYBOX_H
