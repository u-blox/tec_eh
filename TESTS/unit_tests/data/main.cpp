#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "mbed_trace.h"
#include "mbed.h"
#include "eh_utilities.h" // For ARRAY_SIZE
#include "eh_action.h"
#include "eh_data.h"

using namespace utest::v1;

// These are tests for the eh_data module.
//
// ----------------------------------------------------------------
// COMPILE-TIME MACROS
// ----------------------------------------------------------------

#define TRACE_GROUP "DATA"
#define BUFFER_GUARD 0x12345678

// ----------------------------------------------------------------
// PRIVATE VARIABLES
// ----------------------------------------------------------------

// Lock for debug prints
static Mutex gMtx;

// Large list of data pointers so that we can keep track while
// filling up the heap
static Data *gpData[8000];

// Storage for data contents
static DataContents gContents;

// Remember if we're using the internal data buffer
// as the sorting tests can't check for that case
static bool gInternalDataBuffer = false;

// A guard before the buffer
static int gBufferPre = BUFFER_GUARD;
// A data buffer;
static int gBuffer[DATA_MAX_SIZE_WORDS];
// A guard after the buffer
static int gBufferPost = BUFFER_GUARD;

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

// Return randomly selected flags
static unsigned char randomFlags()
{
    unsigned char flags = 0;
    int x = rand() % 3;

    switch (x) {
        case 0:
            flags |= DATA_FLAG_REQUIRES_ACK;
        break;
        case 1:
            flags |= DATA_FLAG_SEND_NOW;
        break;
        case 2:
            flags |= DATA_FLAG_REQUIRES_ACK | DATA_FLAG_SEND_NOW;
        break;
    }

    return flags;
}

// Use an internal data buffer
static void setInternalBuffer() {
   // Initialise data with a buffer
    dataInit(gBuffer);
    gInternalDataBuffer = true;
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
    memset (&gpData, 0, sizeof(gpData));

    // Set an initial action type (not that the action
    // type should matter anyway) and data type
    action.type = randomActionType();
    dataType = randomDataType();

    // Capture the heap stats before we start
    mbed_stats_heap_get(&statsHeapBefore);
    tr_debug("%d byte(s) of heap used at the outset.", (int) statsHeapBefore.current_size);

    // Now allocate and free data items randomly, making sure
    // to use different data types, and to alloc more than we free,
    // causing pDataAlloc to eventually fail
    for (unsigned int x = 0; ((pData = pDataAlloc(&action, dataType, 0, &gContents)) != NULL) && (x < ARRAY_SIZE(gpData)); x++) {
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

    tr_debug("%d data item(s) filled up memory (if test failure occurs on the next line you probably need to increase the size of gpData[]).", y);
    TEST_ASSERT(pData == NULL);

    // Now free any that remain allocated
    for (unsigned int x = 0; x < ARRAY_SIZE(gpData); x++) {
        if (gpData[x] != NULL) {
            dataFree(&(gpData[x]));
        }
    }

    // Having done all that, capture the heap stats once more
    mbed_stats_heap_get(&statsHeapAfter);
    tr_debug("%d byte(s) of heap used at the end.", (int) statsHeapAfter.current_size);

    // The heap used should be the same as at the start
    TEST_ASSERT(statsHeapBefore.current_size == statsHeapAfter.current_size);

    // Check that the guards are still good
    TEST_ASSERT(gBufferPre == BUFFER_GUARD);
    TEST_ASSERT(gBufferPost == BUFFER_GUARD);
}

// Test sorting of data
void test_sort() {
    mbed_stats_heap_t statsHeapBefore;
    mbed_stats_heap_t statsHeapAfter;
    Action action;
    Data *pThis;
    Data *pNext;
    unsigned char flags;
    DataType dataType = (DataType) (DATA_TYPE_NULL + 1);
    unsigned int x = 0;
    unsigned int y = 0;
    Timer timer;

    tr_debug("Print something out with a float in it %f as tr_debug and float prints seems to allocate from the heap when first called.\n", 1.0);

    // Fill gContents with stuff
    memset (&gContents, 0xAA, sizeof (gContents));

    // Set an initial action type (not that the action
    // type should matter anyway) and data type
    action.type = randomActionType();
    dataType = randomDataType();
    flags = randomFlags();

    // Capture the heap stats before we start
    mbed_stats_heap_get(&statsHeapBefore);
    tr_debug("%d byte(s) of heap used at the outset.", (int) statsHeapBefore.current_size);

    // Now allocate up to 500 data items with randomly set flags and time
    // Note: could try to fill up memory but on some platforms that results
    // in a lot of data items which will take a long time to sort
    for (x = 0; (x < 500) && ((pThis = pDataAlloc(&action, dataType, flags, &gContents)) != NULL); x++) {
        TEST_ASSERT((Data *) action.pData == pThis);
        pThis->timeUTC = rand() & 0x7FFFFFFF;
        action.type = randomActionType();
        dataType = randomDataType();
        flags = randomFlags();
    }

    tr_debug("%d data item(s) to sort.", x);

    // Sort the list and check that it is as expected
    tr_debug("Sorting this list, might take a while (if this test fails with TIMEOUT then try increasing the guard timer in GREENTEA_SETUP() below)...");
    y = 0;
    timer.reset();
    timer.start();
    pThis = pDataSort();
    timer.stop();
    tr_debug("Sorting completed, after %.3f second(s) (noting that the sort guard timer is %d seconds).",
            (float) timer.read_ms() / 1000, DATA_SORT_GUARD_TIMER_MS);
    TEST_ASSERT(timer.read_ms() / 1000 < DATA_SORT_GUARD_TIMER_MS)
    while (pThis != NULL) {
        y++;
        pNext = pDataNext();
        if (!gInternalDataBuffer) {
            if (pNext != NULL) {
                TEST_ASSERT(pThis->flags >= pNext->flags);
                if (pThis->flags == pNext->flags) {
                    TEST_ASSERT(pThis->timeUTC >= pNext->timeUTC);
                }
            }
        }
        pThis = pNext;
    }

    tr_debug("%d data item(s) in sorted list.", y);
    TEST_ASSERT(x == y);

    // Finally, free the data
    y = 0;
    pThis = pDataSort();
    while (pThis != NULL) {
        y++;
        dataFree(&pThis);
        pThis = pDataNext();
    }

    TEST_ASSERT(x == y);

    // Having done all that, capture the heap stats once more
    mbed_stats_heap_get(&statsHeapAfter);
    tr_debug("%d byte(s) of heap used at the end.", (int) statsHeapAfter.current_size);

    // The heap used should be the same as at the start
    TEST_ASSERT(statsHeapBefore.current_size == statsHeapAfter.current_size);

    // Check that the guards are still good
    TEST_ASSERT(gBufferPre == BUFFER_GUARD);
    TEST_ASSERT(gBufferPost == BUFFER_GUARD);
}

void test_alloc_free_internal_buffer() {
    // Initialise data with a buffer
     dataInit(gBuffer);
     gInternalDataBuffer = true;
     test_alloc_free();
}

void test_sort_internal_buffer() {
    // Initialise data with a buffer
     dataInit(gBuffer);
     gInternalDataBuffer = true;
     test_sort();
}

// ----------------------------------------------------------------
// TEST ENVIRONMENT
// ----------------------------------------------------------------

// Setup the test environment
utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea with a timeout
    // Note: leave plenty of time as filling up all of RAM with
    // random data and then sorting it can take a loooong time
    // if this is running on a platform with a lot of RAM.
    GREENTEA_SETUP(120, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}

// Test cases (internal buffer ones MUST be run after the others)
Case cases[] = {
    Case("Add alloc and free", test_alloc_free),
    Case("Sort", test_sort),
    Case("Add alloc and free, internal buffer", test_alloc_free_internal_buffer),
    Case("Sort, internal buffer", test_sort_internal_buffer)
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
