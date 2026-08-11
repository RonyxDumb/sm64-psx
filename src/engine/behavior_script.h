#ifndef BEHAVIOR_SCRIPT_H
#define BEHAVIOR_SCRIPT_H

#include <PR/ultratypes.h>

#define BHV_PROC_CONTINUE 0
#define BHV_PROC_BREAK    1

#define cur_obj_get_int(offset) gCurrentObject->OBJECT_FIELD_S32(offset)
#define cur_obj_get_float(offset) (FFIELD(gCurrentObject, OBJECT_FIELD_F32(offset)))
// TODO
#define cur_obj_get_q32(offset) (QFIELD(gCurrentObject, OBJECT_FIELD_F32(offset)))


#define cur_obj_add_float(offset, value) FMODFIELD(gCurrentObject, OBJECT_FIELD_F32(offset), += (f32)(value))
#define cur_obj_set_float(offset, value) FSETFIELD(gCurrentObject, OBJECT_FIELD_F32(offset), (f32)(value))
#define cur_obj_set_q32(offset, value) QSETFIELD(gCurrentObject, OBJECT_FIELD_F32(offset), (value))
// TODO
#define cur_obj_add_q32(offset, value) QMODFIELD(gCurrentObject, OBJECT_FIELD_F32(offset), += q((f32)(value)))
#define cur_obj_add_int(offset, value) gCurrentObject->OBJECT_FIELD_S32(offset) += (s32)(value)
#define cur_obj_set_int(offset, value) gCurrentObject->OBJECT_FIELD_S32(offset) = (s32)(value)
#define cur_obj_or_int(offset, value)  gCurrentObject->OBJECT_FIELD_S32(offset) |= (s32)(value)
#define cur_obj_and_int(offset, value) gCurrentObject->OBJECT_FIELD_S32(offset) &= (s32)(value)
#define cur_obj_set_vptr(offset, value) gCurrentObject->OBJECT_FIELD_VPTR(offset) = (void *)(value)

#define obj_and_int(object, offset, value) object->OBJECT_FIELD_S32(offset) &= (s32)(value)

u16 random_u16(void);
float random_float(void);
q32 random_q32(void);
s32 random_sign(void);

void cur_obj_update(void);

#endif // BEHAVIOR_SCRIPT_H
