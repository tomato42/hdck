/** hdck - hard drive low-level error and badsector checking 
 * 
 * Copyright (C) 2010  Hubert Kario
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
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
