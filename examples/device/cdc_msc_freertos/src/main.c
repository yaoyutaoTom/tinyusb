/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"
#include "semphr.h"

#include "bsp/board.h"
#include "tusb.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

// static timer
StaticTimer_t static_blink;
TimerHandle_t blink_tm;

// static task for usbd
#define USBD_STACK_SIZE     150
StackType_t  stack_usbd[USBD_STACK_SIZE];
StaticTask_t static_task_usbd;

// static task for cdc
#define CDC_STACK_SZIE      128
StackType_t  stack_cdc[CDC_STACK_SZIE];
StaticTask_t static_task_cdc;


void led_blinky_cb(TimerHandle_t xTimer);
void usb_device_task(void* param);
void cdc_task(void* params);

/*------------- MAIN -------------*/
int main(void)
{
  board_init();

  // soft timer for blinky
  blink_tm = xTimerCreateStatic(NULL, pdMS_TO_TICKS(BLINK_NOT_MOUNTED), true, NULL, led_blinky_cb, &static_blink);
  xTimerStart(blink_tm, 0);

  tusb_init();

  // Create a task for tinyusb device stack
  (void) xTaskCreateStatic( usb_device_task, "usbd", USBD_STACK_SIZE, NULL, configMAX_PRIORITIES-1, stack_usbd, &static_task_usbd);

  // Create task
#if CFG_TUD_CDC
  (void) xTaskCreateStatic( cdc_task, "cdc", CDC_STACK_SZIE, NULL, configMAX_PRIORITIES-2, stack_cdc, &static_task_cdc);
#endif

  vTaskStartScheduler();
  NVIC_SystemReset();

  return 0;
}

// USB Device Driver task
// This top level thread process all usb events and invoke callbacks
void usb_device_task(void* param)
{
  (void) param;

  // RTOS forever loop
  while (1)
  {
    // tinyusb device task
    tud_task();
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  xTimerChangePeriod(blink_tm, pdMS_TO_TICKS(BLINK_MOUNTED), 0);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  xTimerChangePeriod(blink_tm, pdMS_TO_TICKS(BLINK_NOT_MOUNTED), 0);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  xTimerChangePeriod(blink_tm, pdMS_TO_TICKS(BLINK_SUSPENDED), 0);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  xTimerChangePeriod(blink_tm, pdMS_TO_TICKS(BLINK_MOUNTED), 0);
}


//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
#if CFG_TUD_CDC
void cdc_task(void* params)
{
  (void) params;

  // RTOS forever loop
  while ( 1 )
  {
    if ( tud_cdc_connected() )
    {
      // connected and there are data available
      if ( tud_cdc_available() )
      {
        uint8_t buf[64];

        // read and echo back
        uint32_t count = tud_cdc_read(buf, sizeof(buf));

        for(uint32_t i=0; i<count; i++)
        {
          tud_cdc_write_char(buf[i]);

          if ( buf[i] == '\r' ) tud_cdc_write_char('\n');
        }

        tud_cdc_write_flush();
      }
    }

    taskYIELD();
  }
}

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  (void) itf;

  // connected
  if ( dtr && rts )
  {
    // print initial message when connected
    tud_cdc_write_str("\r\nTinyUSB CDC MSC HID device with FreeRTOS example\r\n");
  }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
  (void) itf;
}

#endif // CFG_TUD_CDC

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinky_cb(TimerHandle_t xTimer)
{
  (void) xTimer;
  static bool led_state = false;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
