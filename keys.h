/*
 * Netris -- A free networked version of T*tris
 * Copyright (C) 1994-1996,1999  Mark H. Weaver <mhw@netris.org>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef KEYS_H
#define KEYS_H

#define DEFAULT_KEYS "jJklL mspf^ln "

enum { 
    KT_left, 
    KT_full_left, 
    KT_rotate, 
    KT_right, 
    KT_full_right, 
    KT_drop, 
    KT_down,
	KT_toggleSpy, 
    KT_pause, 
    KT_faster, 
    KT_redraw, 
    KT_new, 
    KT_numKeys 
};

char keyTable[KT_numKeys + 1];
#endif
