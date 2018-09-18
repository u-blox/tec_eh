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

#ifndef _ACT_ENERGY_SOURCE_H_
#define _ACT_ENERGY_SOURCE_H_

/**************************************************************************
 * MANIFEST CONSTANTS
 *************************************************************************/

/**************************************************************************
 * FUNCTIONS
 *************************************************************************/

/** Enable a given energy source.
 *
 * @param source the number of the source to enable.
 */
void enableEnergySource(int source);

/** Disable a given energy source.
 *
 * @param source the number of the source to disable.
 */
void disableEnergySource(int source);

/** Get the active energy sources.
 *
 * @return a bit map of the enabled energy sources.
 */
unsigned char getEnergySources();

#endif // _ACT_ENERGY_SOURCE_H_

// End Of File
