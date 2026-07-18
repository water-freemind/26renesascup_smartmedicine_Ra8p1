#include "blinky_thread.h"
#include "hal_data.h"

void hal_entry (void)
{
    blinky_thread_entry(NULL);
}
