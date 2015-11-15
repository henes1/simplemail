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
 * @file mailinfo.h
 */

#ifndef SM__MAILINFOCLASS_H
#define SM__MAILINFOCLASS_H

/* the objects of the listview */
IMPORT struct MUI_CustomClass *CL_MailInfo;
#define MailInfoObject (Object*)MyNewObject(CL_MailInfo->mcc_Class, NULL

#define MUIA_MailInfo_MailInfo					(TAG_USER | 0x30070001) /* struct mail_info * */

/**
 * Create the mail info custom class.
 *
 * @return 0 on failure, 1 on success
 */
int create_mailinfo_class(void);

/**
 * Delete the mail info custom class.
 */
void delete_mailinfo_class(void);

#endif
