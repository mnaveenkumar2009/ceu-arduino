#if ARDUINO_ARCH_AVR
    #define CEU_STACK_MAX  500
#elif ARDUINO_ARCH_SAMD
    #define CEU_STACK_MAX 1000
#else
    #error "Unsupported Platform!"
#endif

#define ceu_sys_assert(v,msg)                              \
    if (!(v)) {                                            \
        ceu_callback_num_ptr(CEU_CALLBACK_ABORT, 0, NULL, CEU_TRACE_null); \
    }

#define _DELAY(ms)                      \
    {                                   \
        int i;                          \
        for (i=0; i<ms; i++) {          \
            delayMicroseconds(1000);    \
        }                               \
    }

#include "_ceu_app.c.h"

#ifdef CEU_FEATURES_ISR
    #include "wiring_private.h"

    #ifndef _VECTOR_SIZE
        #ifdef ARDUINO_ARCH_AVR
            #define _VECTOR_SIZE 26     /* defined as _VECTORS_SIZE for ATmega328p in "iom328p.h" */
        #elif ARDUINO_ARCH_SAMD
            #define _VECTOR_SIZE PERIPH_COUNT_IRQn
        #else
            #error "Unsupported Platform!"
        #endif
    #endif

    static tceu_isr isrs[_VECTOR_SIZE];
    #include "isrs.c.h"
#else
    #ifdef CEU_FEATURES_ISR_SLEEP
        #error "Invalid option!"
    #endif
    #ifndef CEU_ARDUINO_SERIAL_SPEED
        #define CEU_ARDUINO_SERIAL_SPEED 9600
    #endif
#endif

#ifndef CEU_FEATURES_ISR
typedef struct tceu_arduino {
    u32 old;
    u32 pins_bits;
} tceu_arduino;

static tceu_arduino CEU_ARDUINO;
#endif

static int ceu_callback_arduino (int cmd, tceu_callback_val p1, tceu_callback_val p2)
{
    int is_handled = 1;

    switch (cmd) {
        case CEU_CALLBACK_START:
#ifndef CEU_FEATURES_ISR
            CEU_ARDUINO.old       = micros();
            CEU_ARDUINO.pins_bits = 0;
#endif
            break;

        case CEU_CALLBACK_ABORT: {
            noInterrupts();
            pinMode(13, OUTPUT);
            for (;;) {
                digitalWrite(13, !digitalRead(13));
                for (int i=0; i<50; i++) {
                    delayMicroseconds(1000);    /* max is 16383 */
                }
            }
            interrupts();
        }

#if 0
        case CEU_CALLBACK_LOG: {
            switch (p1.num) {
                case 0:
                    Serial.print((char*)p2.ptr);
                    break;
                case 1:
                    Serial.print(p2.num,HEX);
                    break;
                case 2:
                    Serial.print(p2.num);
                    break;
            }
            break;
        }
#endif

#ifdef CEU_FEATURES_ISR
        case CEU_CALLBACK_ISR_ENABLE: {
            if (p1.num == 1) {
                interrupts();
            } else {
                noInterrupts();
            }
            break;
        }
        case CEU_CALLBACK_ISR_ATTACH: {
            tceu_isr* isr = (tceu_isr*) p1.ptr;
            int* args = (int*) p2.ptr;
            if (args[0] < EXTERNAL_NUM_INTERRUPTS) {
                attachInterrupt(args[0], (void(*)())(isr->fun), args[1]);    /* TODO: no mem */
            } else {
                isrs[args[0]] = *isr;
            }
            break;
        }
        case CEU_CALLBACK_ISR_DETACH: {
            int* args = (int*)p2.ptr;
            if (args[0] < EXTERNAL_NUM_INTERRUPTS) {
                detachInterrupt(args[0]);
            } else {
                isrs[args[0]].fun = NULL;
            }
            break;
        }
        case CEU_CALLBACK_ISR_EMIT: {
            //ceu_dbg_assert(isrs[p1.num].evt.id == CEU_INPUT__NONE);
            isrs[p1.num].evt = *((tceu_evt_id_params*) p2.ptr);
            break;
        }
        case CEU_CALLBACK_WCLOCK_DT:
            ceu_callback_ret.num = CEU_WCLOCK_INACTIVE;
            break;
#else
        case CEU_CALLBACK_WCLOCK_DT:
            ceu_callback_ret.num = ceu_arduino_dt();
            break;
#endif

        case CEU_CALLBACK_OUTPUT:
            switch (p1.num) {
                #include "pins_outputs.c.h"
                default:
                    is_handled = 0;
            }
            break;

        default:
            is_handled = 0;
    }
    return is_handled;
}

#ifndef CEU_FEATURES_ISR
s32 ceu_arduino_dt (void) {
    u32 now = micros();
    u32 dt  = (now - CEU_ARDUINO.old);  // no problems with overflow
    CEU_ARDUINO.old = now;
    return dt;
}
#endif

void setup () {
    #include "pins_modes.c.h"

#ifdef _CEU_INPUT_SERIAL_RECEIVE_BYTE_
    Serial.begin(CEU_ARDUINO_SERIAL_SPEED);
#endif

#ifdef CEU_FEATURES_ISR
    memset((void*)&isrs, 0, sizeof(isrs));
pinMode(12, OUTPUT);
pinMode(11, OUTPUT);
#endif

    tceu_callback cb = { &ceu_callback_arduino, NULL };
    ceu_start(&cb, 0, NULL);

    while (!CEU_APP.end_ok)
    {
#ifdef CEU_FEATURES_ISR
        ceu_input(CEU_INPUT__ASYNC, NULL);
        {
            tceu_evt_id_params evt;
            int i;
            noInterrupts();
            for (i=0; i<_VECTOR_SIZE; i++) {
                tceu_isr* isr = &isrs[i];
                if (isr->evt.id != CEU_INPUT__NONE) {
                    evt = isr->evt;
                    isr->evt.id = CEU_INPUT__NONE;
                    interrupts();
                    ceu_input(evt.id, evt.params);
                    goto _CEU_ARDUINO_AWAKE_;
                }
            }
#ifdef CEU_FEATURES_ISR_SLEEP
            if (!CEU_APP.async_pending) {
digitalWrite(12,0);
                ceu_pm_sleep();
            }
#endif
            interrupts();
_CEU_ARDUINO_AWAKE_:;
digitalWrite(12,1);
_DELAY(25);
        }

#else // !CEU_FEATURES_ISR

        ceu_input(CEU_INPUT__ASYNC, NULL);
        #include "pins_inputs.c.h"
#ifdef _CEU_INPUT_SERIAL_RECEIVE_BYTE_
        if (Serial.available()) {
            byte c = Serial.read();
            ceu_input(CEU_INPUT_SERIAL_RECEIVE_BYTE, &c);
        }
#endif

#endif
    }
    ceu_stop();
    ceu_sys_assert(0, "bug found");
    while (1);
}

void loop () {}
