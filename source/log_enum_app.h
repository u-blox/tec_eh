/** You must provide a log_enum_app.h file containing your
 * application's log events.  This is a sample of the format.
 * It must be kept in line with the strings in log_strings_app.h.
 */
    EVENT_PROTOCOL_VERSION,
    EVENT_SYSTEM_VERSION,
    EVENT_WAITING_ENERGY,
    EVENT_POST_ERROR,
    EVENT_POST_BEST_EFFORT,
    EVENT_IMEI_ENDING,
    EVENT_WAKE_UP,
    EVENT_POWER,
    EVENT_AWAKE,
    EVENT_MODEM_TYPE,
    EVENT_ACTION,
    EVENT_ACTION_ALLOC_FAILURE,
    EVENT_ACTION_THREAD_ALLOC_FAILURE,
    EVENT_ACTION_THREAD_STARTED,
    EVENT_ACTION_THREAD_START_FAILURE,
    EVENT_ACTION_THREAD_TERMINATED,
    EVENT_ACTION_THREAD_SIGNALLED,
    EVENT_ACTION_THREADS_RUNNING,
    EVENT_ACTION_DRIVER_INIT_FAILURE,
    EVENT_ACTION_DRIVER_HEAP_TOO_LOW,
    EVENT_TIME_SET,
    EVENT_GET_IMEI_FAILURE,
    EVENT_CONNECT_FAILURE,
    EVENT_GET_TIME_FAILURE,
    EVENT_ALL_THREADS_TERMINATED,
    EVENT_DATA_ITEM_ALLOC,
    EVENT_DATA_ITEM_ALLOC_FAILURE,
    EVENT_DATA_ITEM_FREE,
    EVENT_DATA_CURRENT_SIZE_BYTES,
    EVENT_DATA_CURRENT_QUEUE_BYTES,
    EVENT_PROCESSOR_FINISHED,
    EVENT_V_BAT_OK_READING_MV,
    EVENT_V_IN_READING_MV,
    EVENT_V_PRIMARY_READING_MV,
    EVENT_V_IN_READING_AVERAGED_MV,
    EVENT_ENERGY_AVAILABLE_NWH,
    EVENT_ENERGY_AVAILABLE_UWH,
    EVENT_ENERGY_SOURCE,
    EVENT_ENERGY_SOURCE_SET,
    EVENT_ENERGY_SOURCE_CHOICE_MEASURED,
    EVENT_ENERGY_SOURCE_CHOICE_RANDOM,
    EVENT_ENERGY_SOURCE_CHOICE_SEQUENCE,
    EVENT_ENERGY_SOURCE_CHOICE_HISTORY,
    EVENT_ACTION_REMOVED_ENERGY_LIMIT,
    EVENT_ACTION_REMOVED_QUEUE_LIMIT,
    EVENT_ENERGY_REQUIRED_NWH,
    EVENT_ENERGY_REQUIRED_UWH,
    EVENT_ENERGY_REQUIRED_TOTAL_NWH,
    EVENT_ENERGY_REQUIRED_TOTAL_UWH,
    EVENT_HEAP_LEFT,
    EVENT_STACK_MIN_LEFT,
    EVENT_THIS_STACK_MIN_LEFT,
    EVENT_HEAP_MIN_LEFT,
    EVENT_ENERGY_USED_NWH,
    EVENT_ENERGY_USED_UWH,
    EVENT_NOT_ENOUGH_POWER_TO_RUN_PROCESSOR,
    EVENT_PROCESSOR_RUNNING,
    EVENT_MAX_PROCESSOR_RUN_TIME_REACHED,
    EVENT_RETURN_TO_SLEEP,
    EVENT_MBED_DIE_CALLED,
    EVENT_RESTART,
    EVENT_RESTART_TIME,
    EVENT_RESTART_LINK_REGISTER,
    EVENT_RESTART_FATAL_ERROR_TYPE,
    EVENT_RESTART_FATAL_ERROR_CODE,
    EVENT_RESTART_FATAL_ERROR_MODULE,
    EVENT_RESTART_FATAL_ERROR_ADDRESS,
    EVENT_RESTART_FATAL_ERROR_VALUE,
    EVENT_RESTART_FATAL_ERROR_THREAD_ID,
    EVENT_RESTART_FATAL_ERROR_THREAD_ENTRY_ADDRESS,
    EVENT_RESTART_FATAL_ERROR_THREAD_STACK_SIZE,
    EVENT_RESTART_FATAL_ERROR_THREAD_STACK_MEM,
    EVENT_RESTART_FATAL_ERROR_THREAD_CURRENT_SP,
    EVENT_POSITION_BACK_OFF_SECONDS,
    EVENT_CELLULAR_OFF_NOW,
    EVENT_CME_ERROR,
    EVENT_MODEM_ENTERED_PSM,
    EVENT_MODEM_CSCON_STATE

