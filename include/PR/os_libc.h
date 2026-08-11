#ifndef _OS_LIBC_H_
#define _OS_LIBC_H_

#include "ultratypes.h"
#include <string.h>

#define bcopy(b1,b2,len) (memmove((b2), (b1), (len)), (void) 0)
#define bzero(b,len) (memset((b), 0, (len)), (void) 0)

#endif /* !_OS_LIBC_H_ */
