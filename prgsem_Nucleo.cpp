/*
 * File name: prgsem.c
 * Date:      2022/05/7 6:03 PM
 * Author:    Aleksandr Zelik
 * Tusk text: https://cw.fel.cvut.cz/wiki/courses/b3b36prg/semestral-project/start
*/
#include "mbed.h"
#include "prgsem/messages.h"
#include "stdlib.h"

Serial serial(USBTX, USBRX);
DigitalOut compute_led(LED1);
InterruptIn button_event(USER_BUTTON);
Ticker ticker;

void Tx_interrupt();
void Rx_interrupt();
uint8_t __ComputeFrameNucleo__(int re, int im);
bool send_message(const message *msg, uint8_t *buf, int size);
bool send_buffer(const uint8_t* msg, unsigned int size);
bool receive_message(uint8_t *msg_buf, int size, int *len);

#define BUF_SIZE 255
#define MESSAGE_SIZE sizeof(message)
#define VERSION_MAJOR 0
#define VERSION_MINOR 1
#define VERSION_PATCH 0
#define PERIOD_COMPUTING 0.1
#define COMPUTING_TIME 0.2
#define MAX_RESULT_VALUE 255
#define BAUD_RATE_STANDART 115200
#define BAUD_RATE_FAST 576000

enum state {State_OFF, State_ON};

char tx_buffer[BUF_SIZE];
char rx_buffer[BUF_SIZE];
 
// pointers to the circular buffers
volatile int tx_in = 0;
volatile int tx_out = 0;
volatile int rx_in = 0;
volatile int rx_out = 0;

volatile bool abort_request = false;

typedef struct Main_computation {
    uint8_t cid; // local chunk id
    uint8_t n; // local amount of chunks
    double c_Re, c_Im, d_Re, d_Im, s_Re, s_Im; // local parametrs of image
    uint8_t chunk_w, chunk_h; // local size of each chunk
    int act_Re, act_Im; // actual real and complex value
    bool computing, actual_comp_done;
    bool fast_mode;
} Main_computation;

Main_computation computation = { 0, 
                                0,
                                0, 0, 0, 0, 0, 0,
                                0, 0, 
                                0, 0,
                                false, true,
                                false};

void button() { abort_request = true; }

void tick() { compute_led = !compute_led; }

int main() {

    // Inicializating part
    serial.baud(BAUD_RATE_STANDART);
    serial.attach(&Rx_interrupt, Serial::RxIrq); // attach interrupt handler to receive data
    serial.attach(&Tx_interrupt, Serial::TxIrq); // attach interrupt handler to transmit data
    
    button_event.rise(&button);

    compute_led = State_OFF;

    for (int i = 0; i < 10; i++) {
        compute_led = !compute_led;
        wait(0.1);
    }
    while(serial.readable()) { serial.getc(); }
    
    message msg = { .type = MSG_STARTUP, .data.startup.message = {'Z', 'E', 'L', 'I', 'K', '-', 'S', 'E', 'M'}};
    uint8_t msg_buf[MESSAGE_SIZE];
    int msg_len = 0;
    send_message(&msg, msg_buf, msg_len);
    bool baud_changed = false;
    uint8_t re = 0, im = 0; // local coordinates into each chunk

    // Working part
    while(true) {
        baud_changed = false;
        if (abort_request && computation.computing) {  // abort computing
            msg.type = MSG_ABORT;
            send_message(&msg, msg_buf, msg_len);
            computation.computing = false;
            abort_request = false;
            ticker.detach();
            compute_led = State_OFF;
        }
        
        if (rx_in != rx_out && 
            receive_message(msg_buf, MESSAGE_SIZE, &msg_len) && 
            parse_message_buf(msg_buf, msg_len, &msg)
        ) {
            switch(msg.type) {

// --------------------------------------------------------------------------------------------------
                case MSG_GET_VERSION:
                    msg.type = MSG_VERSION;
                    msg.data.version.major = VERSION_MAJOR;
                    msg.data.version.minor = VERSION_MINOR;
                    msg.data.version.patch = VERSION_PATCH;
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_SET_COMPUTE:
                    if (computation.computing) {
                        msg.type = MSG_ERROR;
                    } else {
                        msg.type = MSG_OK;

                        computation.c_Re = msg.data.set_compute.c_re;
                        computation.c_Im = msg.data.set_compute.c_im;
                        computation.d_Re = msg.data.set_compute.d_re;
                        computation.d_Im = msg.data.set_compute.d_im;

                        computation.n = msg.data.set_compute.n;
                        
                    }
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_COMPUTE:
                    
                    if (computation.computing) {
                        
                        msg.type = MSG_ERROR;
                    } else {
                        msg.type = MSG_OK;
                        
                        computation.s_Re = msg.data.compute.re;
                        computation.s_Im = msg.data.compute.im;
                        computation.cid = msg.data.compute.cid;

                        computation.chunk_w = msg.data.compute.n_re; // size squrt chunk
                        /*Each chunk is a squrt, it's means not necessary know other site*/
                        computation.chunk_h = msg.data.compute.n_im; // quantity chunks in height

                        re = 0;
                        im = 0;

                        ticker.attach(&tick, PERIOD_COMPUTING);
                        computation.computing = true;
                        computation.actual_comp_done = false;
                    }
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_ABORT:
                    if (computation.computing) {
                        msg.type = MSG_OK;

                        computation.computing = false;
                        abort_request = false;
                        ticker.detach();
                        compute_led = State_OFF;
                    } else {
                        msg.type = MSG_ERROR;
                    }
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_SET_BAUD_RATE:
                    if (!computation.computing) {
                        if (computation.fast_mode) {
                            msg.type = MSG_SET_BAUD_RATE;
                            send_message(&msg, msg_buf, msg_len);
                            wait(0.1);
                            serial.baud(BAUD_RATE_STANDART);
                        } else {
                            msg.type = MSG_SET_BAUD_RATE;
                            send_message(&msg, msg_buf, msg_len);
                            wait(0.1);
                            serial.baud(BAUD_RATE_FAST);
                        }
                        baud_changed = true;
                        computation.fast_mode = !computation.fast_mode;
                    } else {
                        msg.type = MSG_ERROR;
                    }
                    break;
                default:
                    msg.type = MSG_ERROR;
                    break;
            } // end switch
            if (!baud_changed)
                send_message(&msg, msg_buf, msg_len);
        } // end if

        // computing part
        else if (computation.computing) { 

            if (im < computation.chunk_h) {
                msg.type = MSG_COMPUTE_DATA;

                msg.data.compute_data.cid = computation.cid;
                msg.data.compute_data.i_re = re;
                msg.data.compute_data.i_im = im;
                msg.data.compute_data.iter = __ComputeFrameNucleo__(re, im);
                
                re++;
                
                if (re == computation.chunk_w) {
                    re = 0;
                    im++;
                }
            } else { // computing is done
                ticker.detach();
                compute_led = State_OFF;
                msg.type = MSG_DONE;

                computation.act_Re = 0;
                computation.act_Im = 0;
                computation.computing = false;
                computation.actual_comp_done = true;
            }

            send_message(&msg, msg_buf, msg_len);
        } // end computing part 
        else {
            sleep();
        }
    } // end while

}

uint8_t __ComputeFrameNucleo__(int re, int im) {
    uint8_t ret = 0;
    double new_R = computation.s_Re + computation.d_Re * re, 
            new_I = computation.s_Im - computation.d_Im * im, 
            old_R, old_I;

    while (((new_R*new_R + new_I*new_I) < 4) && (ret < computation.n)) {
        old_R = new_R;
        old_I = new_I;
        new_R = old_R*old_R - old_I*old_I + computation.c_Re;
        new_I = 2*old_R*old_I + computation.c_Im;
        ret++;
    }
    
    return ret;
}

// - function -----------------------------------------------------------------
void Tx_interrupt()
{
    // send a single byte as the interrupt is triggered on empty out buffer 
    if (tx_in != tx_out) {
        serial.putc(tx_buffer[tx_out]);
        tx_out = (tx_out + 1) % BUF_SIZE;
    } else { // buffer sent out, disable Tx interrupt
        USART2->CR1 &= ~USART_CR1_TXEIE; // disable Tx interrupt
    }
    return;
}

// - function -----------------------------------------------------------------
void Rx_interrupt()
{
    // receive bytes and stop if rx_buffer is full
    while ((serial.readable()) && (((rx_in + 1) % BUF_SIZE) != rx_out)) {
        rx_buffer[rx_in] = serial.getc();
        rx_in = (rx_in + 1) % BUF_SIZE;
    }
    return;
}

// - function -----------------------------------------------------------------
bool send_message(const message *msg, uint8_t *buf, int size) {
    return fill_message_buf(msg, buf, MESSAGE_SIZE, &size) && send_buffer(buf, size);
}

// - function -----------------------------------------------------------------
bool send_buffer(const uint8_t* msg, unsigned int size)
{
    if (!msg && size == 0) {
        return false;    // size must be > 0
    }
 
    int i = 0;
    NVIC_DisableIRQ(USART2_IRQn); // start critical section for accessing global data
 
    while ( (i == 0) || i < size ) { //end reading when message has been read
        if ( ((tx_in + 1) % BUF_SIZE) == tx_out) { // needs buffer space
            NVIC_EnableIRQ(USART2_IRQn); // enable interrupts for sending buffer
            while (((tx_in + 1) % BUF_SIZE) == tx_out) {
                /// let interrupt routine empty the buffer
            }
            NVIC_DisableIRQ(USART2_IRQn); // disable interrupts for accessing global buffer
        }
 
        tx_buffer[tx_in] = msg[i];
        i += 1;
        tx_in = (tx_in + 1) % BUF_SIZE;
    } // send buffer has been put to tx buffer, enable Tx interrupt for sending it out
 
    USART2->CR1 |= USART_CR1_TXEIE; // enable Tx interrupt
 
    NVIC_EnableIRQ(USART2_IRQn); // end critical section
 
    return true;
}

// - function -----------------------------------------------------------------
// read massege from serial port
bool receive_message(uint8_t *msg_buf, int size, int *len)
{
    bool ret = false;
    int i = 0;
    *len = 0; // message size

    NVIC_DisableIRQ(USART2_IRQn); // start critical section for accessing global data
 
    while ( ((i == 0) || (i != *len)) && i < size ) {
        if (rx_in == rx_out) { // wait if buffer is empty
            NVIC_EnableIRQ(USART2_IRQn); // enable interrupts for receing buffer
            while (rx_in == rx_out) { // wait of next character
            }
            NVIC_DisableIRQ(USART2_IRQn); // disable interrupts for accessing global buffer
        }
 
        uint8_t c = rx_buffer[rx_out];
        if (i == 0) { // message type
            
            if (get_message_size(c, len)) { // message type recognized
                msg_buf[i++] = c;
                ret = *len <= size; // msg_buffer must be large enough
            } else {
                ret = false;
                break; // unknown message
            }
            
        } else {
            msg_buf[i++] = c;
        }
        rx_out = (rx_out + 1) % BUF_SIZE;
    }
 
    NVIC_EnableIRQ(USART2_IRQn); // end critical section
    
    return ret;
}
