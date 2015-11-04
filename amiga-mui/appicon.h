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
 * @file appicon.h
 */

#ifndef SM__APPICON_H
#define SM__APPICON_H

#define SM_APPICON_CHECK 0
#define SM_APPICON_EMPTY 1
#define SM_APPICON_NEW   2
#define SM_APPICON_OLD   3
#define SM_APPICON_MAX   4

/**
 * Initialize the appicon port and objects.
 *
 * @return 0 on failure, 1 on success
 */
int appicon_init(void);

/**
 * Free the appicon port and objects.
 */
void appicon_free(void);

/**
 * Returns the signal mask of the appicon port
 *
 * @return the signal mask
 */
ULONG appicon_mask(void);

/**
 * Handle all pending appicon events.
 */
void appicon_handle(void);

/**
 * Refreshes the appicon
 *
 * @param force if the appicon should be forced to be refreshed.
 */
void appicon_refresh(int force);

/**
 * Snapshot the AppIcon
 */
void appicon_snapshot(void);

/**
 * Unsnapshot the AppIcon
 */
void appicon_unsnapshot(void);

/**
 * Returns the app icons object pointer
 *
 * @return the disk object or NULL.
 */
struct DiskObject *appicon_get_hide_icon(void);

#endif /* SM__APPICON_H */
