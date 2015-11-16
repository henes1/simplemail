/***************************************************************************
 SimpleMail - Copyright (C) 2000 Hynek Schlawack and Sebastian Bauer

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
***************************************************************************/

/**
 * @file utf8stringclass.h
 */

#ifndef SM__UTF8STRING_HPP
#define SM__UTF8STRING_HPP

IMPORT struct MUI_CustomClass *CL_UTF8String;
#define UTF8StringObject (Object*)MyNewObject(CL_UTF8String->mcc_Class, NULL

#define MUIA_UTF8String_Contents (TAG_USER | 0x300A0001)
#define MUIA_UTF8String_Charset  (TAG_USER | 0x300A0002) /* (STRPTR) */

#define MUIM_UTF8String_Insert   (TAG_USER | 0x300A0101)

#define setutf8string(o,str) set(o,MUIA_UTF8String_Contents,str)
#define nnsetutf8string(o,str) nnset(o,MUIA_UTF8String_Contents,str)
#define getutf8string(o) (char*)xget(o,MUIA_UTF8String_Contents)

/**
 * Create the utf8 string custom class.
 *
 * @return 0 on failure, 1 on success
 */
int create_utf8string_class(void);

/**
 * Delete the utf8 string custom class.
 */
void delete_utf8string_class(void);

#endif
