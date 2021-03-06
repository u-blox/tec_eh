/* mbed Microcontroller Library
 * Copyright (c) 2006-2018 u-blox Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <mbed.h>
#include <stdlib.h> // for abs()
#include <eh_utilities.h>
#include <eh_debug.h>
#include <eh_data.h>
#include <eh_action.h>

/**************************************************************************
 * MANIFEST CONSTANTS
 *************************************************************************/

/** Macro to check an Action **
 *
 */
#define CHECK_ACTION_PP(ppaction)  \
    MBED_ASSERT ((*(ppaction) < (Action *) (gActionList  + ARRAY_SIZE(gActionList))) || \
                 (*(ppaction) == NULL))

/**************************************************************************
 * LOCAL VARIABLES
 *************************************************************************/

/** Flag so that we know if we've been initialised.
 */
static bool gInitialised = false;

/** The action list.
 */
static Action gActionList[MAX_NUM_ACTIONS];

/** The array used while ranking actions; must be the same size as gActionList.
 */
static Action *gpRankedList[MAX_NUM_ACTIONS];

/** The outcome of ranking the array: a prioritised list of action types.
 */
static ActionType gRankedTypes[MAX_NUM_ACTION_TYPES];

/** Mutex to protect these lists.
 */
static Mutex gMtx;

/** A pointer to the next action type to perform.
 */
static ActionType *gpNextActionType = NULL;

/** Desirability table for actions.
 * Note: index into this using ActionType.
 */
static Desirability gDesirability[MAX_NUM_ACTION_TYPES];

/** The variability damper table for actions.
 * Note: index into this using ActionType.
 */
static VariabilityDamper gVariabilityDamper[MAX_NUM_ACTION_TYPES];

/** A pointer to the last data entry for each action type, used when evaluating how variable it is.
 */
static Data *gpLastDataValue[MAX_NUM_ACTION_TYPES] = {NULL};

/** The peak variability table for actions, used as temporary storage when ranking.
 * Note: index into this using ActionType.
 */
static unsigned int gPeakVariability[MAX_NUM_ACTION_TYPES];

/** The number of occurrences of each action type, used as temporary storage when ranking.
 * Note: index into this using ActionType.
 */
static unsigned int gOccurrence[MAX_NUM_ACTION_TYPES];

#ifdef MBED_CONF_APP_ENABLE_PRINTF
/** The action states as strings for debug purposes.
 */
static const char *gActionStateString[] = {"ACTION_STATE_NULL",
                                           "ACTION_STATE_REQUESTED",
                                           "ACTION_STATE_IN_PROGRESS",
                                           "ACTION_STATE_COMPLETED",
                                           "ACTION_STATE_TRIED_AND_FAILED"
                                           "ACTION_STATE_ABORTED"};

/** The action types as strings for debug purposes.
 */
static const char *gActionTypeString[] = {"ACTION_TYPE_NULL",
                                          "ACTION_TYPE_REPORT",
                                          "ACTION_TYPE_GET_TIME_AND_REPORT",
                                          "ACTION_TYPE_MEASURE_HUMIDITY",
                                          "ACTION_TYPE_MEASURE_ATMOSPHERIC_PRESSURE",
                                          "ACTION_TYPE_MEASURE_TEMPERATURE",
                                          "ACTION_TYPE_MEASURE_LIGHT",
                                          "ACTION_TYPE_MEASURE_ACCELERATION",
                                          "ACTION_TYPE_MEASURE_POSITION",
                                          "ACTION_TYPE_MEASURE_MAGNETIC",
                                          "ACTION_TYPE_MEASURE_BLE"};
#endif

/**************************************************************************
 * STATIC FUNCTIONS
 *************************************************************************/

// Empty the action list.
// Note: doesn't lock the list.
static void clearActionList(bool freeData)
{
    for (unsigned int x = 0; x < ARRAY_SIZE(gActionList); x++) {
        gActionList[x].state = ACTION_STATE_NULL;
        if (freeData && (gActionList[x].pData != NULL)) {
            free(gActionList[x].pData);
        }
        gActionList[x].pData = NULL;
    }
}

// Empty the ranked action lists.
// Note: doesn't lock the list.
static void clearRankedLists()
{
    for (unsigned int x = 0; x < ARRAY_SIZE(gpRankedList); x++) {
        gpRankedList[x] = NULL;
    }
    for (unsigned int x = 0; x < ARRAY_SIZE(gRankedTypes); x++) {
        gRankedTypes[x] = ACTION_TYPE_NULL;
    }
    for (unsigned int x = 0; x < ARRAY_SIZE(gPeakVariability); x++) {
        gPeakVariability[x] = 0;
    }
    for (unsigned int x = 0; x < ARRAY_SIZE(gpLastDataValue); x++) {
        gpLastDataValue[x] = NULL;
    }
    for (unsigned int x = 0; x < ARRAY_SIZE(gOccurrence); x++) {
        gOccurrence[x] = 0;
    }

    gpNextActionType = NULL;
}

// Print an action.
static void printAction(const Action *pAction)
{
    PRINTF("- %s, %s completed @%d seconds, cost %llu nWh, %s.\n", gActionTypeString[pAction->type],
           gActionStateString[pAction->state], (int) pAction->timeCompletedUTC,
           pAction->energyCostNWH, pAction->pData != NULL ? "has data" : "has no data");
}

// Overwrite an action with new contents.
static void writeAction(Action *pAction, ActionType type)
{
    pAction->type = type;
    pAction->state = ACTION_STATE_REQUESTED;
    pAction->timeCompletedUTC = 0;
    pAction->energyCostNWH = 0;
    // Unhook any data items that might have been
    // attached to a completed action.  Don't
    // try to free them though, they have a life
    // of their own
    dataLockList();
    if (pAction->pData != NULL) {
        ((Data *) pAction->pData)->pAction = NULL;
        pAction->pData = NULL;
    }
    dataUnlockList();
}

// The condition function.
static bool condition(Action *pAction, Action *pNextAction)
{
    bool answer = false;

    // First check rarity: is pNextAction more rare than pAction?
    if (gOccurrence[pNextAction->type] < gOccurrence[pAction->type]) {
        answer = true;
        // If rarity is the same, check energy cost (least first)
    } else if (gOccurrence[pNextAction->type] == gOccurrence[pAction->type]) {
        if (pNextAction->energyCostNWH < pAction->energyCostNWH) {
            answer = true;
            // If the energy cost is the same, check desirability (most first)
        } else if (pNextAction->energyCostNWH == pAction->energyCostNWH) {
            if (gDesirability[pNextAction->type] > gDesirability[pAction->type]) {
                answer = true;
                // If desirability is the same, check variability (most first)
            } else if (gDesirability[pNextAction->type] == gDesirability[pAction->type]) {
                if (gPeakVariability[pNextAction->type] > gPeakVariability[pAction->type]) {
                    answer = true;
                    // If variability is the same, check time (oldest first)
                } else if(gPeakVariability[pNextAction->type] == gPeakVariability[pAction->type]) {
                    if (pNextAction->timeCompletedUTC < pAction->timeCompletedUTC) {
                        answer = true;
                    }
                }
            }
        }
    }

    return answer;
}

// Rank the gpRankedList using the given condition function.
// NOTE: this does not lock the list.
static void ranker(bool condition(Action *, Action *)) {
    Action **ppRanked;
    Action *pRankedTmp;

    ppRanked = &(gpRankedList[0]);
    while (ppRanked < (Action **) (gpRankedList  + ARRAY_SIZE(gpRankedList)) - 1) {
        CHECK_ACTION_PP(ppRanked);
        CHECK_ACTION_PP(ppRanked + 1);
        // If condition is true, swap them and restart the sort
        if (condition(*ppRanked, *(ppRanked + 1))) {
            pRankedTmp = *ppRanked;
            *ppRanked = *(ppRanked + 1);
            *(ppRanked + 1) = pRankedTmp;
            ppRanked = &(gpRankedList[0]);
        } else {
            ppRanked++;
        }
    }
}

/**************************************************************************
 * PUBLIC FUNCTIONS
 *************************************************************************/

// Initialise the action lists.
void actionInit()
{
    MTX_LOCK(gMtx);

    // Clear the lists (but only free data if we've been initialised before)
    clearActionList(gInitialised);
    clearRankedLists();

    for (unsigned int x = ACTION_TYPE_NULL + 1; x < ARRAY_SIZE(gDesirability); x++) {
        gDesirability[x] = DESIRABILITY_DEFAULT;
    }

    for (unsigned int x = ACTION_TYPE_NULL + 1; x < ARRAY_SIZE(gVariabilityDamper); x++) {
        gVariabilityDamper[x] = VARIABILITY_DAMPER_DEFAULT;
    }

    gInitialised = true;

    MTX_UNLOCK(gMtx);
}

// Set the desirability of an action type.
bool actionSetDesirability(ActionType type, Desirability desirability)
{
    bool success = false;

    if (type < ARRAY_SIZE(gDesirability)) {
        gDesirability[type] = desirability;
        success = true;
    }

    return success;
}

// Get the desirability of an action type.
Desirability actionGetDesirability(ActionType type)
{
    MBED_ASSERT(type < ARRAY_SIZE(gDesirability));
    return gDesirability[type];
}

// Set the variability damper of an action type.
bool actionSetVariabilityDamper(ActionType type, VariabilityDamper variabilityDamper)
{
    bool success = false;

    if (type < ARRAY_SIZE(gVariabilityDamper)) {
        gVariabilityDamper[type] = variabilityDamper;
        success = true;
    }

    return success;
}

// Mark an action as completed.
void actionCompleted(Action *pAction)
{
    MTX_LOCK(gMtx);

    if (pAction != NULL) {
        CHECK_ACTION_PP(&pAction);
        pAction->state = ACTION_STATE_COMPLETED;
    }

    MTX_UNLOCK(gMtx);
}

// Determine if an action has run.
bool hasActionRun(Action *pAction)
{
    bool hasRun = false;

    MTX_LOCK(gMtx);

    if (pAction != NULL) {
        CHECK_ACTION_PP(&pAction);
        if ((pAction->state == ACTION_STATE_COMPLETED) ||
            (pAction->state == ACTION_STATE_TRIED_AND_FAILED)) {
            hasRun = true;
        }
    }

    MTX_UNLOCK(gMtx);

    return hasRun;
}

// Mark an action as "tried and failed".
void actionTriedAndFailed(Action *pAction)
{
    MTX_LOCK(gMtx);

    if (pAction != NULL) {
        CHECK_ACTION_PP(&pAction);
        pAction->state = ACTION_STATE_TRIED_AND_FAILED;
    }

    MTX_UNLOCK(gMtx);
}

// Mark an action as aborted.
void actionAborted(Action *pAction)
{
    MTX_LOCK(gMtx);

    if (pAction != NULL) {
        CHECK_ACTION_PP(&pAction);
        pAction->state = ACTION_STATE_ABORTED;
    }

    MTX_UNLOCK(gMtx);
}

// Remove an action from the list.
void actionRemove(Action *pAction)
{
    MTX_LOCK(gMtx);

    if (pAction != NULL) {
        CHECK_ACTION_PP(&pAction);
        pAction->state = ACTION_STATE_NULL;
    }

    MTX_UNLOCK(gMtx);
}

// Return the number of actions not yet finished (i.e. requested or
// in progress).
int actionCount()
{
    int numActions = 0;

    MTX_LOCK(gMtx);

    for (unsigned int x = 0; x < ARRAY_SIZE(gActionList); x++) {
        if ((gActionList[x].state == ACTION_STATE_REQUESTED) ||
            (gActionList[x].state == ACTION_STATE_IN_PROGRESS)) {
            numActions++;
        }
    }

    MTX_UNLOCK(gMtx);

    return numActions;
}

// Add a new action to the list.
Action *pActionAdd(ActionType type)
{
    Action *pAction;

    MTX_LOCK(gMtx);

    pAction = NULL;
    // See if there are any NULL, ABORTED or TIMED-OUT entries
    // that can be re-used
    for (unsigned int x = 0; (x < ARRAY_SIZE(gActionList)) && (pAction == NULL); x++) {
        if ((gActionList[x].state == ACTION_STATE_NULL) ||
            (gActionList[x].state == ACTION_STATE_ABORTED) ||
            (gActionList[x].state == ACTION_STATE_TRIED_AND_FAILED)) {
            pAction = &(gActionList[x]);
        }
    }
    // If not, try to re-use a COMPLETED entry
    for (unsigned int x = 0; (x < ARRAY_SIZE(gActionList)) && (pAction == NULL); x++) {
        if (gActionList[x].state == ACTION_STATE_COMPLETED) {
            pAction = &(gActionList[x]);
        }
    }

    if (pAction != NULL) {
        writeAction(pAction, type);
    }

    MTX_UNLOCK(gMtx);

    return pAction;
}

// Return the average energy required to complete (or fail
// to successfully perform) an action.
unsigned long long int actionEnergyNWH(ActionType type)
{
    unsigned long long int energyNWH = 0;
    unsigned int numActions = 0;

    MTX_LOCK(gMtx);

    for (unsigned int x = 0; x < ARRAY_SIZE(gActionList); x++) {
        if ((gActionList[x].type == type) &&
            ((gActionList[x].state == ACTION_STATE_COMPLETED) ||
             (gActionList[x].state == ACTION_STATE_TRIED_AND_FAILED))) {
            energyNWH += gActionList[x].energyCostNWH;
            numActions++;
        }
    }

    if (numActions > 0) {
        energyNWH /= numActions;
    }

    MTX_UNLOCK(gMtx);

    return energyNWH;
}

// Get the next action type to perform and advance the action type pointer.
ActionType actionRankNextType()
{
    ActionType actionType;

    MTX_LOCK(gMtx);

    actionType = ACTION_TYPE_NULL;
    if (gpNextActionType != NULL) {
        actionType = *gpNextActionType;
        if (gpNextActionType < (ActionType *) (gRankedTypes  + ARRAY_SIZE(gRankedTypes))) {
            gpNextActionType++;
        } else {
            gpNextActionType = NULL;
        }
    }

    MTX_UNLOCK(gMtx);

    return actionType;
}

// Create the ranked the action type list.
ActionType actionRankTypes()
{
    Action **ppRanked;
    unsigned int z;
    bool found;
    Desirability d;
    ActionType a;
    Desirability desirability[MAX_NUM_ACTION_TYPES];
    ActionType actionTypeSortedByDesirability[MAX_NUM_ACTION_TYPES];

    MTX_LOCK(gMtx);

    // Clear the lists
    clearRankedLists();

    ppRanked = &(gpRankedList[0]);
    // Populate the list with the actions that have been used
    // working out the peak variability and number of occurrences
    // of each one along the way
    for (unsigned int x = 0; x < ARRAY_SIZE(gActionList); x++) {
        if ((gActionList[x].state != ACTION_STATE_NULL) &&
            (gActionList[x].state != ACTION_STATE_ABORTED) &&
            (gActionList[x].state != ACTION_STATE_TRIED_AND_FAILED)) {
            MBED_ASSERT(gActionList[x].type != ACTION_TYPE_NULL);
            (gOccurrence[gActionList[x].type])++;
            if (gActionList[x].pData != NULL) {
                // If the action has previous data, work out how much it
                // differs from this previous data and divide by the
                // variability damper
                if (gpLastDataValue[gActionList[x].type] != NULL) {
                    z = abs(dataDifference(gpLastDataValue[gActionList[x].type],
                                           (Data *) gActionList[x].pData));
                    z = z / gVariabilityDamper[gActionList[x].type];
                    if (z > gPeakVariability[gActionList[x].type]) {
                        gPeakVariability[gActionList[x].type] = z;
                    }
                }
                gpLastDataValue[gActionList[x].type] = (Data *) gActionList[x].pData;
            }
            *ppRanked = &(gActionList[x]);
            ppRanked++;
        }
    }

    // Run through the rankings once for each condition
    for (unsigned int x = 0; x < 5; x++) {
        ranker (&condition);
    }

    // Use the ranked list to assemble the list of ranked action types
    z = 0;
    for (unsigned int x = 0; (x < ARRAY_SIZE(gpRankedList)) && (gpRankedList[x] != NULL); x++) {
        // Check that the type is not already in the list
        found = false;
        for (unsigned int y = 0; (y < ARRAY_SIZE(gRankedTypes)) && !found; y++) {
            found = (gRankedTypes[y] == gpRankedList[x]->type);
        }
        // If it's not in the list and hasn't been given a desirability
        // of zero then add it
        if (!found && (gDesirability[gpRankedList[x]->type] > 0)) {
            MBED_ASSERT(z < ARRAY_SIZE(gRankedTypes));
            gRankedTypes[z] = gpRankedList[x]->type;
            z++;
        }
    }

    // Find out if there are any actions with a non-zero desirability that are
    // not in the list of ranked action types
    memset (&desirability, 0, sizeof(desirability));
    for (unsigned int x = ACTION_TYPE_NULL + 1; x < ARRAY_SIZE(gDesirability); x++) {
        if (gDesirability[x] > 0) {
            found = false;
            for (unsigned int y = 0; (y < ARRAY_SIZE(gRankedTypes)) && !found; y++) {
                found = (gRankedTypes[y] == x);
            }
            if (!found) {
                desirability[x] = gDesirability[x];
            }
        }
    }

    // Make a list of these action types in order of desirability
    z = 0;
    d = 1;
    memset(&actionTypeSortedByDesirability, ACTION_TYPE_NULL, sizeof (actionTypeSortedByDesirability));
    for (unsigned int x = 0; (d > 0) && (x < ARRAY_SIZE(actionTypeSortedByDesirability)); x++) {
        d = 0;
        a = ACTION_TYPE_NULL;
        for (unsigned int y = ACTION_TYPE_NULL + 1; y < ARRAY_SIZE(desirability); y++) {
            if (desirability[y] > d) {
                d = desirability[y];
                a = (ActionType) y;
            }
        }
        if (a != ACTION_TYPE_NULL) {
            actionTypeSortedByDesirability[z] = a;
            desirability[a] = 0;
            z++;
        }
    }

    // Add any actions in the actionTypeSortedByDesirability list to the end of the ranked
    // action type list
    z = 0;
    for (unsigned int x = 0;
        (x < sizeof (gRankedTypes)) && (actionTypeSortedByDesirability[z] != ACTION_TYPE_NULL) && (z < ARRAY_SIZE(actionTypeSortedByDesirability));
        x++) {
        if (gRankedTypes[x] == ACTION_TYPE_NULL) {
            gRankedTypes[x] = actionTypeSortedByDesirability[z];
            z++;
        }
    }

    // Set the next action type pointer to the start of the ranked action types
    gpNextActionType = &(gRankedTypes[0]);

    MTX_UNLOCK(gMtx);

    return actionRankNextType();
}

// Return the action pointer to the start of the ranked list.
ActionType actionRankFirstType()
{
    gpNextActionType = &(gRankedTypes[0]);

    return actionRankNextType();
}

// Move a given action type to the given position in the ranked list.
ActionType actionRankMoveType(ActionType actionType, unsigned int position)
{
    unsigned int currentPosition;
    unsigned int numRankedActionTypes;

    MTX_LOCK(gMtx);

    // Determine the number of action types in the list, on the way
    // finding the current index of the action type we are to move
    currentPosition = 0xFFFFFF;
    for (numRankedActionTypes = 0;
         (numRankedActionTypes < ARRAY_SIZE(gRankedTypes)) &&
         (gRankedTypes[numRankedActionTypes] != ACTION_TYPE_NULL);
         numRankedActionTypes++) {
        if (gRankedTypes[numRankedActionTypes] == actionType) {
            currentPosition = numRankedActionTypes;
        }
    }

    // Only continue if there are action types and the one we've been
    // asked to move is present in the list
    if ((numRankedActionTypes > 0) && (currentPosition != 0xFFFFFF)) {

        // Sort out the limits of the index we're going to move it to
        if (position >= numRankedActionTypes) {
            position = numRankedActionTypes - 1;
        }

        // Ignore the silly case
        if (currentPosition != position) {
            // Jump past things that don't need to move
            if (currentPosition < position) {
                // Move everything up by one, overwriting currentPosition
                // in the process
                for (unsigned int x = currentPosition; x < position; x++) {
                    gRankedTypes[x] = gRankedTypes[x + 1];
                }
            } else {
                // Move everything down by one from position, overwriting
                // currentPosition in the process
                for (unsigned int x = currentPosition; x > position; x--) {
                    gRankedTypes[x] = gRankedTypes[x - 1];
                }
            }
            // Write the wanted type in at the new position
            gRankedTypes[position] = actionType;
        }
    }

    // Set the next action type pointer to the start of the ranked action types
    gpNextActionType = &(gRankedTypes[0]);

    MTX_UNLOCK(gMtx);

    return actionRankNextType();
}

// Delete an action type from the ranked list.
ActionType actionRankDelType(ActionType actionType)
{
    unsigned int position;
    unsigned int numRankedActionTypes;

    MTX_LOCK(gMtx);

    // Find the action type in the list
    position = 0xFFFFFF;
    for (numRankedActionTypes = 0;
         (numRankedActionTypes < ARRAY_SIZE(gRankedTypes)) &&
         (gRankedTypes[numRankedActionTypes] != ACTION_TYPE_NULL);
         numRankedActionTypes++) {
        if (gRankedTypes[numRankedActionTypes] == actionType) {
            position = numRankedActionTypes;
        }
    }

    // If we found it...
    if ((numRankedActionTypes > 0) && (position != 0xFFFFFF)) {
        // Move everything up by one, overwriting position
        // in the process
        for (unsigned int x = position; x < numRankedActionTypes - 1; x++) {
            gRankedTypes[x] = gRankedTypes[x + 1];
        }
        // Then put an empty entry at the end
        gRankedTypes[numRankedActionTypes - 1] = ACTION_TYPE_NULL;
    }

    // Set the next action type pointer to the start of the ranked action types
    gpNextActionType = &(gRankedTypes[0]);

    MTX_UNLOCK(gMtx);

    return actionRankNextType();
}

// Lock the action list.
void actionLockList()
{
    gMtx.lock();
}

// Unlock the action list.
void actionUnlockList()
{
    gMtx.unlock();
}

// Print an action for debug purpose.
void actionPrint(const Action *pAction)
{
    MTX_LOCK(gMtx);

    PRINTF("Action ");
    printAction(pAction);

    MTX_UNLOCK(gMtx);
}

// Print the action list for debug purposes.
void actionPrintList()
{
    int numActions;

    MTX_LOCK(gMtx);

    numActions = 0;
    PRINTF("Action list:\n");
    for (unsigned int x = 0; (x < ARRAY_SIZE(gpRankedList)) && (gpRankedList[x] != NULL); x++) {
        if ((gActionList[x].state != ACTION_STATE_NULL) &&
            (gActionList[x].state != ACTION_STATE_ABORTED)&&
            (gActionList[x].state != ACTION_STATE_TRIED_AND_FAILED)) {
            printAction(&(gActionList[x]));
            numActions++;
        }
    }

    PRINTF("%d action(s) in the list.\n", numActions);

    MTX_UNLOCK(gMtx);
}

// Print the ranked action types for debug purposes.
void actionPrintRankedTypes()
{
    int numActionTypes;

    MTX_LOCK(gMtx);

    numActionTypes = 0;
    PRINTF("Ranked action types:\n");
    for (unsigned int x = 0; (x < ARRAY_SIZE(gRankedTypes)) && (gRankedTypes[x] != ACTION_TYPE_NULL); x++) {
        numActionTypes++;
        PRINTF("%2d: %s.\n", numActionTypes, gActionTypeString[gRankedTypes[x]]);
    }

    MTX_UNLOCK(gMtx);
}

// End of file
