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

/*
** mailtreelistclass.c
*/

/* If mail list should be really a tree define the next */
#undef MAILLIST_IS_TREE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <libraries/iffparse.h>

#include <libraries/mui.h>
#include <mui/NListview_MCC.h>

#ifdef MAILLIST_IS_TREE
#include <mui/NListtree_Mcc.h>
#endif

#include <clib/alib_protos.h>
#include <proto/utility.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/intuition.h>

#include "codesets.h"
#include "configuration.h"
#include "debug.h"
#include "mail.h"
#include "folder.h"
#include "simplemail.h"
#include "smintl.h"

#include "amigasupport.h"
#include "compiler.h"
#include "mailtreelistclass.h"
#include "muistuff.h"
#include "picturebuttonclass.h"
#include "support_indep.h"

struct MUI_NListtree_TreeNode *FindListtreeUserData(Object *tree, APTR udata);

struct MailTreelist_Data
{
	struct Hook display_hook;
	int folder_type;

	APTR status_unread;
	APTR status_read;
	APTR status_waitsend;
	APTR status_sent;
	APTR status_mark;
	APTR status_hold;
	APTR status_reply;
	APTR status_forward;
	APTR status_norcpt;
	APTR status_new_partial;
	APTR status_read_partial;
	APTR status_unread_partial;
	APTR status_reply_partial;
	APTR status_new_spam;
	APTR status_unread_spam;

	APTR status_important;
	APTR status_attach;
	APTR status_group;
	APTR status_new;
	APTR status_crypt;
	APTR status_signed;
	APTR status_trashcan;

	Object *context_menu;
	Object *title_menu;

	Object *show_from_item;
	Object *show_subject_item;
	Object *show_reply_item;
	Object *show_date_item;
	Object *show_size_item;
	Object *show_filename_item;
	Object *show_pop3_item;
	Object *show_recv_item;

	/* translated strings (faster to hold the translation) */
	char *status_text;
	char *from_text;
	char *to_text;
	char *subject_text;
	char *reply_text;
	char *date_text;
	char *size_text;
	char *filename_text;
	char *pop3_text;
	char *received_text;

	/* the converted strings */
	char fromto_buf[256];
	char subject_buf[512];
	char reply_buf[256];

	char bubblehelp_buf[4096];
};

static char *mailtree_get_fromto(struct MailTreelist_Data *data, struct mail *mail)
{
	char *field;
	char *dest;
	int ascii7;

	if (data->folder_type == FOLDER_TYPE_SEND)
	{
		if (mail->flags & MAIL_FLAGS_NORCPT)
		{
			field = _("<No Recipient>");
			ascii7 = 1;
		} else
		{
			field = mail->to_phrase;
			ascii7 = !!(mail->flags & MAIL_FLAGS_TO_ASCII7);
			if (!field)
			{
				field = mail->to_addr;
				ascii7 = 1;
			}
		}
	} else
	{
		field = mail->from_phrase;
		ascii7 = !!(mail->flags & MAIL_FLAGS_FROM_ASCII7);
		if (!field)
		{
			field = mail->from_addr;
			ascii7 = 1;
		}
	}

	if (!field)
	{
		field = "";
		ascii7 = 1;
	}

	if (!(mail->flags & MAIL_FLAGS_GROUP) && ascii7)
		return field;

	dest = data->fromto_buf;
	if (mail->flags & MAIL_FLAGS_GROUP)
	{
		sprintf(dest,"\33O[%08lx]",data->status_group);
		dest += strlen(dest);
	}

	if (ascii7)
		strcpy(dest,field);
	else
		utf8tostr(field,dest,sizeof(data->fromto_buf) - (dest - data->fromto_buf),user.config.default_codeset);

	return data->fromto_buf;
}


#ifdef MAILLIST_IS_TREE
STATIC ASM SAVEDS VOID mails_display(REG(a0,struct Hook *h),REG(a2,Object *obj), REG(a1,struct MUIP_NListtree_DisplayMessage *msg))
{
	char **array = msg->Array;
	char **preparse = msg->Preparse;
	struct mail *mail;

	if (msg->TreeNode)
	{
		mail = (struct mail*)msg->TreeNode->tn_User;
	} else mail = NULL;
#else
STATIC ASM SAVEDS VOID mails_display(REG(a0,struct Hook *h),REG(a2,Object *obj), REG(a1,struct NList_DisplayMessage *msg))
{
	char **array = msg->strings;
	char **preparse = msg->preparses;
	struct mail *mail;

	mail = (struct mail*)msg->entry;
#endif

	{
		struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(CL_MailTreelist->mcc_Class,obj);

		if (mail)
		{
			if (mail == (struct mail*)MUIV_MailTreelist_UserData_Name)
			{
				/* only one string should be displayed */
				*array++ = NULL; /* status */
				*array++ = NULL;
				*array++ = NULL;
				*array++ = NULL;
				*array++ = NULL;
				*array++ = NULL;
				*array = NULL;
			} else
			{
				/* is a mail */
				APTR status;

				static char size_buf[32];
				static char date_buf[64];
				static char recv_buf[64];
				static char status_buf[128];

				if (mail->flags & MAIL_FLAGS_AUTOSPAM)
				{
					sprintf(status_buf,"\33O[%08lx]",data->status_new_spam);
				} else
				if (mail->flags & MAIL_FLAGS_NEW)
				{
					if (mail_is_spam(mail)) sprintf(status_buf,"\33O[%08lx]",data->status_new_spam);
					else if (mail->flags & MAIL_FLAGS_PARTIAL)	sprintf(status_buf,"\33O[%08lx]",data->status_new_partial);
					else sprintf(status_buf,"\33O[%08lx]",data->status_new);

					*preparse++ = "\33b";
					*preparse++ = "\33b";
					*preparse++ = "\33b";
					*preparse++ = "\33b";
					*preparse++ = "\33b";
					*preparse++ = "\33b";
					*preparse = "\33b";
				} else
				{
					if (mail_is_spam(mail)) sprintf(status_buf,"\33O[%08lx]",data->status_unread_spam);
					else if ((mail->flags & MAIL_FLAGS_NORCPT) && data->folder_type == FOLDER_TYPE_SEND) sprintf(status_buf,"\33O[%08lx]",data->status_norcpt);
					else
					{
						if (mail->flags & MAIL_FLAGS_PARTIAL)
						{
							switch(mail_get_status_type(mail))
							{
								case MAIL_STATUS_READ: status = data->status_read_partial;break;
								case MAIL_STATUS_REPLIED: status = data->status_reply_partial;break;
								default: status = data->status_unread_partial;break;
							}
						} else
						{
							switch(mail_get_status_type(mail))
							{
								case	MAIL_STATUS_UNREAD:status = data->status_unread;break;
								case	MAIL_STATUS_READ:status = data->status_read;break;
								case	MAIL_STATUS_WAITSEND:status = data->status_waitsend;break;
								case	MAIL_STATUS_SENT:status = data->status_sent;break;
								case	MAIL_STATUS_HOLD:status = data->status_hold;break;
								case	MAIL_STATUS_REPLIED:status = data->status_reply;break;
								case	MAIL_STATUS_FORWARD:status = data->status_forward;break;
								default: status = NULL;
							}
						}
						sprintf(status_buf,"\33O[%08lx]",status);
					}
				}

				if (mail->status & MAIL_STATUS_FLAG_MARKED) sprintf(status_buf+strlen(status_buf),"\33O[%08lx]",data->status_mark);
				if (mail->flags & MAIL_FLAGS_IMPORTANT) sprintf(status_buf+strlen(status_buf),"\33O[%08lx]",data->status_important);
				if (mail->flags & MAIL_FLAGS_CRYPT) sprintf(status_buf+strlen(status_buf),"\33O[%08lx]",data->status_crypt);
				else
				{
					if (mail->flags & MAIL_FLAGS_SIGNED) sprintf(status_buf+strlen(status_buf),"\33O[%08lx]",data->status_signed);
					else if (mail->flags & MAIL_FLAGS_ATTACH) sprintf(status_buf+strlen(status_buf),"\33O[%08lx]",data->status_attach);
				}
				if (mail_is_marked_as_deleted(mail)) sprintf(status_buf+strlen(status_buf),"\33O[%08lx]",data->status_trashcan);

				sprintf(size_buf,"%ld",mail->size);
				SecondsToString(date_buf,mail->seconds);

				if (xget(data->show_recv_item,MUIA_Menuitem_Checked))
					SecondsToString(recv_buf,mail->received);

				utf8tostr(mail->subject,data->subject_buf,sizeof(data->subject_buf),user.config.default_codeset);

				*array++ = status_buf; /* status */
				*array++ = mailtree_get_fromto(data,mail);
				*array++ = data->subject_buf;
				*array++ = mail->reply_addr;
				*array++ = date_buf;
				*array++ = size_buf;
				*array++ = mail->filename;
				*array++ = mail->pop3_server;
				*array = recv_buf;
			}
		} else
		{
			*array++ = data->status_text;

			if (data->folder_type != FOLDER_TYPE_SEND)
				*array++ = data->from_text;
			else *array++ = data->to_text;

			*array++ = data->subject_text;
			*array++ = data->reply_text;
			*array++ = data->date_text;
			*array++ = data->size_text;
			*array++ = data->filename_text;
			*array++ = data->pop3_text;
			*array = data->received_text;
		}	
	}
}

STATIC VOID MailTreelist_SetNotified(void **msg)
{
	Object *obj = (Object*)msg[0];
	struct IClass *cl = (struct IClass*)msg[1];
	struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(cl,obj);
	struct mail *m;

#ifdef MAILLIST_IS_TREE
	struct MUI_NListtree_TreeNode *treenode = (struct MUI_NListtree_TreeNode *)xget(obj, MUIA_NListtree_Active);

	if (treenode && treenode->tn_User) m = (struct mail*)treenode->tn_User;
	else m = NULL;
#else
	DoMethod(obj, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &m);
#endif

	if (m)
	{
		if (m != (struct mail*)MUIV_MailTreelist_UserData_Name)
		{
			char *from = mail_get_from_address(m);
			char *to = mail_get_to_address(m);
			char *replyto = mail_get_replyto_address(m);
			char date_buf[64];
			char recv_buf[64];
			char *buf = data->bubblehelp_buf;

			SecondsToString(date_buf,m->seconds);
			SecondsToString(recv_buf,m->received);

			/* Help bubble text */
			sprintf(buf,"\33b%s\33n",_("Current Message"));
			buf += strlen(buf);
			if (m->subject)
			{
				*buf++ = '\n';
				buf = mystpcpy(buf,data->subject_text);
				*buf++ = ':';
				*buf++ = ' ';
				buf += utf8tostr(m->subject,buf,sizeof(data->bubblehelp_buf) - (buf - data->bubblehelp_buf),user.config.default_codeset);
			}

			if (from)
			{
				*buf++ = '\n';
				buf = mystpcpy(buf,data->from_text);
				*buf++ = ':';
				*buf++ = ' ';
				buf += utf8tostr(from,buf,sizeof(data->bubblehelp_buf) - (buf - data->bubblehelp_buf),user.config.default_codeset);
			}

			if (to)
			{
				*buf++ = '\n';
				buf = mystpcpy(buf,data->to_text);
				*buf++ = ':';
				*buf++ = ' ';
				buf += utf8tostr(to,buf,sizeof(data->bubblehelp_buf) - (buf - data->bubblehelp_buf),user.config.default_codeset);
			}

			if (replyto)
			{
				*buf++ = '\n';
				buf = mystpcpy(buf,data->reply_text);
				*buf++ = ':';
				*buf++ = ' ';
				buf = mystpcpy(buf,replyto);
			}

			sprintf(buf,"\n%s: %s\n%s: %s\n%s: %d\n%s: %s\n%s: %s",
							data->date_text, date_buf,
							data->received_text, recv_buf,
							data->size_text, m->size,
							data->pop3_text, m->pop3_server?m->pop3_server:"",
							data->filename_text, m->filename);

			set(obj,MUIA_ShortHelp,data->bubblehelp_buf);

			free(replyto);
			free(to);
			free(from);
		} else
		{
			set(obj,MUIA_ShortHelp,NULL);
		}
	}	
}

STATIC VOID MailTreelist_UpdateFormat(struct IClass *cl,Object *obj)
{
	struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(cl,obj);
	char buf[256];

	strcpy(buf,"COL=0 BAR");

	if (xget(data->show_from_item,MUIA_Menuitem_Checked)) strcat(buf,",COL=1 BAR");
	if (xget(data->show_subject_item,MUIA_Menuitem_Checked)) strcat(buf,",COL=2 BAR");
	if (xget(data->show_reply_item,MUIA_Menuitem_Checked)) strcat(buf,",COL=3 BAR");
	if (xget(data->show_date_item,MUIA_Menuitem_Checked)) strcat(buf,",COL=4 BAR");
	if (xget(data->show_size_item,MUIA_Menuitem_Checked)) strcat(buf,",COL=5 P=\33r BAR");
	if (xget(data->show_filename_item,MUIA_Menuitem_Checked)) strcat(buf,",COL=6 BAR");
	if (xget(data->show_pop3_item,MUIA_Menuitem_Checked)) strcat(buf,",COL=7 BAR");
	if (xget(data->show_recv_item,MUIA_Menuitem_Checked)) strcat(buf,",COL=8 BAR");

#ifdef MAILLIST_IS_TREE
	set(obj, MUIA_NListtree_Format, buf);
#else
	set(obj, MUIA_NList_Format, buf);
#endif
}

STATIC ULONG MailTreelist_New(struct IClass *cl,Object *obj,struct opSet *msg)
{
	struct MailTreelist_Data *data;

	if (!(obj=(Object *)DoSuperNew(cl,obj,
#ifdef MAILLIST_IS_TREE
					MUIA_NListtree_MultiSelect,MUIV_NListtree_MultiSelect_Default/*|MUIV_NListtree_MultiSelect_Flag_AutoSelectChilds*/,
					MUIA_NListtree_DupNodeName, FALSE,
#else
					MUIA_NList_MultiSelect, MUIV_NList_MultiSelect_Default,
#endif
					MUIA_ContextMenu, MUIV_NList_ContextMenu_Always,
					TAG_MORE,msg->ops_AttrList)))
		return 0;

	data = (struct MailTreelist_Data*)INST_DATA(cl,obj);

	data->status_text = _("Status");
	data->from_text = _("From");
	data->to_text = _("To");
	data->subject_text = _("Subject");
	data->reply_text = _("Reply");
	data->date_text = _("Date");
	data->size_text = _("Size");
	data->filename_text = _("Filename");
	data->pop3_text = _("POP3 Server");
	data->received_text = _("Received");

	init_hook(&data->display_hook,(HOOKFUNC)mails_display);

#ifdef MAILLIST_IS_TREE
	SetAttrs(obj,
						MUIA_NListtree_DisplayHook, &data->display_hook,
						MUIA_NListtree_Title, TRUE,
						TAG_DONE);
#else
	SetAttrs(obj,
						MUIA_NList_DisplayHook2, &data->display_hook,
						MUIA_NList_Title, TRUE,
						MUIA_NList_DragType, MUIV_NList_DragType_Default,
						TAG_DONE);
#endif

	data->title_menu = MenustripObject,
		Child, MenuObjectT(_("Mail Settings")),
			Child, data->show_from_item = MenuitemObject, MUIA_ObjectID, MAKE_ID('M','S','F','T'),MUIA_Menuitem_Title, _("Show From/To?"), MUIA_UserData, 1, MUIA_Menuitem_Checked, TRUE, MUIA_Menuitem_Checkit, TRUE, MUIA_Menuitem_Toggle, TRUE, End,
			Child, data->show_subject_item = MenuitemObject, MUIA_ObjectID, MAKE_ID('M','S','S','B'),MUIA_Menuitem_Title, _("Show Subject?"), MUIA_UserData, 2, MUIA_Menuitem_Checked, TRUE, MUIA_Menuitem_Checkit, TRUE, MUIA_Menuitem_Toggle, TRUE, End,
			Child, data->show_reply_item = MenuitemObject, MUIA_ObjectID, MAKE_ID('M','S','R','T'),MUIA_Menuitem_Title, _("Show Reply-To?"), MUIA_UserData, 3, MUIA_Menuitem_Checked, TRUE, MUIA_Menuitem_Checkit, TRUE, MUIA_Menuitem_Toggle, TRUE, End,
			Child, data->show_date_item = MenuitemObject, MUIA_ObjectID, MAKE_ID('M','S','D','T'),MUIA_Menuitem_Title, _("Show Date?"), MUIA_UserData, 4, MUIA_Menuitem_Checked, TRUE, MUIA_Menuitem_Checkit, TRUE, MUIA_Menuitem_Toggle, TRUE, End,
			Child, data->show_size_item = MenuitemObject, MUIA_ObjectID, MAKE_ID('M','S','S','Z'),MUIA_Menuitem_Title, _("Show Size?"), MUIA_UserData, 5, MUIA_Menuitem_Checked, TRUE, MUIA_Menuitem_Checkit, TRUE, MUIA_Menuitem_Toggle, TRUE, End,
			Child, data->show_filename_item = MenuitemObject, MUIA_ObjectID, MAKE_ID('M','S','F','N'), MUIA_Menuitem_Title, _("Show Filename?"), MUIA_UserData, 6,  MUIA_Menuitem_Checked, TRUE, MUIA_Menuitem_Checkit, TRUE, MUIA_Menuitem_Toggle, TRUE, End,
			Child, data->show_pop3_item = MenuitemObject, MUIA_ObjectID, MAKE_ID('M','S','P','3'),MUIA_Menuitem_Title, _("Show POP3 Server?"), MUIA_UserData, 7, MUIA_Menuitem_Checked, TRUE, MUIA_Menuitem_Checkit, TRUE, MUIA_Menuitem_Toggle, TRUE, End,
			Child, data->show_recv_item = MenuitemObject, MUIA_ObjectID, MAKE_ID('M','S','R','V'), MUIA_Menuitem_Title, _("Show Received?"), MUIA_UserData, 8,  MUIA_Menuitem_Checked, TRUE, MUIA_Menuitem_Checkit, TRUE, MUIA_Menuitem_Toggle, TRUE, End,
			Child, MenuitemObject, MUIA_Menuitem_Title, -1, End,
			Child, MenuitemObject, MUIA_Menuitem_Title, _("Default Width: this"), MUIA_UserData, MUIV_NList_Menu_DefWidth_This, End,
			Child, MenuitemObject, MUIA_Menuitem_Title, _("Default Width: all"), MUIA_UserData, MUIV_NList_Menu_DefWidth_All, End,
			Child, MenuitemObject, MUIA_Menuitem_Title, _("Default Order: this"), MUIA_UserData, MUIV_NList_Menu_DefOrder_This, End,
			Child, MenuitemObject, MUIA_Menuitem_Title, _("Default Order: all"), MUIA_UserData, MUIV_NList_Menu_DefOrder_All, End,
			End,
		End;

	MailTreelist_UpdateFormat(cl,obj);

#ifdef MAILLIST_IS_TREE
	DoMethod(obj, MUIM_Notify, MUIA_NListtree_Active, MUIV_EveryTime, App, 5, MUIM_CallHook, &hook_standard, MailTreelist_SetNotified, obj, cl);
	DoMethod(obj, MUIM_Notify, MUIA_NListtree_DoubleClick, MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_MailTree_DoubleClick, TRUE);
#else
	DoMethod(obj, MUIM_Notify, MUIA_NList_Active, MUIV_EveryTime, App, 5, MUIM_CallHook, &hook_standard, MailTreelist_SetNotified, obj, cl);
	DoMethod(obj, MUIM_Notify, MUIA_NList_DoubleClick, MUIV_EveryTime, obj, 3, MUIM_Set, MUIA_MailTree_DoubleClick, TRUE);
#endif

	return (ULONG)obj;
}

STATIC ULONG MailTreelist_Dispose(struct IClass *cl, Object *obj, Msg msg)
{
	struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(cl,obj);
	if (data->context_menu) MUI_DisposeObject(data->context_menu);
	if (data->title_menu) MUI_DisposeObject(data->title_menu);
	return DoSuperMethodA(cl,obj,msg);
}

STATIC ULONG MailTreelist_Set(struct IClass *cl, Object *obj, struct opSet *msg)
{
	struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(cl,obj);
	struct TagItem *tstate, *tag;

	tstate = (struct TagItem *)msg->ops_AttrList;

	while (tag = NextTagItem (&tstate))
	{
/*		ULONG tidata = tag->ti_Data;*/

		switch (tag->ti_Tag)
		{
			case	MUIA_MailTree_DoubleClick:
						break;

			case	MUIA_MailTreelist_FolderType:
						if (data->folder_type != tag->ti_Data)
						{
							data->folder_type = tag->ti_Data;
						}
						break;

			case	MUIA_MailTree_Active:
						{
							struct mail *m = (struct mail*)tag->ti_Data;

#ifdef MAILLIST_IS_TREE
							struct MUI_NListtree_TreeNode *tn = FindListtreeUserData(obj,m);
							set(obj,MUIA_NListtree_Active,tn);
#else
							int i;
							for (i=0;i<xget(obj,MUIA_NList_Entries);i++)
							{
								struct mail *m2;
								DoMethod(obj,MUIM_NList_GetEntry,i,&m2);
								if (m == m2)
								{
									set(obj,MUIA_NList_Active,i);
									break;
								}
							}
#endif
						}
						break;
		}
	}

	return DoSuperMethodA(cl,obj,(Msg)msg);
}

STATIC ULONG MailTreelist_Get(struct IClass *cl, Object *obj, struct opGet *msg)
{
	if (msg->opg_AttrID == MUIA_MailTree_Active)
	{
#ifdef MAILLIST_IS_TREE
		struct MUI_NListtree_TreeNode *tree_node;
		tree_node = (struct MUI_NListtree_TreeNode *)xget(obj,MUIA_NListtree_Active);

		if (tree_node)
		{
			if (tree_node->tn_User && tree_node->tn_User != (void*)MUIV_MailTreelist_UserData_Name)
			{
				*msg->opg_Storage = (struct mail*)tree_node->tn_User;
				return 1;
			}
		}
		*msg->opg_Storage = 0;
#else
		DoMethod(obj, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, msg->opg_Storage);
#endif
		return 1;
	}

	if (msg->opg_AttrID == MUIA_MailTree_DoubleClick)
	{
		*msg->opg_Storage = 0;
		return 1;
	}

	return DoSuperMethodA(cl,obj,(Msg)msg);
}

STATIC ULONG MailTreelist_Setup(struct IClass *cl, Object *obj, struct MUIP_Setup *msg)
{
	struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(cl,obj);

	SM_ENTER;

	if (!DoSuperMethodA(cl,obj,(Msg)msg))
	{
		SM_LEAVE;
		return 0;
	}

	data->status_unread = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_unread", End, 0);
	data->status_unread_partial = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_unread_partial", End, 0);
	data->status_read_partial = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_old_partial", End, 0);
	data->status_reply_partial = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_reply_partial", End, 0);
	data->status_read = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_old", End, 0);
	data->status_waitsend = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_waitsend", End, 0);
	data->status_sent = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_sent", End, 0);
	data->status_mark = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_mark", End, 0);
	data->status_hold = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_hold", End, 0);
	data->status_reply = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_reply", End, 0);
	data->status_forward = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_forward", End, 0);
	data->status_norcpt = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_norcpt", End, 0);
	data->status_new_partial = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_new_partial", End, 0);
	data->status_new_spam = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_new_spam",End,0);
	data->status_unread_spam = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_unread_spam",End,0);

	data->status_important = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_urgent", End, 0);
	data->status_attach = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_attach", End, 0);
	data->status_group = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_group", End, 0);
	data->status_new = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_new", End, 0);
	data->status_crypt = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_crypt", End, 0);
	data->status_signed = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_signed", End, 0);
	data->status_trashcan = (APTR)DoMethod(obj, MUIM_NList_CreateImage, PictureButtonObject, MUIA_PictureButton_Filename, "PROGDIR:Images/status_trashcan", End, 0);

	SM_LEAVE;
	return 1;
}

STATIC ULONG MailTreelist_Cleanup(struct IClass *cl, Object *obj, Msg msg)
{
	struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(cl,obj);
	if (data->status_trashcan) DoMethod(obj, MUIM_NList_DeleteImage, data->status_trashcan);
	if (data->status_signed) DoMethod(obj, MUIM_NList_DeleteImage, data->status_signed);
	if (data->status_crypt) DoMethod(obj, MUIM_NList_DeleteImage, data->status_crypt);
	if (data->status_new) DoMethod(obj, MUIM_NList_DeleteImage, data->status_new);
	if (data->status_group) DoMethod(obj, MUIM_NList_DeleteImage, data->status_group);
	if (data->status_attach) DoMethod(obj, MUIM_NList_DeleteImage, data->status_attach);
	if (data->status_important) DoMethod(obj, MUIM_NList_DeleteImage, data->status_important);
	if (data->status_new_spam) DoMethod(obj, MUIM_NList_DeleteImage, data->status_new_spam);
	if (data->status_unread_spam) DoMethod(obj, MUIM_NList_DeleteImage, data->status_unread_spam);

	if (data->status_reply_partial) DoMethod(obj, MUIM_NList_DeleteImage, data->status_reply_partial);
	if (data->status_read_partial) DoMethod(obj, MUIM_NList_DeleteImage, data->status_read_partial);
	if (data->status_new_partial) DoMethod(obj, MUIM_NList_DeleteImage, data->status_new_partial);
	if (data->status_norcpt) DoMethod(obj, MUIM_NList_DeleteImage, data->status_norcpt);
	if (data->status_hold) DoMethod(obj, MUIM_NList_DeleteImage, data->status_hold);
	if (data->status_mark) DoMethod(obj, MUIM_NList_DeleteImage, data->status_mark);
	if (data->status_reply) DoMethod(obj, MUIM_NList_DeleteImage, data->status_reply);
	if (data->status_forward) DoMethod(obj, MUIM_NList_DeleteImage, data->status_forward);
	if (data->status_unread_partial) DoMethod(obj, MUIM_NList_DeleteImage, data->status_unread_partial);
	if (data->status_unread) DoMethod(obj, MUIM_NList_DeleteImage, data->status_unread);
	if (data->status_read) DoMethod(obj, MUIM_NList_DeleteImage, data->status_read);
	if (data->status_waitsend) DoMethod(obj, MUIM_NList_DeleteImage, data->status_waitsend);
	if (data->status_sent) DoMethod(obj, MUIM_NList_DeleteImage, data->status_sent);
	return DoSuperMethodA(cl,obj,msg);
}

STATIC ULONG MailTreelist_DragQuery(struct IClass *cl, Object *obj, struct MUIP_DragDrop *msg)
{
  if (msg->obj==obj) return MUIV_DragQuery_Refuse; /* mails should not be resorted by the user */
  return DoSuperMethodA(cl,obj,(Msg)msg);
}

#ifdef MAILLIST_IS_TREE
STATIC ULONG MailTreelist_MultiTest(struct IClass *cl, Object *obj, struct MUIP_NListtree_MultiTest *msg)
{
	if (msg->TreeNode->tn_User == (APTR)MUIV_MailTreelist_UserData_Name) return FALSE;
	return TRUE;
}
#endif

STATIC ULONG MailTreelist_Export(struct IClass *cl, Object *obj, struct MUIP_Export *msg)
{
	struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(cl,obj);

	DoMethodA(data->show_from_item, (Msg)msg);
	DoMethodA(data->show_subject_item, (Msg)msg);
	DoMethodA(data->show_reply_item, (Msg)msg);
	DoMethodA(data->show_date_item, (Msg)msg);
	DoMethodA(data->show_size_item, (Msg)msg);
	DoMethodA(data->show_filename_item, (Msg)msg);
	DoMethodA(data->show_pop3_item, (Msg)msg);
	DoMethodA(data->show_recv_item, (Msg)msg);
	return DoSuperMethodA(cl,obj,(Msg)msg);
}

STATIC ULONG MailTreelist_Import(struct IClass *cl, Object *obj, struct MUIP_Import *msg)
{
	struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(cl,obj);

	DoMethodA(data->show_from_item, (Msg)msg);
	DoMethodA(data->show_subject_item, (Msg)msg);
	DoMethodA(data->show_reply_item, (Msg)msg);
	DoMethodA(data->show_date_item, (Msg)msg);
	DoMethodA(data->show_size_item, (Msg)msg);
	DoMethodA(data->show_filename_item, (Msg)msg);
	DoMethodA(data->show_pop3_item, (Msg)msg);
	DoMethodA(data->show_recv_item, (Msg)msg);

	MailTreelist_UpdateFormat(cl,obj);

	return DoSuperMethodA(cl,obj,(Msg)msg);
}


#define MENU_SETSTATUS_MARK   9
#define MENU_SETSTATUS_UNMARK 10
#define MENU_SETSTATUS_READ   11
#define MENU_SETSTATUS_UNREAD 12
#define MENU_SETSTATUS_HOLD	13
#define MENU_SETSTATUS_WAITSEND  14
#define MENU_SETSTATUS_SPAM 15
#define MENU_SETSTATUS_HAM 16
#define MENU_SPAMCHECK 17
#define MENU_DELETE 18

STATIC ULONG MailTreelist_NList_ContextMenuBuild(struct IClass *cl, Object * obj, struct MUIP_NList_ContextMenuBuild *msg)
{
	struct MailTreelist_Data *data = (struct MailTreelist_Data*)INST_DATA(cl,obj);
  Object *context_menu;

  if (data->context_menu)
  {
  	MUI_DisposeObject(data->context_menu);
  	data->context_menu = NULL;
  }

	if (msg->ontop) return (ULONG)data->title_menu; /* The default NList Menu should be returned */

	context_menu = MenustripObject,
		Child, MenuObjectT(_("Mail")),
			Child, MenuitemObject, MUIA_Menuitem_Title, _("Spam Check"), MUIA_UserData, MENU_SPAMCHECK, End,
			Child, MenuitemObject, MUIA_Menuitem_Title, _("Set status"),
				Child, MenuitemObject, MUIA_Menuitem_Title, _("Mark"), MUIA_UserData, MENU_SETSTATUS_MARK, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, _("Unmark"), MUIA_UserData, MENU_SETSTATUS_UNMARK, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, ~0, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, _("Hold"), MUIA_UserData, MENU_SETSTATUS_HOLD, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, _("Pending"), MUIA_UserData, MENU_SETSTATUS_WAITSEND, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, ~0, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, _("Read"), MUIA_UserData, MENU_SETSTATUS_READ, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, _("Unread"), MUIA_UserData, MENU_SETSTATUS_UNREAD, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, ~0, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, _("Is Spam"), MUIA_UserData, MENU_SETSTATUS_SPAM, End,
				Child, MenuitemObject, MUIA_Menuitem_Title, _("Is Ham"), MUIA_UserData, MENU_SETSTATUS_HAM, End,
				End,
			Child, MenuitemObject, MUIA_Menuitem_Title, _("Delete"), MUIA_UserData, MENU_DELETE, End,
			End,
		End;

  data->context_menu = context_menu;
  return (ULONG) context_menu;
}

STATIC ULONG MailTreelist_ContextMenuChoice(struct IClass *cl, Object *obj, struct MUIP_ContextMenuChoice *msg)
{
	switch (xget(msg->item,MUIA_UserData))
	{
		case	1:
		case  2:
		case  3:
		case  4:
		case  5:
		case  6:
		case	7:
		case	8:
				  MailTreelist_UpdateFormat(cl,obj);
				  break;
		case	MENU_SETSTATUS_MARK: callback_mails_mark(1); break;
		case	MENU_SETSTATUS_UNMARK: callback_mails_mark(0); break;
		case	MENU_SETSTATUS_READ: callback_mails_set_status(MAIL_STATUS_READ); break;
		case	MENU_SETSTATUS_UNREAD: callback_mails_set_status(MAIL_STATUS_UNREAD); break;
		case	MENU_SETSTATUS_HOLD: callback_mails_set_status(MAIL_STATUS_HOLD); break;
		case	MENU_SETSTATUS_WAITSEND: callback_mails_set_status(MAIL_STATUS_WAITSEND); break;
		case  MENU_SETSTATUS_SPAM: callback_selected_mails_are_spam();break;
		case  MENU_SETSTATUS_HAM: callback_selected_mails_are_ham();break;
		case  MENU_SPAMCHECK: callback_check_selected_mails_if_spam();break;
		case  MENU_DELETE: callback_delete_mails();break;
		default: 
		{
			return DoSuperMethodA(cl,obj,(Msg)msg);
		}
	}
  return 0;
}

STATIC ULONG MailTreelist_Clear(struct IClass *cl, Object *obj, Msg msg)
{
#ifdef MAILLIST_IS_TREE
	return DoMethod(obj, MUIM_NListtree_Clear, NULL, 0);
#else
	return DoMethod(obj, MUIM_NList_Clear);
#endif
}

#ifdef MAILLIST_IS_TREE
/******************************************************************
 Updates the mail trees with the mails in the given folder
*******************************************************************/
static void main_insert_mail_threaded(Object *obj, struct mail *mail, void *parentnode)
{
	int mail_flags = 0;
	struct mail *submail;
	APTR newnode;

	if ((submail = mail->sub_thread_mail))
	{
		mail_flags = TNF_LIST|TNF_OPEN;
	}

	newnode = (APTR)DoMethod(obj,MUIM_NListtree_Insert,"" /*name*/, mail, /*udata */
				 parentnode,MUIV_NListtree_Insert_PrevNode_Tail,mail_flags);

	while (submail)
	{
		main_insert_mail_threaded(obj,submail,newnode);
		submail = submail->next_thread_mail;
	}
}
#endif

STATIC ULONG MailTreelist_SetFolderMails(struct IClass *cl, Object *obj, struct MUIP_MailTree_SetFolderMails *msg)
{
#ifdef MAILTREE_IS_TREE
	struct mail *m;
	void *handle = NULL;
#endif
	struct folder *folder = msg->f;
	int primary_sort, threaded;

	if (!folder)
	{
		DoMethod(obj, MUIM_MailTree_Clear);
		return NULL;
	}

	primary_sort = folder_get_primary_sort(folder)&FOLDER_SORT_MODEMASK;
  threaded = folder->type == FOLDER_TYPE_MAILINGLIST;

	DoMethod(obj, MUIM_MailTree_Freeze);
	DoMethod(obj, MUIM_MailTree_Clear);
	set(obj, MUIA_MailTreelist_FolderType, folder_get_type(folder));

#ifdef MAILLIST_IS_TREE
	if ((primary_sort == FOLDER_SORT_FROMTO || primary_sort == FOLDER_SORT_SUBJECT) && !threaded)
	{
		struct mail *lm = NULL; /* last mail */
		APTR treenode = NULL;

		SetAttrs(obj,
				MUIA_NListtree_TreeColumn, (primary_sort==FOLDER_SORT_SUBJECT)?2:1,
				MUIA_NListtree_ShowTree, TRUE,
				TAG_DONE);

		while ((m = folder_next_mail(folder,&handle)))
		{
			if (primary_sort == FOLDER_SORT_FROMTO)
			{
				int res;
				char *m_to = mail_get_to(m);
				char *m_from = mail_get_from(m);

				if (lm)
				{
					if (folder_get_type(folder) == FOLDER_TYPE_SEND) res = utf8stricmp(m_to, mail_get_to(lm));
					else res = utf8stricmp(m_from, mail_get_from(lm));
				}

				if (!lm || res)
				{
					treenode = (APTR)DoMethod(obj, MUIM_NListtree_Insert, (folder_get_type(folder) == FOLDER_TYPE_SEND)?m_to:m_from, MUIV_MailTreelist_UserData_Name, /* special hint */
							 MUIV_NListtree_Insert_ListNode_Root,MUIV_NListtree_Insert_PrevNode_Tail,TNF_LIST/*|TNF_OPEN*/);
				}
			} else
			{
				if (!lm || utf8stricmp(m->subject,lm->subject))
				{
					treenode = (APTR)DoMethod(obj, MUIM_NListtree_Insert, m->subject, MUIV_MailTreelist_UserData_Name, /* special hint */
							 MUIV_NListtree_Insert_ListNode_Root,MUIV_NListtree_Insert_PrevNode_Tail,TNF_LIST/*|TNF_OPEN*/);
				}
			}

			if (!treenode) break;

			DoMethod(obj, MUIM_NListtree_Insert,"" /*name*/, m, /*udata */
						 treenode,MUIV_NListtree_Insert_PrevNode_Tail,0/*flags*/);

			lm = m;
		}
	} else
	{
		if (!threaded)
		{
			SetAttrs(obj,
					MUIA_NListtree_TreeColumn, 0,
					MUIA_NListtree_ShowTree, FALSE,
					TAG_DONE);

			while ((m = folder_next_mail(folder,&handle)))
			{
				DoMethod(obj,MUIM_NListtree_Insert,"" /*name*/, m, /*udata */
							 MUIV_NListtree_Insert_ListNode_Root,MUIV_NListtree_Insert_PrevNode_Tail,0/*flags*/);
			}
		} else
		{
			SetAttrs(obj,
					MUIA_NListtree_TreeColumn, 0,
					MUIA_NListtree_ShowTree, TRUE,
					TAG_DONE);

			while ((m = folder_next_mail(folder,&handle)))
			{
				if (!m->child_mail)
					main_insert_mail_threaded(obj, m,(void*)MUIV_NListtree_Insert_ListNode_Root);
			}
		}
	}

	if ((m = folder_find_best_mail_to_select(folder)))
		set(obj, MUIA_NListtree_Active, FindListtreeUserData(obj, m));
#else
	{
		int i;
		struct mail **array = folder_get_mail_array(folder);

		DoMethod(obj, MUIM_NList_Insert, array, folder->num_mails, MUIV_NList_Insert_Bottom);

		for (i=0;i<folder->num_mails;i++)
		{
			if (mail_get_status_type(array[i]) == MAIL_STATUS_UNREAD)
			{
				set(obj,MUIA_NList_Active,i);
				break;
			}
		}
	}

/*
	while ((m = folder_next_mail(folder,&handle)))
	{
		DoMethod(obj, MUIM_NList_InsertSingle, m, MUIV_NList_Insert_Bottom);
	}
*/
#endif

	DoMethod(obj, MUIM_MailTree_Thaw);
	return NULL;
}

STATIC ULONG MailTreelist_Freeze(struct IClass *cl, Object *obj, Msg msg)
{
#ifdef MAILLIST_IS_TREE
	set(obj,MUIA_NListtree_Quiet,TRUE);
#else
	set(obj,MUIA_NList_Quiet,TRUE);
#endif
	return 0;
}

STATIC ULONG MailTreelist_Thaw(struct IClass *cl, Object *obj, Msg msg)
{
#ifdef MAILLIST_IS_TREE
	set(obj,MUIA_NListtree_Quiet,FALSE);
#else
	set(obj,MUIA_NList_Quiet,FALSE);
#endif
	return 0;
}

STATIC ULONG MailTreelist_RemoveSelected(struct IClass *cl, Object *obj, Msg msg)
{
#ifdef MAILLIST_IS_TREE
	struct MUI_NListtree_TreeNode *treenode;
	int j = 0, i = 0;
	struct MUI_NListtree_TreeNode **array;

	treenode = (struct MUI_NListtree_TreeNode *)MUIV_NListtree_PrevSelected_Start;

	for (;;)
	{
		DoMethod(obj, MUIM_NListtree_PrevSelected, &treenode);
		if (treenode==(struct MUI_NListtree_TreeNode *)MUIV_NListtree_PrevSelected_End)
			break;
		if (!treenode) break;
		i++;
	}

	if (!i) return 0; /* no emails selected */

	set(obj, MUIA_NListtree_Quiet, TRUE);

	if ((array = (struct MUI_NListtree_TreeNode **)AllocVec(sizeof(struct MUI_NListtree_TreeNode *)*i,0)))
	{
		treenode = (struct MUI_NListtree_TreeNode *)MUIV_NListtree_PrevSelected_Start;

		for (;;)
		{
			DoMethod(obj, MUIM_NListtree_PrevSelected, &treenode);
			if (treenode==(struct MUI_NListtree_TreeNode *)MUIV_NListtree_PrevSelected_End)
				break;
			array[j++] = treenode;
		}

		for (i=0;i<j;i++)
		{
			if ((ULONG)array[i]->tn_User == MUIV_MailTreelist_UserData_Name) continue;

			if (array[i]->tn_Flags & TNF_LIST)
			{
				struct MUI_NListtree_TreeNode *node = (struct MUI_NListtree_TreeNode *)
					DoMethod(obj, MUIM_NListtree_GetEntry, array[i], MUIV_NListtree_GetEntry_Position_Head, 0);

				while (node)
				{
					struct MUI_NListtree_TreeNode *nextnode = (struct MUI_NListtree_TreeNode *)
						DoMethod(obj, MUIM_NListtree_GetEntry, node, MUIV_NListtree_GetEntry_Position_Next, 0);

					DoMethod(obj, MUIM_NListtree_Move, array[i], node, MUIV_NListtree_Move_NewListNode_Root, MUIV_NListtree_Move_NewTreeNode_Tail);
					node = nextnode;
				}
			}

			DoMethod(obj, MUIM_NListtree_Remove, MUIV_NListtree_Remove_ListNode_Root,array[i],0);
		}		

		FreeVec(array);
	}
	set(obj, MUIA_NListtree_Quiet, FALSE);
#else
	DoMethod(obj,MUIM_NList_Remove,MUIV_NList_Remove_Selected);
#endif
	return 0;
}

ULONG MailTreelist_GetFirstSelected(struct IClass *cl, Object *obj, struct MUIP_MailTree_GetFirstSelected *msg)
{
	void *handle = msg->handle;

#ifdef MAILLIST_IS_TREE
	struct MUI_NListtree_TreeNode *treenode = (struct MUI_NListtree_TreeNode *)MUIV_NListtree_NextSelected_Start;
	DoMethod(obj, MUIM_NListtree_NextSelected, &treenode);
	if (treenode == (struct MUI_NListtree_TreeNode *)MUIV_NListtree_NextSelected_End) return NULL;
	*((struct MUI_NListtree_TreeNode **)handle) = treenode;
	if ((ULONG)treenode->tn_User == MUIV_MailTreelist_UserData_Name) return DoMethod(obj, MUIM_MailTree_GetNextSelected, msg->handle);
	if (treenode) return (ULONG)treenode->tn_User;
	return NULL;
#else
	LONG pos = MUIV_NList_NextSelected_Start;
	struct mail *m;
	DoMethod(obj, MUIM_NList_NextSelected, &pos);
	if (pos == MUIV_NList_NextSelected_End)
	{
		*((LONG*)handle) = 0;
		return NULL;
	}
	*((LONG*)handle) = pos;
	DoMethod(obj, MUIM_NList_GetEntry, pos, &m);
	return (ULONG)m;
#endif
}

ULONG MailTreelist_GetNextSelected(struct IClass *cl, Object *obj, struct MUIP_MailTree_GetNextSelected *msg)
{
	void *handle = msg->handle;

#ifdef MAILLIST_IS_TREE
	struct MUI_NListtree_TreeNode *treenode;
	do
	{
		treenode = *((struct MUI_NListtree_TreeNode **)handle);
		DoMethod(obj, MUIM_NListtree_NextSelected, &treenode);
		if (treenode == (struct MUI_NListtree_TreeNode *)MUIV_NListtree_NextSelected_End) return NULL;
		*((struct MUI_NListtree_TreeNode **)handle) = treenode;
	} while((ULONG)treenode->tn_User == MUIV_MailTreelist_UserData_Name);
	return (ULONG)treenode->tn_User;
#else
	LONG pos = *(LONG*)handle;
	struct mail *m;
	DoMethod(obj, MUIM_NList_NextSelected, &pos);
	if (pos == MUIV_NList_NextSelected_End)
	{
		*((LONG*)handle) = 0;
		return NULL;
	}
	*((LONG*)handle) = pos;
	DoMethod(obj, MUIM_NList_GetEntry, pos, &m);
	return (ULONG)m;
#endif
}

ULONG MailTreelist_RefreshMail(struct IClass *cl, Object *obj, struct MUIP_MailTree_RefreshMail *msg)
{
#ifdef MAILLIST_IS_TREE
	struct MUI_NListtree_TreeNode *treenode = FindListtreeUserData(obj, msg->m);
	if (treenode)
		DoMethod(obj, MUIM_NListtree_Redraw, treenode, 0);
#else
  DoMethod(obj, MUIM_NList_RedrawEntry, msg->m);
#endif
	return 0;
}

#ifdef MAILLIST_IS_TREE
static struct MUI_NListtree_TreeNode *main_find_insert_node(Object *obj, struct MUI_NListtree_TreeNode *tn, int *after)
{
	while (tn)
	{
		if (tn->tn_User != (APTR)MUIV_MailTreelist_UserData_Name) (*after)--;
		if (tn->tn_Flags & TNF_LIST)
		{
			struct MUI_NListtree_TreeNode *first;
			struct MUI_NListtree_TreeNode *found;

			first = (struct MUI_NListtree_TreeNode*)DoMethod(obj,MUIM_NListtree_GetEntry,tn,MUIV_NListtree_GetEntry_Position_Head,0);
			found =  main_find_insert_node(obj, first,after);
			if (found) return found;
		}

		if (*after < 0) return tn;

		tn = (struct MUI_NListtree_TreeNode*)DoMethod(obj,MUIM_NListtree_GetEntry,tn,MUIV_NListtree_GetEntry_Position_Next,0);
	}
	return tn;
}
#endif

ULONG MailTreelist_InsertMail(struct IClass *cl, Object *obj, struct MUIP_MailTree_InsertMail *msg)
{
	int after = msg->after;
	struct mail *mail = msg->m;

#ifdef MAILLIST_IS_TREE
	if (after == -2)
	{
		DoMethod(obj,MUIM_NListtree_Insert,"" /*name*/, mail, /*udata */
					 MUIV_NListtree_Insert_ListNode_Root,MUIV_NListtree_Insert_PrevNode_Tail,0/*flags*/);
	} else
	{
		struct MUI_NListtree_TreeNode *tn;
		struct MUI_NListtree_TreeNode *list;

		tn = (struct MUI_NListtree_TreeNode*)DoMethod(obj,MUIM_NListtree_GetEntry,MUIV_NListtree_GetEntry_ListNode_Root,MUIV_NListtree_GetEntry_Position_Head,0);
		tn = main_find_insert_node(obj,tn,&after);
		if (tn)
		{
			list = (struct MUI_NListtree_TreeNode*)DoMethod(obj,MUIM_NListtree_GetEntry,tn, MUIV_NListtree_GetEntry_Position_Parent,0);
		} else list = (struct MUI_NListtree_TreeNode*)MUIV_NListtree_Insert_ListNode_Root;


		/* Indeed this is a lot of faster with current NListtree */
		set(obj,MUIA_NListtree_Quiet, TRUE);

		DoMethod(obj,MUIM_NListtree_Insert,"" /*name*/, mail, /*udata */
					 list,tn?tn:MUIV_NListtree_Insert_PrevNode_Head,0/*flags*/);

		set(obj,MUIA_NListtree_Quiet, FALSE);
	}
#else
	if (after == -2)
	{
		DoMethod(obj, MUIM_NList_InsertSingle, mail, MUIV_NList_Insert_Bottom);
	} else
	{
		DoMethod(obj, MUIM_NList_InsertSingle, mail, after + 1);
	}
#endif
	return 0;
}

STATIC ULONG MailTreelist_RemoveMail(struct IClass *cl, Object *obj, struct MUIP_MailTree_RemoveMail *msg)
{
#ifdef MAILLIST_IS_TREE
	struct MUI_NListtree_TreeNode *treenode = FindListtreeUserData(obj, msg->m);
	if (treenode)
	{
		DoMethod(obj, MUIM_NListtree_Remove, MUIV_NListtree_Remove_ListNode_Root, treenode,0);
	}
#else
	int i;
	for (i=0;i<xget(obj,MUIA_NList_Entries);i++)
	{
		struct mail *m2;
		DoMethod(obj,MUIM_NList_GetEntry,i,&m2);
		if (m2 == msg->m)
		{
			DoMethod(obj,MUIM_NList_Remove,i);
			break;
		}
	}
#endif
	return 0;
}

STATIC ULONG MailTreelist_ReplaceMail(struct IClass *cl, Object *obj, struct MUIP_MailTree_ReplaceMail *msg)
{
#ifdef MAILLIST_IS_TREE
	struct MUI_NListtree_TreeNode *treenode = FindListtreeUserData(obj, msg->oldmail);
	if (treenode)
	{
/*		DoMethod(mail_tree, MUIM_NListtree_Rename, treenode, newmail, MUIV_NListtree_Rename_Flag_User);*/
		set(obj, MUIA_NListtree_Quiet, TRUE);
		DoMethod(obj, MUIM_NListtree_Remove, NULL, treenode,0);
		DoMethod(obj, MUIM_MailTree_InsertMail, msg->newmail, -2);
		set(obj, MUIA_NListtree_Active, FindListtreeUserData(obj, msg->newmail));
		set(obj, MUIA_NListtree_Quiet, FALSE);
	}
#else

	int i;
	for (i=0;i<xget(obj,MUIA_NList_Entries);i++)
	{
		struct mail *m2;
		DoMethod(obj,MUIM_NList_GetEntry,i,&m2);
		if (m2 == msg->oldmail)
		{
			DoMethod(obj, MUIM_NList_ReplaceSingle, msg->newmail, i, NOWRAP, 0);
			break;
		}
	}

#endif
	return 0;
}

STATIC ULONG MailTreelist_RefreshSelected(struct IClass *cl, Object *obj, Msg msg)
{
#ifdef MAILLIST_IS_TREE
#else
	LONG pos = MUIV_NList_NextSelected_Start;
	while (1)
	{
		DoMethod(obj, MUIM_NList_NextSelected, &pos);
		if (pos == MUIV_NList_NextSelected_End) break;
		DoMethod(obj, MUIM_NList_Redraw, pos);
	}
#endif
	return 0;
}

STATIC BOOPSI_DISPATCHER(ULONG, MailTreelist_Dispatcher, cl, obj, msg)
{
	switch(msg->MethodID)
	{
		case	OM_NEW:				return MailTreelist_New(cl,obj,(struct opSet*)msg);
		case	OM_DISPOSE:		return MailTreelist_Dispose(cl,obj,msg);
		case	OM_SET:				return MailTreelist_Set(cl,obj,(struct opSet*)msg);
		case	OM_GET:				return MailTreelist_Get(cl,obj,(struct opGet*)msg);
		case	MUIM_Setup:		return MailTreelist_Setup(cl,obj,(struct MUIP_Setup*)msg);
		case	MUIM_Cleanup:	return MailTreelist_Cleanup(cl,obj,msg);
		case  MUIM_DragQuery: return MailTreelist_DragQuery(cl,obj,(struct MUIP_DragDrop *)msg);
		case	MUIM_Export:		return MailTreelist_Export(cl,obj,(struct MUIP_Export *)msg);
		case	MUIM_Import:		return MailTreelist_Import(cl,obj,(struct MUIP_Import *)msg);

#ifdef MAILLIST_IS_TREE
		case	MUIM_NListtree_MultiTest: return MailTreelist_MultiTest(cl,obj,(struct MUIP_NListtree_MultiTest*)msg);
#endif

		case	MUIM_ContextMenuChoice: return MailTreelist_ContextMenuChoice(cl, obj, (struct MUIP_ContextMenuChoice *)msg);
		case  MUIM_NList_ContextMenuBuild: return MailTreelist_NList_ContextMenuBuild(cl,obj,(struct MUIP_NList_ContextMenuBuild *)msg);

		case	MUIM_MailTree_Clear: return MailTreelist_Clear(cl, obj, (APTR)msg);
		case	MUIM_MailTree_SetFolderMails: return MailTreelist_SetFolderMails(cl, obj, (APTR)msg);
		case	MUIM_MailTree_Freeze: return MailTreelist_Freeze(cl, obj, (APTR)msg);
		case	MUIM_MailTree_Thaw: return MailTreelist_Thaw(cl, obj, (APTR)msg);
		case	MUIM_MailTree_RemoveSelected: return MailTreelist_RemoveSelected(cl, obj, (APTR)msg);
		case	MUIM_MailTree_GetFirstSelected: return MailTreelist_GetFirstSelected(cl, obj, (APTR)msg);
		case	MUIM_MailTree_GetNextSelected: return MailTreelist_GetNextSelected(cl, obj, (APTR)msg);
		case	MUIM_MailTree_RefreshMail: return MailTreelist_RefreshMail(cl,obj,(APTR)msg);
		case	MUIM_MailTree_InsertMail: return MailTreelist_InsertMail(cl,obj,(APTR)msg);
		case	MUIM_MailTree_RemoveMail: return MailTreelist_RemoveMail(cl,obj,(APTR)msg);
		case	MUIM_MailTree_ReplaceMail: return MailTreelist_ReplaceMail(cl,obj,(APTR)msg);
		case	MUIM_MailTree_RefreshSelected: return MailTreelist_RefreshSelected(cl,obj,(APTR)msg);

		default: return DoSuperMethodA(cl,obj,msg);
	}
}

struct MUI_CustomClass *CL_MailTreelist;

#ifdef MAILLIST_IS_TREE
#define MAILLIST_PARENTCLASS "NListtree.mcc"
#else
#define MAILLIST_PARENTCLASS "NList.mcc"
#endif

int create_mailtreelist_class(void)
{
	SM_ENTER;
	if ((CL_MailTreelist = CreateMCC(MAILLIST_PARENTCLASS ,NULL,sizeof(struct MailTreelist_Data),MailTreelist_Dispatcher)))
	{
		SM_DEBUGF(15,("Create CL_MailTreelist: 0x%lx\n",CL_MailTreelist));
		SM_RETURN(1,"%ld");
	}
	SM_DEBUGF(5,("FAILED! Create CL_MailTreelist\n"));
	SM_RETURN(0,"%ld");
}

void delete_mailtreelist_class(void)
{
	SM_ENTER;
	if (CL_MailTreelist)
	{
		if (MUI_DeleteCustomClass(CL_MailTreelist))
		{
			SM_DEBUGF(15,("Deleted CL_MailTreelist: 0x%lx\n",CL_MailTreelist));
			CL_MailTreelist = NULL;
		} else
		{
			SM_DEBUGF(5,("FAILED! Delete CL_MailTreelist: 0x%lx\n",CL_MailTreelist));
		}
	}
	SM_LEAVE;
}
