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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <datatypes/datatypesclass.h>
#include <intuition/icclass.h>
#include <libraries/mui.h>

#include <clib/alib_protos.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>
#include <proto/datatypes.h>

#include "support.h"

#include "amigasupport.h"
#include "compiler.h"
#include "datatypesclass.h"
#include "muistuff.h"

struct DataTypes_Data
{
	Object *dt_obj;
	char *filename; /* Cache the filename */
	int show; /* 1 if between show / hide */

	struct MUI_EventHandlerNode ehnode;
};

STATIC ULONG DataTypes_New(struct IClass *cl,Object *obj,struct opSet *msg)
{
	struct DataTypes_Data *data;

	if (!(obj=(Object *)DoSuperNew(cl,obj,
/*					MUIA_FillArea, TRUE,*/
					TAG_MORE,msg->ops_AttrList)))
		return 0;

	data = (struct DataTypes_Data*)INST_DATA(cl,obj);
	data->dt_obj = NULL;
	data->filename = NULL;

	data->ehnode.ehn_Priority = 1;
	data->ehnode.ehn_Flags    = 0;
	data->ehnode.ehn_Object   = obj;
	data->ehnode.ehn_Class    = NULL;
	data->ehnode.ehn_Events   = IDCMP_IDCMPUPDATE;

	return (ULONG)obj;
}

STATIC VOID DataTypes_Dispose(struct IClass *cl, Object *obj, Msg msg)
{
	struct DataTypes_Data *data = (struct DataTypes_Data*)INST_DATA(cl,obj);
	if (data->dt_obj) DisposeDTObject(data->dt_obj);
	if (data->filename) FreeVec(data->filename);

	DoSuperMethodA(cl,obj,msg);
}

STATIC ULONG DataTypes_Set(struct IClass *cl,Object *obj,struct opSet *msg)
{
	struct DataTypes_Data *data = (struct DataTypes_Data*)INST_DATA(cl,obj);
	struct TagItem *tstate, *tag;

	char *newfilename = NULL;
	void *newbuffer = NULL;
	ULONG newbufferlen = 0;

	tstate = (struct TagItem *)msg->ops_AttrList;

	while (tag = NextTagItem (&tstate))
	{
		ULONG tidata = tag->ti_Data;

		switch (tag->ti_Tag)
		{
			case	MUIA_DataTypes_FileName:
						if (mystricmp(data->filename, (char*)tidata))
							newfilename = (char*)tidata;
						break;

			case	MUIA_DataTypes_Buffer:
						newbuffer = (void*)tidata;
						break;

			case	MUIA_DataTypes_BufferLen:
						newbufferlen = tidata;
						break;
		}
	}

	if (newfilename || newbuffer)
	{
		if (data->filename) FreeVec(data->filename);
		data->filename = StrCopy(newfilename);

		if (data->dt_obj)
		{
			/* Remove the datatype object if it is shown */
			if (data->show) RemoveDTObject(_window(obj),data->dt_obj);

			/* Dispose the datatype object */
			if (data->dt_obj)
			{
				DisposeDTObject(data->dt_obj);
				data->dt_obj = NULL;
			}
		}

		/* DTST_MEMORY is new for version 44 of the datatypes.library */
		data->dt_obj = NewDTObject(newfilename,
				DTA_SourceType, newfilename?DTST_FILE:DTST_MEMORY,
				newbuffer?DTA_SourceAddress:TAG_IGNORE, newbuffer,
				newbuffer?DTA_SourceSize:TAG_IGNORE, newbufferlen,
				TAG_DONE);

		if (data->dt_obj)
		{
			/* If is between MUIM_Show and MUIM_Hide add the datatype to the window */
			if (data->show)
			{
				SetDTAttrs(data->dt_obj, NULL, NULL,
					GA_Left,		_mleft(obj),
					GA_Top,		_mtop(obj),
					GA_Width,	_mwidth(obj),
					GA_Height,	_mheight(obj),
					ICA_TARGET,	ICTARGET_IDCMP,
					TAG_DONE);

				AddDTObject(_window(obj), NULL, data->dt_obj, -1);
			}
			MUI_Redraw(obj, MADF_DRAWOBJECT);
		}
	}
	return DoSuperMethodA(cl,obj,(Msg)msg);
}

STATIC ULONG DataTypes_AskMinMax(struct IClass *cl,Object *obj, struct MUIP_AskMinMax *msg)
{
  DoSuperMethodA(cl, obj, (Msg) msg);

  msg->MinMaxInfo->MinWidth += 20;
  msg->MinMaxInfo->DefWidth += 20;
  msg->MinMaxInfo->MaxWidth = MUI_MAXMAX;

  msg->MinMaxInfo->MinHeight += 40;
  msg->MinMaxInfo->DefHeight += 40;
  msg->MinMaxInfo->MaxHeight = MUI_MAXMAX;
  return 0;
}

STATIC ULONG DataTypes_Setup(struct IClass *cl, Object *obj, struct MUIP_Setup *msg)
{
	struct DataTypes_Data *data = (struct DataTypes_Data*)INST_DATA(cl,obj);
	DoMethod(_win(obj), MUIM_Window_AddEventHandler, &data->ehnode);
	return DoSuperMethodA(cl,obj,(Msg)msg);
}

STATIC ULONG DataTypes_Cleanup(struct IClass *cl, Object *obj, Msg msg)
{
	struct DataTypes_Data *data = (struct DataTypes_Data*)INST_DATA(cl,obj);
	DoMethod(_win(obj), MUIM_Window_RemEventHandler, &data->ehnode);
	return 0;
}

STATIC ULONG DataTypes_Show(struct IClass *cl, Object *obj, Msg msg)
{
	struct DataTypes_Data *data = (struct DataTypes_Data*)INST_DATA(cl,obj);

	DoSuperMethodA(cl,obj,msg);

	data->show = 1;

	if (data->dt_obj)
	{
		SetDTAttrs(data->dt_obj, NULL, NULL,
				GA_Left,		_mleft(obj),
				GA_Top,		_mtop(obj),
				GA_Width,	_mwidth(obj),
				GA_Height,	_mheight(obj),
				ICA_TARGET,	ICTARGET_IDCMP,
				TAG_DONE);

		AddDTObject(_window(obj), NULL, data->dt_obj, -1);
	}

	return 1;
}

STATIC VOID DataTypes_Hide(struct IClass *cl, Object *obj, Msg msg)
{
	struct DataTypes_Data *data = (struct DataTypes_Data*)INST_DATA(cl,obj);

	data->show = 0;

	if (data->dt_obj)
	{
		RemoveDTObject(_window(obj),data->dt_obj);
	}

	DoSuperMethodA(cl,obj,msg);
}

STATIC ULONG DataTypes_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg)
{
	struct DataTypes_Data *data = (struct DataTypes_Data*)INST_DATA(cl,obj);
	DoSuperMethodA(cl,obj,(Msg)msg);

	if (msg->flags & MADF_DRAWOBJECT && data->dt_obj)
	{
		RefreshDTObjects(data->dt_obj, _window(obj), NULL, NULL);
	}

	return 1;
}

STATIC ULONG DataTypes_HandleEvent(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *msg)
{
	struct DataTypes_Data *data = (struct DataTypes_Data*)INST_DATA(cl,obj);

	if (msg->imsg && msg->imsg->Class == IDCMP_IDCMPUPDATE)
	{
		struct TagItem *tstate, *tag;

		tstate = (struct TagItem *)msg->imsg->IAddress;

		while (tag = NextTagItem (&tstate))
		{
			ULONG tidata = tag->ti_Data;

			switch (tag->ti_Tag)
			{
				case	DTA_Busy:
/*							if (tidata) set(_app(obj), MUIA_Application_Sleep, TRUE);
							else set(_app(obj), MUIA_Application_Sleep, FALSE);*/
							break;

				case	DTA_Sync:
							if (data->show && data->dt_obj)
							{
							  RefreshDTObjects (data->dt_obj, _window(obj), NULL, NULL);
							}
						  break;
			}
		}
	}
	return 0;
}

STATIC SAVEDS ASM ULONG DataTypes_Dispatcher(register __a0 struct IClass *cl, register __a2 Object *obj, register __a1 Msg msg)
{
	switch(msg->MethodID)
	{
		case	OM_NEW:				return DataTypes_New(cl,obj,(struct opSet*)msg);
		case  OM_DISPOSE:		DataTypes_Dispose(cl,obj,msg); return 0;
		case  OM_SET:				return DataTypes_Set(cl,obj,(struct opSet*)msg);
		case  MUIM_AskMinMax: return DataTypes_AskMinMax(cl,obj,(struct MUIP_AskMinMax *)msg);
		case	MUIM_Setup:		return DataTypes_Setup(cl,obj,(struct MUIP_Setup*)msg);
		case	MUIM_Cleanup:	return DataTypes_Cleanup(cl,obj,msg);
		case	MUIM_Show:			return DataTypes_Show(cl,obj,msg);
		case	MUIM_Hide:			DataTypes_Hide(cl,obj,msg);return 0;
		case	MUIM_Draw:			return DataTypes_Draw(cl,obj,(struct MUIP_Draw*)msg);
		case	MUIM_HandleEvent: return DataTypes_HandleEvent(cl,obj,(struct MUIP_HandleEvent*)msg);
		default: return DoSuperMethodA(cl,obj,msg);
	}
}

struct MUI_CustomClass *CL_DataTypes;

int create_datatypes_class(void)
{
	if ((CL_DataTypes = MUI_CreateCustomClass(NULL,MUIC_Area,NULL,sizeof(struct DataTypes_Data),DataTypes_Dispatcher)))
		return 1;
	return 0;
}

void delete_datatypes_class(void)
{
	if (CL_DataTypes) MUI_DeleteCustomClass(CL_DataTypes);
}
