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
 * @file arexx.h
 */

#ifndef SM__AREXX_H
#define SM__AREXX_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

/**
 * Returns the ARexx message port if it already exists. Should
 * be called in Forbid() state.
 *
 * @return the ARexx port.
 */
struct MsgPort *arexx_find(void);

/**
 * Initialize the ARexx port, fails if the port already exists.
 *
 * @return 0 on failure, 1 on success.
 */
int arexx_init(void);

/**
 * Cleanup ARexx stuff
 */
void arexx_cleanup(void);

/**
 * Executes the given ARexx command
 *
 * @param command the command to be executed.
 * @return 0 on failure, 1 on success
 */
int arexx_execute_script(char *command);

/**
 * Returns the mask of the ARexx port
 *
 * @return the mask of the ARexx port
 */
ULONG arexx_mask(void);

/**
 * Handle the incoming ARexx messages.
 *
 * @return always 0.
 */
int arexx_handle(void);

#endif
