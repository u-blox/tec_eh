#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "mbed_trace.h"
#include "mbed.h"
#include "eh_action.h"
#include "eh_data.h"
#define TRACE_GROUP "ACTION"

using namespace utest::v1;

// These are tests for the eh_data module.
// To run them, before you do "mbed test", you need
// to (once) do "mbedls --m 0004:UBLOX_EVK_NINA_B1" to
// set up the right target name, otherwise Mbed thinks
// you have an LPC2368 attached.
//
// ----------------------------------------------------------------
// COMPILE-TIME MACROS
// ----------------------------------------------------------------

// ----------------------------------------------------------------
// PRIVATE VARIABLES
// ----------------------------------------------------------------

// Lock for debug prints
static Mutex gMtx;

// Large list of data pointers so that we can keep track while
// filling up the heap
static Data *gpData[4000];

// Storage for data contents
static DataContents gContents;

// ----------------------------------------------------------------
// PRIVATE FUNCTIONS
// ----------------------------------------------------------------

#ifdef MBED_CONF_MBED_TRACE_ENABLE
// Locks for debug prints
static void lock()
{
    gMtx.lock();
}

static void unlock()
{
    gMtx.unlock();
}
#endif

// Return a randomly selected action type
static ActionType randomActionType()
{
    return (ActionType) ((rand() % (MAX_NUM_ACTION_TYPES - 1)) + 1); // -/+1 to avoid the NULL action
}

// Return a randomly selected data type
static DataType randomDataType()
{
    return (DataType) ((rand() % (MAX_NUM_DATA_TYPES - 1)) + 1); // -/+1 to avoid the NULL data type
}

// ----------------------------------------------------------------
// TESTS
// ----------------------------------------------------------------

// Test of adding and removing data
void test_alloc_free() {
    mbed_stats_heap_t statsHeapBefore;
    mbed_stats_heap_t statsHeapAfter;
    Action action;
    Data *pData;
    DataType dataType = (DataType) (DATA_TYPE_NULL + 1);
    int y = 0;
    int z;

    tr_debug("Print something out as tr_debug seems to allocate from the heap when first called.\n");

    // Fill gContents with stuff and null the pointers
    memset (&gContents, 0xAA, sizeof (gContents));
    memset (gpData, NULL, sizeof(gpData));

    // Set an initial action type (not that the action
    // type should matter anyway) and data type
    action.type = randomActionType();
    dataType = randomDataType();

    // Capture the heap stats before we start
    mbed_stats_heap_get(&statsHeapBefore);
    tr_debug("%d byte(s) of heap used at the outset.", statsHeapBefore.current_size);

    // Now allocate and free data items randomly, making sure
    // to use different data types, and to alloc more than we free,
    // causing pDataAlloc to eventually fail
    for (unsigned int x = 0; (pData = pDataAlloc(&action, dataType, &gContents)) != NULL; x++) {
        TEST_ASSERT((Data *) action.pData == pData);
        gpData[x] = pData;
        y++;
        if (x % (rand() % 5) == 0) {
            z = 0;
            if (x != 0) {
                z = rand() % x;
            }
            if (gpData[z] != NULL) {
              dataFree(&(gpData[z]));
              y--;
            }
        }
        action.type = randomActionType();
        dataType = randomDataType();
    }

    TEST_ASSERT (pData == NULL);
    tr_debug("%d data item(s) filled up memory.", y);

    // Now free any that remain allocated
    for (unsigned int x = 0; x < ARRAY_SIZE(gpData); x++) {
        if (gpData[x] != NULL) {
            dataFree(&(gpData[x]));
        }
    }

    // Having done all that, capture the heap stats once more
    mbed_stats_heap_get(&statsHeapAfter);
    tr_debug("%d byte(s) of heap used at the end.", statsHeapAfter.current_size);

    // The heap used should be the same as at the start
    TEST_ASSERT(statsHeapBefore.current_size == statsHeapAfter.current_size);
}

// ----------------------------------------------------------------
// TEST ENVIRONMENT
// ----------------------------------------------------------------

// Setup the test environment
utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea with a timeout
    GREENTEA_SETUP(60, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}

// Test cases
Case cases[] = {
    Case("Add alloc and free", test_alloc_free)
};

Specification specification(test_setup, cases);

// ----------------------------------------------------------------
// MAIN
// ----------------------------------------------------------------

int main()
{

#ifdef MBED_CONF_MBED_TRACE_ENABLE
    mbed_trace_init();

    mbed_trace_mutex_wait_function_set(lock);
    mbed_trace_mutex_release_function_set(unlock);
#endif

    // Run tests
    return !Harness::run(specification);
}

// End Of File