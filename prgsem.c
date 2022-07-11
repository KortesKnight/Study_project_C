/*
 * File name: prgsem.c
 * Date:      2022/05/7 6:03 PM
 * Author:    Aleksandr Zelik
 * Tusk text: https://cw.fel.cvut.cz/wiki/courses/b3b36prg/semestral-project/start
*/

#include "stdio.h"
#include "stdlib.h"
#include "stdbool.h"
#include "stdint.h"

#include "termios.h"
#include "unistd.h"  // for STDIN_FILENO

#include "pthread.h"

#include "prg_serial_nonblock.h"
#include "messages.h"
#include "event_queue.h"
#include "screen.h"

#define SERIAL_READ_TIMOUT_MS 500 // timeout for reading from serial port
#define PREPARING_TO_READ msg_buf[0] = 0; known_type = false; correct_read = true;
#define MESSAGE_SIZE (sizeof(message))
#define EMPTY_SYMBOL 0
#define START_ID 0
#define UNKNOWN 0 
#define START_FRAME 0
#define START_INDX 0

#define EXTRA_SPACE 1

#define STEP_INC_SIZE 10

#define def_height 480
#define def_width 640
#define def_N_iter 60
#define def_cR -0.4
#define def_cI 0.6
#define def_aR -1.6
#define def_aI -1.1
#define def_bR 1.6
#define def_bI 1.1
#define def_inR 0.005
#define def_inI 0.005
#define def_chunk_size 2
#define def_q_frames 100
#define def_height 480
#define def_width 640

// #define MAX_RE_IM_VALUE 30

#define min_step 0.01
// #define MIN_SIZE_CHUNK 20

#define max(x_1, x_2) x_1 > x_2 ? x_1 : x_2;
#define min(x_1, x_2) x_1 < x_2 ? x_1 : x_2;

enum parametrs { STEP_IN, C_RE, C_IM, HEIGHT, WIDTH, N_IT, A_RE, A_IM, B_RE, B_IM };
const char *parametrs_name[] = {"STEP_IN", "C_RE", "C_IM", "HEIGHT", "WIDTH", "N_IT", "A_RE", "A_IM", "B_RE", "B_IM"};

enum main_sizes {
    MIN_WIDTH = 320,
    MIN_HEIGHT = 240,
    MAX_WIDTH = 1600,
    MAX_HEIGHT = 800,
    MAX_Q_CHUNKS = 255, // max quantity chunks
    MAX_N_ITTER = 100, // max amount of iteration
    MIN_N_ITTER = 10, // min amount of iteration
    MAX_RE_IM_VALUE = 30, // max real and complex value
    MIN_SIZE_CHUNK = 20
};

// shared data structure
typedef struct {
   bool quit;
   bool computing_CPU;
   Image *img;
   int fd; // serial port file descriptor
} data_t;

typedef struct Main_parameters {
    uint8_t N_iter;
    double cR, cI, aR, aI, bR, bI;
    uint16_t height, width;
    double inR, inI; // Increment in the corresponding coordinates
    int q_frames;
    uint8_t changing_param;
    bool confirm_new_screeen_size;
    double step_in;
} Main_parameters;

Main_parameters interfece_parametrs = { def_N_iter,
                    def_cR, def_cI, def_aR, def_aI, def_bR, def_bI,
                    def_height, def_width,
                    def_inR, def_inI,
                    def_q_frames,
                    C_RE,
                    true,
                    min_step
                    }; ///////////////////////

typedef struct Main_computation {
    bool computing, set_computing, ANIM_CPU, complete_ANIM_CPU;
    uint8_t cid;
    uint8_t chunk_size;
    double act_Re, act_Im;
    int id_w_in_chunk, id_h_in_chunk; 
    uint16_t idx_chunk_w, idx_chunk_h;
    int chuck_in_row, chuck_in_col, quantity_chunks;
    bool fast_mode;
} Main_computation;

Main_computation computation = {false, false, false, true, 
                                UNKNOWN, 
                                UNKNOWN, 
                                UNKNOWN, UNKNOWN, 
                                UNKNOWN, UNKNOWN, 
                                UNKNOWN, UNKNOWN, 
                                UNKNOWN, UNKNOWN, MAX_Q_CHUNKS,
                                false};

pthread_mutex_t mtx;
pthread_cond_t cond;

void call_termios(int reset);

// ------------ LOGIC ------------ //
void* input_thread(void*);
void* serial_rx_thread(void*); // serial receive buffer
void __ComputeFrameCPU__(Image *img, double cR, double cI); // compute one frame of image
uint8_t __CalculateChunckSize__(void);
void __CalculateIncrement__();
// --------------------------------// 

// ---------- INTERFACE ---------- //
void print_menu();
void print_choice();
void print_parametrs();
bool CanChangeDouble(double changing, int inc); // can I change current double value by inc step 
// --------------------------------// 

bool send_message(data_t *data, message *msg);

// export DISPLAY=192.168.56.1:0.0 // parameters for cmd in Linux
// export LIBGL_ALWAYS_INDIRECT=1 // parameters for cmd in Linux

// - main ---------------------------------------------------------------------
int main(int argc, char *argv[]) {

    // Inicializating part
    data_t data = { .quit = false, .computing_CPU = false, .img = NULL, .fd = -1};

    const char *serial = argc > 1 ? argv[1] : "/dev/ttyS11";
    data.fd = serial_open(serial);
    data.img = __InitImage__(def_width, def_height, def_chunk_size);

    if (data.fd == -1) {
        fprintf(stderr, "ERROR: Cannot open serial port %s\n", serial);
        exit(100);
    }

    enum { INPUT, SERIAL_RX, NUM_THREADS };
    const char *threads_names[] = { "Input", "Serial In"};

    void* (*thr_functions[])(void*) = { input_thread, serial_rx_thread };

    pthread_t threads[NUM_THREADS];
    pthread_mutex_init(&mtx, NULL); // initialize mutex with default attributes
    pthread_cond_init(&cond, NULL); // initialize mutex with default attributes

    call_termios(0);

    for (int i = 0; i < NUM_THREADS; ++i) {
        int r = pthread_create(&threads[i], NULL, thr_functions[i], &data);
        fprintf(stderr, "INFO: Create thread '%s' %s\n", threads_names[i], ( r == 0 ? "OK" : "FAIL") );
    }

    print_menu();
    
    message msg;
    int prev_ev_pc = MSG_NBR; // previous event from PC
    int inc = 0;
    bool quit = false;
    int frame = 0;
    double local_cR = 0;
    double local_cI = 0;

    // Working part

    while (!quit) {
        // example of the event queue
        event ev = queue_pop();
        // handle keyboard events
        if (ev.source == EV_KEYBOARD) {
            msg.type = MSG_NBR;
            prev_ev_pc = ev.type;

            switch(ev.type) {

// --------------------------------------------------------------------------------------------------
            case EV_GET_VERSION: // prepare packet for get version
                msg.type = MSG_GET_VERSION;
                fprintf(stderr, "INFO: Get version requested\n");
                break;
            
// --------------------------------------------------------------------------------------------------
            case EV_SET_COMPUTE:
                if (computation.computing) {
                   fprintf(stderr, "WARN: Set compute request discarded, it is currently computing\n"); 
                } else if (computation.set_computing) {
                    fprintf(stderr, "INFO: Parameters to compute already set\n\r");
                } else {
                    msg.type = MSG_SET_COMPUTE;
                    
                    // calculate increment step which depends on the size of the window
                    __CalculateIncrement__();

                    // create message to sent
                    msg.data.set_compute.c_re = interfece_parametrs.cR;
                    msg.data.set_compute.c_im = interfece_parametrs.cI;
                    msg.data.set_compute.d_re = interfece_parametrs.inR;
                    msg.data.set_compute.d_im = interfece_parametrs.inI;
                    msg.data.set_compute.n = interfece_parametrs.N_iter;
                    
                    // set parameters to computing
                    computation.idx_chunk_w = START_INDX;
                    computation.idx_chunk_h = START_INDX;
                    computation.chunk_size = __CalculateChunckSize__() + EXTRA_SPACE;
                    computation.chuck_in_row = interfece_parametrs.width / computation.chunk_size + EXTRA_SPACE;
                    computation.chuck_in_col = interfece_parametrs.height / computation.chunk_size + EXTRA_SPACE;
                    
                    // set actual start point to computing
                    computation.act_Re = min(interfece_parametrs.aR, interfece_parametrs.bR);
                    computation.act_Im = max(interfece_parametrs.aI, interfece_parametrs.bI);

                    // reset window's size if it is necessary
                    pthread_mutex_lock(&mtx);
                    if (data.img->width != interfece_parametrs.width || 
                        data.img->height != interfece_parametrs.height
                    ) {
                        xwin_close();
                        __FreeImage__(data.img);
                        data.img = __InitImage__((int)interfece_parametrs.width, 
                                            (int)interfece_parametrs.height, computation.chunk_size);
                    }
                    pthread_mutex_unlock(&mtx);

                    fprintf(stderr, "INFO: Set compute request\n\r");
                    
                    prev_ev_pc = EV_SET_COMPUTE;
                }
                break;

// --------------------------------------------------------------------------------------------------
            case EV_COMPUTE:
                if (computation.computing) {
                    fprintf(stderr, 
                        "WARN: New computation requested but it is discarded due on ongoing computation\n\r");
                } else if(!computation.set_computing){
                    fprintf(stderr, "WARN: Settings are not confirmed\n\r");
                } else {
                    
                    msg.type = MSG_COMPUTE;

                    // set parameters to message
                    msg.data.compute.re = computation.act_Re;
                    msg.data.compute.im = computation.act_Im;
                    msg.data.compute.n_re = computation.chunk_size;
                    msg.data.compute.n_im = computation.chunk_size;

                    msg.data.compute.cid = computation.cid;  
                    
                    fprintf(stderr, "INFO: Compute request\n\r");
                    
                }
                break;

// --------------------------------------------------------------------------------------------------
            case EV_COMPUTE_CPU:
                if (computation.computing) {
                    fprintf(stderr, 
                        "WARN: New computation requested but it is discarded due on ongoing computation\n\r");
                } else if(!computation.set_computing) {
                    fprintf(stderr, "WARN: Settings are not confirmed\n\r");
                } else {

                    // start computing on CPU
                    computation.computing = true;
                    fprintf(stderr, "INFO: Start compute on CPU\n\r");

                    pthread_mutex_lock(&mtx);
                    __ComputeFrameCPU__(data.img, interfece_parametrs.cR, interfece_parametrs.cI);
                    pthread_mutex_unlock(&mtx);
                    
                    computation.computing = false;

                    fprintf(stderr, "INFO: Done compute on CPU\n\r");
                }
                break;

// --------------------------------------------------------------------------------------------------
            case EV_ANIMATION_PC_START:
                if (computation.computing) {
                    fprintf(stderr, 
                        "WARN: New computation requested but it is discarded due on ongoing computation\n\r");
                } else if(!computation.set_computing) {
                    fprintf(stderr, "WARN: Settings are not confirmed\n\r");
                } else {

                    // start computing animation on CPU
                    fprintf(stderr, "INFO: Start compute animation on CPU\n\r");
                    computation.computing = true;
                    computation.ANIM_CPU = true;

                    // set start parameters if previous picture was complete
                    if (computation.complete_ANIM_CPU) {
                        computation.complete_ANIM_CPU = false;
                        frame = START_FRAME;
                        local_cR = interfece_parametrs.cR;
                        local_cI = interfece_parametrs.cI;
                    }
                    event ev = { .source = EV_KEYBOARD, .type = EV_ANIMATION_PC};
                    queue_push(ev);
                }
                break;

// --------------------------------------------------------------------------------------------------
            case EV_ANIMATION_PC:

                pthread_mutex_lock(&mtx);
                __ComputeFrameCPU__(data.img, local_cR, local_cI);
                pthread_mutex_unlock(&mtx);

                // get next frame 
                if (computation.ANIM_CPU && frame < interfece_parametrs.q_frames) {
                    frame++;
                    local_cR += interfece_parametrs.inR;
                    local_cI += interfece_parametrs.inI;
                    event ev = { .source = EV_KEYBOARD, .type = EV_ANIMATION_PC};
                    queue_push(ev);
                } else {
                    computation.ANIM_CPU = false;
                    computation.computing = false;
                    if (frame == interfece_parametrs.q_frames) computation.complete_ANIM_CPU = true;

                    if (computation.complete_ANIM_CPU) {
                        pthread_mutex_lock(&mtx);
                        __ComputeFrameCPU__(data.img, interfece_parametrs.cR, interfece_parametrs.cI);
                        pthread_mutex_unlock(&mtx);
                    }
                    fprintf(stderr, "INFO: End compute animation on CPU\n\r");
                }
                break;

// --------------------------------------------------------------------------------------------------
            case EV_SET_CHANGING_PARAM:
                switch (ev.data.param) {
                case STEP_IN ... B_IM:
                    interfece_parametrs.changing_param = ev.data.param;
                    fprintf(stderr, "INFO: Parameter which will be change %s\n\r", parametrs_name[ev.data.param]);
                    break;
                default:
                    fprintf(stderr, "WARN: Unkown parameters\n\r");
                    break;
                }
                break;

// --------------------------------------------------------------------------------------------------
            case EV_CHANGE_PARAM:
                inc = ev.data.param;

                switch (interfece_parametrs.changing_param) {
                case C_RE:
                    if (CanChangeDouble(interfece_parametrs.cR, inc))
                        interfece_parametrs.cR += inc * interfece_parametrs.step_in;
                    fprintf(stderr, "INFO: New value of %s: %f\n\r", parametrs_name[C_RE], interfece_parametrs.cR);
                    break;
                case C_IM:
                    if (CanChangeDouble(interfece_parametrs.cI, inc))
                        interfece_parametrs.cI += inc * interfece_parametrs.step_in;
                    fprintf(stderr, "INFO: New value of %s: %f\n\r", parametrs_name[C_IM], interfece_parametrs.cI);
                    break;
                case HEIGHT:
                    if (!(interfece_parametrs.height + inc < MIN_HEIGHT || 
                        interfece_parametrs.height + inc > MAX_HEIGHT)
                    )
                        interfece_parametrs.height += (uint16_t) inc * STEP_INC_SIZE;
                    fprintf(stderr, "INFO: New value of %s: %d\n\r", 
                        parametrs_name[HEIGHT], interfece_parametrs.height);
                    interfece_parametrs.confirm_new_screeen_size = false;
                    break;
                case WIDTH:
                    if (!(interfece_parametrs.width + inc < MIN_WIDTH || 
                        interfece_parametrs.width + inc > MAX_WIDTH)
                    )
                        interfece_parametrs.width += (uint16_t) inc * STEP_INC_SIZE ;
                    interfece_parametrs.confirm_new_screeen_size = false;
                    fprintf(stderr, "INFO: New value of %s: %d\n\r", parametrs_name[WIDTH], interfece_parametrs.width);
                    break;
                case N_IT:
                    if (!(interfece_parametrs.N_iter + inc < MIN_N_ITTER || 
                        interfece_parametrs.N_iter + inc > MAX_N_ITTER)
                    )
                        interfece_parametrs.N_iter += inc;
                    fprintf(stderr, "INFO: New value of %s: %d\n\r", parametrs_name[N_IT], interfece_parametrs.N_iter);
                    break;
                case A_RE:
                    if (CanChangeDouble(interfece_parametrs.aR, inc))
                        interfece_parametrs.aR += inc * interfece_parametrs.step_in;
                    fprintf(stderr, "INFO: New value of %s: %f\n\r", parametrs_name[A_RE], interfece_parametrs.aR);
                    break;
                case A_IM:
                    if (CanChangeDouble(interfece_parametrs.aI, inc))
                        interfece_parametrs.aI += inc * interfece_parametrs.step_in;
                    fprintf(stderr, "INFO: New value of %s: %f\n\r", parametrs_name[A_IM], interfece_parametrs.aI);
                    break;
                case B_RE:
                    if (CanChangeDouble(interfece_parametrs.bR, inc))
                        interfece_parametrs.bR += inc * interfece_parametrs.step_in;
                    fprintf(stderr, "INFO: New value of %s: %f\n\r", parametrs_name[B_RE], interfece_parametrs.bR);
                    break;
                case B_IM:
                    if (CanChangeDouble(interfece_parametrs.bI, inc))
                        interfece_parametrs.bI += inc * interfece_parametrs.step_in;
                    fprintf(stderr, "INFO: New value of %s: %f\n\r", parametrs_name[B_IM], interfece_parametrs.bI);
                    break;
                case STEP_IN:
                    if (!(interfece_parametrs.step_in + inc * min_step <= 0))
                        interfece_parametrs.step_in += inc * min_step;
                    fprintf(stderr, "INFO: New value of %s: %f\n\r", 
                        parametrs_name[STEP_IN], interfece_parametrs.step_in);
                    break;
                default:
                    fprintf(stderr, "WARN: Unkown parameter\n\r");
                    break;
                }
                computation.set_computing = false;
                break;

// --------------------------------------------------------------------------------------------------
            case EV_CANCEL_CHANGING:
                fprintf(stderr, "INFO: Parameters changing was canceled\n\r");
                break;

// --------------------------------------------------------------------------------------------------
            case EV_ABORT:
                if (computation.computing) {
                    if (computation.ANIM_CPU) {
                        fprintf(stderr, "INFO: Abort animation on PC\n\r");
                        computation.ANIM_CPU = false;
                        computation.computing = false;
                    } else {
                        msg.type = MSG_ABORT;
                        fprintf(stderr, "INFO: Abort request\n\r");
                    }
                } else {
                    fprintf(stderr, "WARN: Abort requested but it is not computing\n\r");
                }
                break;

// --------------------------------------------------------------------------------------------------
            case EV_RESET_CHUNK:
                if (computation.computing) {
                    fprintf(stderr, "WARN: Chunk reset request discarded, it is currently computing\n\r");
                } else {
                    fprintf(stderr, "INFO: Chunk reset request\n\r");
                    computation.cid = START_ID;
                    computation.set_computing = false;
                    fprintf(stderr, 
                    "INFO: Chunk reset confirm, chunk ID: %d\n\r", computation.cid);
                }
                break;

// --------------------------------------------------------------------------------------------------
            case EV_CHANGE_BAUD_RATE:
                if (computation.computing) {
                    fprintf(stderr, "WARN: Chunk reset request discarded, it is currently computing\n\r");
                } else {
                    prev_ev_pc = EV_CHANGE_BAUD_RATE;
                    msg.type = MSG_SET_BAUD_RATE;
                    fprintf(stderr, "INFO: BAUD RATE request \n\r");
                }
                break;

// --------------------------------------------------------------------------------------------------
            case EV_SAVE_IMAGE:
                pthread_mutex_lock(&mtx);
                __SavePNGImage__(data.img);
                pthread_mutex_unlock(&mtx);
                fprintf(stderr, "INFO: Image was saved\n\r");
                break;

// --------------------------------------------------------------------------------------------------
            case EV_REFRESH_BUFF_IMAGE:
                fprintf(stderr, "INFO: Buffer was clean\n\r");
                pthread_mutex_lock(&mtx);
                data.img = __SetBlackScreen__(data.img);
                pthread_mutex_unlock(&mtx);
                break;

// --------------------------------------------------------------------------------------------------
            case EV_QUIT:
                msg.type = MSG_ABORT;
                computation.computing = false;
                pthread_mutex_lock(&mtx);
                data.quit = true;
                pthread_cond_signal(&cond);
                pthread_mutex_unlock(&mtx);
                break;
            default:
                break;
            }
            if (msg.type != MSG_NBR) { // messge has been set
                if (!send_message(&data, &msg)) {
                    fprintf(stderr, "ERROR: send_message() does not send all bytes of the message!\n\r");
                }
            }
        // handle nucleo events
        } else if (ev.source == EV_NUCLEO) { 
            if (ev.type == EV_SERIAL) {
            message *msg = ev.data.msg;
            switch (msg->type) {

// --------------------------------------------------------------------------------------------------
                case MSG_STARTUP: {
                        char str[STARTUP_MSG_LEN+1];
                        for (int i = 0; i < STARTUP_MSG_LEN; ++i) {
                            str[i] = msg->data.startup.message[i];
                        }
                        str[STARTUP_MSG_LEN] = '\0';
                        fprintf(stderr, "INFO: Nucleo restarted - '%s'\n", str);
                    }
                    computation.set_computing = false;
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_VERSION:
                    if (msg->data.version.patch > 0) {
                        fprintf(stderr, 
                        "INFO: Nucleo firmware ver. %d.%d-p%d\n", 
                        msg->data.version.major, msg->data.version.minor, msg->data.version.patch);
                    } else {
                        fprintf(stderr, 
                        "INFO: Nucleo firmware ver. %d.%d\n", 
                        msg->data.version.major, msg->data.version.minor);
                    }
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_OK:
                    switch (prev_ev_pc) {
                    case EV_ABORT:
                        computation.computing = false;
                        pthread_mutex_lock(&mtx);
                        __RepaintScreen__(data.img);
                        pthread_mutex_unlock(&mtx);
                        fprintf(stderr, "INFO: Abort from Nucleo confirm, computing: %d\r\n", 
                                computation.computing);
                        break;
                    case EV_COMPUTE:
                        computation.computing = true;
                        fprintf(stderr, "INFO: Start computing from Nucleo confirm\r\n");
                        fprintf(stderr, 
                        "INFO: Nucleo reports the computation is start computing: %d\r\n", 
                        computation.computing);
                        break;
                    case EV_SET_COMPUTE:
                        fprintf(stderr, "INFO: Set new parameters from Nucleo confirm\r\n");
                        fprintf(stderr, "INFO: New parameters were set\n\r");
                        computation.set_computing = true;
                        break;
                    case EV_CHANGE_BAUD_RATE:
                        fprintf(stderr, "INFO: Set new baud rate from Nucleo confirm\r\n");
                        break;
                    default:
                        break;
                    }
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_ERROR:
                    if (prev_ev_pc == EV_ABORT) {
                        fprintf(stderr, "WARN: Abort from Nucleo NOT confirm\r\n");
                    } else if (prev_ev_pc == EV_COMPUTE) {
                        computation.computing = false;
                        fprintf(stderr, "WARN: Start computing from Nucleo NOT confirm\r\n");
                    } else if (prev_ev_pc == EV_SET_COMPUTE) {
                        fprintf(stderr, "INFO: Set new parameters from Nucleo NOT confirm\r\n");
                        fprintf(stderr, "INFO: New parameters were NOT set\n\r");
                    }
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_COMPUTE_DATA:

                    // write down recived data from message
                    computation.cid = msg->data.compute_data.cid;
                    computation.id_w_in_chunk = msg->data.compute_data.i_re;
                    computation.id_h_in_chunk = msg->data.compute_data.i_im;

                    // calculate value to each pixel
                    double t = (double)msg->data.compute_data.iter / interfece_parametrs.N_iter;
                    double R = (int8_t)(9*(1-t)*t*t*t*255);
                    double G = (int8_t)(15*(1-t)*(1-t)*t*t*255);
                    double B = (int8_t)(8.5*(1-t)*(1-t)*(1-t)*t*255);

                    // coordinates of corresponding pixel
                    int id_h = computation.idx_chunk_h * computation.chunk_size + computation.id_h_in_chunk;
                    int id_w = computation.idx_chunk_w * computation.chunk_size + computation.id_w_in_chunk;
                    
                    pthread_mutex_lock(&mtx);
                    if (id_w < data.img->width && id_h < data.img->height) {
                        data.img->pixels[id_h*data.img->width + id_w].R = R;
                        data.img->pixels[id_h*data.img->width + id_w].G = G;
                        data.img->pixels[id_h*data.img->width + id_w].B = B;
                    }
                    pthread_mutex_unlock(&mtx);
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_ABORT:
                    computation.computing = false;
                    fprintf(stderr, "INFO: Abort from Nucleo\r\n");
                    fprintf(stderr, 
                    "INFO: Nucleo reports the computation was abort, computing: %d\r\n", 
                        computation.computing);
                    pthread_mutex_lock(&mtx);
                    __RepaintScreen__(data.img);
                    pthread_mutex_unlock(&mtx);
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_DONE:
                    computation.computing = false;
                    fprintf(stderr, 
                    "INFO: Nucleo reports the computation is done, computing: %d\r\n", 
                    computation.computing);
                    
                    pthread_mutex_lock(&mtx);
                    __RepaintScreen__(data.img);
                    pthread_mutex_unlock(&mtx);
                    fprintf(stderr, 
                        "INFO: quantity_chunks: %d\n", computation.quantity_chunks);
                    
                    // do next step if it isn't last chunk
                    if (computation.cid < computation.quantity_chunks) {
                        computation.cid++;
                        fprintf(stderr, 
                        "INFO: Prepare next chunk of data cid: %d\n", computation.cid);
                        computation.idx_chunk_w++;
                        
                        // set width index to 0 if it was chunk in row
                        if (computation.idx_chunk_w == computation.chuck_in_row) {
                            computation.idx_chunk_h++;
                            computation.idx_chunk_w = START_INDX;
                        }
                        
                        // do next chunk if that was not last
                        if (computation.idx_chunk_h != computation.chuck_in_col) {
                            computation.act_Re = min(interfece_parametrs.aR, interfece_parametrs.bR);
                            computation.act_Im = max(interfece_parametrs.aI, interfece_parametrs.bI);

                            computation.act_Re += interfece_parametrs.inR * 
                                                    computation.idx_chunk_w * computation.chunk_size;
                            computation.act_Im -= interfece_parametrs.inR * 
                                                    computation.idx_chunk_h * computation.chunk_size;
                            event ev = { .source = EV_KEYBOARD, .type = EV_COMPUTE };
                            queue_push(ev);
                        }
                    }
                    break;

// --------------------------------------------------------------------------------------------------
                case MSG_SET_BAUD_RATE:
                    fprintf(stderr, "INFO: Change BAUD RATE from Nucleo confirm\r\n");

                    if (computation.fast_mode) {
                        pthread_mutex_lock(&mtx);
                        
                        // close old serial port
                        serial_close(data.fd);

                        // open new serial to comunicate with new BAUD RATE
                        data.fd = serial_open(serial);
                        pthread_mutex_unlock(&mtx);
                    } else {
                        pthread_mutex_lock(&mtx);

                        // close old serial port
                        serial_close(data.fd);

                        // open new serial to comunicate with new BAUD RATE
                        data.fd = serial_open_fast(serial);
                        pthread_mutex_unlock(&mtx);
                    }

                    // actualize state
                    computation.fast_mode = !computation.fast_mode;
                    if (computation.fast_mode)
                        fprintf(stderr, "INFO: BAUD RATE was changed to FAST mode\r\n");
                    else
                        fprintf(stderr, "INFO: BAUD RATE was changed to SLOW mode\r\n");
                    break;
                default:
                    break;
            }
            if (msg) {
                free(msg);
            }
            } else if (ev.type == EV_QUIT) {
                quit = true;
            } else {
            // ignore all other events
            }
        }
        
    } // end main quit

    // Ending part
    queue_cleanup(); // cleanup all events and free allocated memory for messages.
    for (int i = 0; i < NUM_THREADS; ++i) {
        fprintf(stderr, "INFO: Call join to the thread %s\n", threads_names[i]);
        int r = pthread_join(threads[i], NULL);
        fprintf(stderr, "INFO: Joining the thread %s has been %s\n", threads_names[i], (r == 0 ? "OK" : "FAIL"));
    }
    if (data.img != NULL) {__CloseImage__(); __FreeImage__(data.img); }
    
    serial_close(data.fd);
    call_termios(1); // restore terminal settings
    return EXIT_SUCCESS;
}

// - function -----------------------------------------------------------------
void __CalculateIncrement__() {
    double new_IRE = 0, new_IIM = 0;
    double delta_w = interfece_parametrs.bR - interfece_parametrs.aR;
    double delta_h = interfece_parametrs.bI - interfece_parametrs.aI;
    
    // delta always must be positive 
    if (delta_w < 0) delta_w = -delta_w;
    if (delta_h < 0) delta_h = -delta_h;

    // calculate new Increase step to each direction
    new_IRE = delta_w / interfece_parametrs.width;
    new_IIM = delta_h / interfece_parametrs.height;

    interfece_parametrs.inR = new_IRE;
    interfece_parametrs.inI = new_IIM;
}

// - function -----------------------------------------------------------------
void* input_thread(void* d) {
    data_t *data = (data_t*)d;
    bool end = false;
    int c;
    event ev = { .source = EV_KEYBOARD };

    // handing event from PC keyboard

    while (!end && (c = getchar())) {
        ev.type = EV_TYPE_NUM;
        ev.data.param = UNKNOWN;

        switch(c) {
            case 'g': // get version
                ev.type = EV_GET_VERSION;
                break;
            case 's':
                ev.type = EV_SET_COMPUTE;
                break;
            case '1':  
                ev.type = EV_COMPUTE;
                break;
            case 'a':
                ev.type = EV_ABORT;
                break;
            case 'r':
                ev.type = EV_RESET_CHUNK;
                break;
            case 'p':
                ev.type = EV_REFRESH;
                break;
            case 'c':
                ev.type = EV_COMPUTE_CPU;
                break;
            case 'h':
                ev.type = EV_SET_CHANGING_PARAM;
                print_choice();
                c = getchar();
                
                switch (c) {
                case '0' ... '9':
                    ev.data.param = (int)(c - '0');
                    break;
                default:
                    ev.type = EV_CANCEL_CHANGING;
                    break;
                }
                break;
            case '+':
            case '-':
                ev.type = EV_CHANGE_PARAM;
                ev.data.param = c == '+' ? 1 : -1;
                break;
            case 'm':
                print_menu();                   
                break;
            case 'i':
                ev.type = EV_REFRESH_BUFF_IMAGE;
                break;
            case 'f':
                ev.type = EV_ANIMATION_PC_START;
                break;
            case 'j':
                ev.type = EV_CHANGE_BAUD_RATE;
                break;
            case 't':
                ev.type = EV_SAVE_IMAGE;
                break;
            case 'q':
                end = true;
                ev.type = EV_ABORT;
                break;
            default: // discard all other keys
                break;
        }
        if (ev.type != EV_TYPE_NUM) { // new event 
            queue_push(ev);
        }
        pthread_mutex_lock(&mtx);
        end = end || data->quit; // check for quit
        pthread_mutex_unlock(&mtx);
    }
    ev.type = EV_QUIT;
    queue_push(ev);

    fprintf(stderr, "INFO: Exit input thead %p\n", (void*)pthread_self());
    return NULL;
}

// - function -----------------------------------------------------------------
void __ComputeFrameCPU__(Image *img, double cR, double cI) {

    for (int id_h = 0; id_h < img->height; id_h++) {
        for (int id_w = 0; id_w < img->width; id_w++) { 
            double new_R = interfece_parametrs.aR + interfece_parametrs.inR * id_w, // left --> right
                new_I = interfece_parametrs.aI + interfece_parametrs.inI * (img->height - id_h), // top --> bottom
            old_R, old_I;
            uint8_t counter = 0; 
            while (new_R*new_R + new_I*new_I < 4 && counter < interfece_parametrs.N_iter) {
                old_R = new_R;
                old_I = new_I;
                new_R = old_R*old_R - old_I*old_I + cR;
                new_I = 2*old_R*old_I + cI;
                counter++;
            }
            double t = (double) counter / (double) interfece_parametrs.N_iter;
            
            img->pixels[id_h*img->width + id_w].R = (int8_t)(9*(1-t)*t*t*t*255);
            img->pixels[id_h*img->width + id_w].G = (int8_t)(15*(1-t)*(1-t)*t*t*255);
            img->pixels[id_h*img->width + id_w].B = (int8_t)(8.5*(1-t)*(1-t)*(1-t)*t*255);
        }
    }
    __RepaintScreen__(img);
}

// - function -----------------------------------------------------------------
uint8_t __CalculateChunckSize__() {
    float picture_size = interfece_parametrs.height * interfece_parametrs.width;
    float size_chunk = MIN_SIZE_CHUNK;
    while (picture_size > (size_chunk * size_chunk * MAX_Q_CHUNKS)) size_chunk += (5 * EXTRA_SPACE);

    return (uint8_t)(size_chunk);
}

// - function -----------------------------------------------------------------
void* serial_rx_thread(void* d) { // read bytes from the serial and puts the parsed message to the queue
    data_t *data = (data_t*)d;
    uint8_t msg_buf[sizeof(message)]; // maximal buffer for all possible messages defined in messages.h
    event ev = { .source = EV_NUCLEO, .type = EV_SERIAL, .data.msg = NULL };

    message *rec_msg; // received message

    int len_msg = 0; // length message 
    bool known_type = false; // known data type
    bool correct_read = true; // correct reading data from serial port

    bool end = false;
    unsigned char c; // buffer symbol

    pthread_mutex_lock(&mtx);
    int fd = data->fd;
    pthread_mutex_unlock(&mtx);

    while (serial_getc_timeout(fd, SERIAL_READ_TIMOUT_MS, &c) > 0) {}; // discard garbage

    while (!end) {
        msg_buf[0] = EMPTY_SYMBOL; 
        known_type = false; 
        correct_read = true;

        int r = serial_getc_timeout(fd, SERIAL_READ_TIMOUT_MS, &c);
        if (r > 0) { // character has been read
            // TODO you may put your code here

            if (get_message_size((uint8_t)c, &len_msg)) known_type = true;

            if (known_type) {
            msg_buf[0] = (uint8_t)c;
            for (int ind_buf = 1; ind_buf < len_msg; ind_buf++) {
                r = serial_getc_timeout(fd, SERIAL_READ_TIMOUT_MS, &c);
                if (r > 0) {
                    msg_buf[ind_buf] = (uint8_t)c; correct_read = true;
                } else if (r == 0) {
                    correct_read = false;
                    break;
                } else {
                    fprintf(stderr, "ERROR: Cannot receive data from the serial port\r\n");
                    end = true;
                    break;
                }
            }

            if (correct_read) {
                rec_msg = (message*)malloc(len_msg);
                if (rec_msg == NULL) {
                    fprintf(stderr, "FATAL ERROR: Not enough memory to allocate recived message!\n\r");
                    exit(200);
                }
                if (parse_message_buf(msg_buf, len_msg, rec_msg)) {
                    ev.data.msg = rec_msg;
                    queue_push(ev);
                } else 
                    fprintf(stderr, "ERROR: Cannot parse message type %d\n\r", msg_buf[0]);
            } else {
                fprintf(stderr, "WARN: the packet has not been received discard what has been read\n\r");
            }

            } else {
            fprintf(stderr, 
            "ERROR: Unknown message type has been received 0x%x\n - '%c'\r", c, c);
            while (serial_getc_timeout(fd, SERIAL_READ_TIMOUT_MS, &c) > 0) {}; // discard garbage
            }

        } else if (r == 0) { //read but nothing has been received
            // TODO you may put your code here
            pthread_mutex_lock(&mtx);
            end = end || data->quit;
            pthread_mutex_unlock(&mtx);
        } else {
            fprintf(stderr, "ERROR: Cannot receive data from the serial port\n\r");
            end = true;
        }
        
    }

    ev.type = EV_QUIT;
    queue_push(ev);
    fprintf(stderr, "INFO: Exit serial_rx_thread %p\n", (void*)pthread_self());
    return NULL;
}

// - function -----------------------------------------------------------------
bool send_message(data_t *data, message *msg) {

    int len_send_msg;
    uint8_t buf_msg[MESSAGE_SIZE];

    pthread_mutex_lock(&mtx);
    int fd = data->fd;
    pthread_mutex_unlock(&mtx);

    if (!fill_message_buf(msg, buf_msg, MESSAGE_SIZE, &len_send_msg)) return false;

    for (int id_sym = 0; id_sym < len_send_msg; id_sym++) {

        if (!serial_putc(fd, buf_msg[id_sym])) return false;
    }

    return true;
}

// - function -----------------------------------------------------------------
void call_termios(int reset) {
   static struct termios tio, tioOld;
   tcgetattr(STDIN_FILENO, &tio);
   if (reset) {
      tcsetattr(STDIN_FILENO, TCSANOW, &tioOld);
   } else {
      tioOld = tio; //backup 
      cfmakeraw(&tio);
      tio.c_oflag |= OPOST;
      tcsetattr(STDIN_FILENO, TCSANOW, &tio);
   }
}

// - function -----------------------------------------------------------------
void print_choice() {
    fprintf(stderr, "INFO: Selected changing paramtrs\n\r");
    fprintf(stderr, "INFO: What parameters would you want to change?\n\r");
    fprintf(stderr, "---------- INFORAMTION ----------||\n\r");
    fprintf(stderr, "1. C real: %.2f                 \n\r", interfece_parametrs.cR);
    fprintf(stderr, "2. C Imaginary: %.2f            \n\r", interfece_parametrs.cI);
    fprintf(stderr, "3. Height: %4d                  \n\r", interfece_parametrs.height);
    fprintf(stderr, "4. Width: %4d                   \n\r", interfece_parametrs.width);
    fprintf(stderr, "5. N iterations: %3d            \n\r", interfece_parametrs.N_iter);
    fprintf(stderr, "6. A real: %.2f                 \n\r", interfece_parametrs.aR);
    fprintf(stderr, "7. A Imaginary: %.2f            \n\r", interfece_parametrs.aI);
    fprintf(stderr, "8. B real: %.2f                 \n\r", interfece_parametrs.bR);
    fprintf(stderr, "9. B Imaginary: %.2f            \n\r", interfece_parametrs.bI);
    fprintf(stderr, "0. Step increas: %.2f           \n\r", interfece_parametrs.step_in);
    fprintf(stderr, "SOME OTHERS to cancel choice    \n\r");
    fprintf(stderr, "_________________________________||\n\r");
}

// - function -----------------------------------------------------------------
void print_menu() {
    fprintf(stderr, "---------- INFORAMTION ----------||\n\r");
    fprintf(stderr, "g - get version                  \n\r");
    fprintf(stderr, "s - select computing parameters  \n\r");
    fprintf(stderr, "1 - start computing on Nucleo    \n\r");
    fprintf(stderr, "a - abort computing              \n\r");
    fprintf(stderr, "r - reset computing chank        \n\r");
    fprintf(stderr, "i - cleanup image buffer         \n\r");
    fprintf(stderr, "p - picture actual image buffer  \n\r");
    fprintf(stderr, "c - compute on PC                \n\r");
    fprintf(stderr, "f - compute animation on PC      \n\r");
    fprintf(stderr, "h - change parameters            \n\r");
    fprintf(stderr, "j - change BAUD RATE             \n\r");
    fprintf(stderr, "t - save picture                 \n\r");
    fprintf(stderr, "q - exit                         \n\r");
    fprintf(stderr, "_________________________________||\n\r");
}

// - function -----------------------------------------------------------------
bool CanChangeDouble(double changing, int inc) {

    double calcul = changing + inc * interfece_parametrs.step_in;

    return -MAX_RE_IM_VALUE < calcul && calcul < MAX_RE_IM_VALUE;

}
/* end of sem-main.c */
