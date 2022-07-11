/*
 * File name: event_queue.h
 * Date:      2017/04/15 12:41
 * Author:    Jan Faigl
 */

#ifndef __EVENT_QUEUE_H__
#define __EVENT_QUEUE_H__

#include "messages.h"

typedef enum {
   EV_NUCLEO,
   EV_KEYBOARD,
   EV_NUM
} event_source;

typedef enum {
   EV_COMPUTE, // request compute on nucleo with particular 
   EV_RESET_CHUNK, //reset the chunk id
   EV_ABORT,
   EV_GET_VERSION,
   EV_THREAD_EXIT,
   EV_QUIT,
   EV_SERIAL,
   EV_SET_COMPUTE,
   EV_COMPUTE_CPU,
   EV_CLEAR_BUFFER,
   EV_REFRESH,
   EV_TYPE_NUM,

   EV_CHANGE_BAUD_RATE,

   EV_SET_CHANGING_PARAM,
   EV_CHANGE_PARAM,
   EV_CANCEL_CHANGING,
   EV_WRONG_INPUT_PC,
   EV_SAVE_IMAGE,

   EV_ANIMATION_PC_START,
   EV_ANIMATION_PC,

   EV_REFRESH_BUFF_IMAGE

} event_type;

typedef struct {
   int param;
} event_keyboard;

typedef struct {
   message *msg;
} event_serial;

typedef struct {
   event_source source;
   event_type type;
   union {
      int param;
      message *msg;
   } data;
} event;

void queue_init(void);
void queue_cleanup(void);

event queue_pop(void);

void queue_push(event ev);

#endif

/* end of event_queue.h */