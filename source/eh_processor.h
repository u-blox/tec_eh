/*
 * Copyright (C) u-blox Melbourn Ltd
 * u-blox Melbourn Ltd, Melbourn, UK
 *
 * All rights reserved.
 *
 * This source file is the sole property of u-blox Melbourn Ltd.
 * Reproduction or utilisation of this source in whole or part is
 * forbidden without the written consent of u-blox Melbourn Ltd.
 */

#ifndef _EH_PROCESSOR_H_
#define _EH_PROCESSOR_H_

#include <eh_action.h>

/**************************************************************************
 * MANIFEST CONSTANTS
 *************************************************************************/

/**************************************************************************
 * FUNCTIONS
 *************************************************************************/

/** Handle wakeup of the system and perform all necessary actions, returning
 * when it is time to go back to sleep again.
 */
void handleWakeup();

#endif // _EH_PROCESSOR_H_

// End Of File
