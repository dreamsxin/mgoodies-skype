/*

dbx_tree: tree database driver for Miranda IM

Copyright 2007-2008 Michael "Protogenes" Kunz,

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#pragma once

inline long _InterlockedExchange(long volatile * Target, long Value)
{
    __asm__ __volatile__ ( "lock xchg %1, %0"
                 : "+m"(*Target),"=r"(Value)
                 : "1"(Value)
                 : "memory");
    return Value;
}
inline long _InterlockedExchangeAdd(long volatile * Addend, long Value)
{
    __asm__ __volatile__ ( "lock xadd %1, %0"
                 : "+m"(*Addend),"=r"(Value)
                 : "1"(Value)
                 : "memory");
    return Value;
}
inline long _InterlockedIncrement(long volatile * Addend)
{
    return _InterlockedExchangeAdd(Addend, 1) + 1;
}
inline long _InterlockedDecrement(long volatile * Addend)
{
    return _InterlockedExchangeAdd(Addend, -1) - 1;
}