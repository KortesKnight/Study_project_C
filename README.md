# Semester project from C language courses


## Project task
The main goal of the whole project is calculating fractal by communication between PC and Nucleo board`(NUCLEO-F042K6)`by USB port. All details are presented on [this web page](https://cw.fel.cvut.cz/wiki/courses/b3b36prg/semestral-project/start).

> In this project were used several templates from other homeworks and several template libraries from web of task such as:
```
event_queue.h
message.h
prg_serial_nonblock.h
xwin_sdl.h
```

## Starting work
In order to start using this program you need to connect your Nucleo board to USB port of your PC. Then download bin file`prgsem-v1_NUCLEO_F446RE.bin`on Nucleo board, or compile bin file by yourself using `prgsem_Nucleo.cpp` and these necessary libraries `message.h and message.c`.

## Working on different systems
### Linux

> The project was tested only on Ubuntu 20.04 system, which means program may behave unusually on other versions of Linux. 

1. Compile project.
2. Execute actions from previous page `(Starting work)`.
3. Launch program by a console command `./prgsem`.
4. If the launch failed, try to found out which port is Nucleo connected to and launch program as `./prgsem your_conacting_port`again.

### Windows

>1. The project was tested only on Windows 10 home system, which means program may behave unusually on other versions of Windows. 
>2. Before we will start use this program you should follow [this instructions by fredericseiler](https://github.com/microsoft/WSL/issues/4793).

1. Compile project.
2. Execute actions from previous page `(Starting work)`.
3. Launch program by a console command `./prgsem`.
4. If the launch failed, try to found out which port is Nucleo connected to and launch program as `./prgsem your_conacting_port`again.

## From PC side
The program has cmd interface and works with user keyboard. User can interact with Nucleo board by sending and receiving defined messages. Following parameters of calculating fractals also can be changed:
- Real and imaginary value
- Height and width of output display
- Amount of iterations into the depths of calculations
- Step of changing parameters

User can able to do several basic actions:
- Getting Nucleo program version
- Selecting computing parameters
- Starting computing on Nucleo
- Aborting computing
- Reseting computing data chunks
- Cleanup image buffer
- Picturing actual image buffer
- Computing fractals on PC
- Computing animation of fractals on PC
- Changing all parameters
- Changing BAUD RATE
- Saving picture as PNG
- Exiting

## From Nucleo side
Nucleo board is able to receive and send messages, but board's main target is to calculate received data chunk and send it back.

Nucleo can calculate only a single chunk at the same time. You still have opportunity to interrupt processing by signal from PC or physical button on Nucleo itself. 

## Basic working principle
The whole program has global queue which contain all coming events from all sources, so it will not make a mistake in the order of actions. The same queue is realized on Nucleo board too.

Way of calculating fractals and communication protocol is presented on [this web page](https://cw.fel.cvut.cz/wiki/courses/b3b36prg/semestral-project/start).
