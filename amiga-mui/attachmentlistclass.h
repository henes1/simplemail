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
 * @file attachmentlistclass.h
 */

#ifndef SM__ATTACHMENTLISTCLASS_H
#define SM__ATTACHMENTLISTCLASS_H

/* the objects of the listview */
IMPORT struct MUI_CustomClass *CL_AttachmentList;
#define AttachmentListObject (Object*)MyNewObject(CL_AttachmentList->mcc_Class, NULL

#define MUIA_AttachmentList_Quick        (TAG_USER | 0x30060001) /* (ULONG) */

#define MUIM_AttachmentList_FindUniqueID (TAG_USER | 0x30060101)
struct  MUIP_AttachmentList_FindUniqueID {ULONG MethodID; int unique_id;};

/**
 * Create the attachment list custom class.
 *
 * @return 0 on failure, 1 on success
 */
int create_attachmentlist_class(void);

/**
 * Delete the attachment list custom class.
 */
void delete_attachmentlist_class(void);

/* structure of a entry of this list */
struct attachment
{
	int unique_id; /* a unique id */
	char *filename;
	char *temporary_filename; /* path to the temporary file, it gets deleted if the entry is destructed */
	char *description;
	char *content_type;

	int size; /* size of the file */
	int editable; /* 1 text is editable */

	char *contents; /* text contents */
	int lastxcursor; /* last x position of the cursor */
	int lastycursor; /* last y position of the cursor */
};


#endif
