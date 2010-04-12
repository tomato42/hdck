#ifndef _IOPRIO_H
#define _IOPRIO_H

static inline int ioprio_set(int which, int who, int ioprio)
{
        return syscall(SYS_ioprio_set, which, who, ioprio);
}

static inline int ioprio_get(int which, int who)
{
        return syscall(SYS_ioprio_get, which, who);
}

enum {
        IOPRIO_CLASS_NONE,
        IOPRIO_CLASS_RT,
        IOPRIO_CLASS_BE,
        IOPRIO_CLASS_IDLE,
};

enum {
        IOPRIO_WHO_PROCESS = 1,
        IOPRIO_WHO_PGRP,
        IOPRIO_WHO_USER,
};
#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_PRIO_VALUE(ioprio_class, ioprio_prio) ((ioprio_prio) | (ioprio_class) << IOPRIO_CLASS_SHIFT)

#endif // _IOPRIO_H
