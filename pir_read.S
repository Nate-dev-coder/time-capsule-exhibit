@ pir_read.S - HC-SR501 PIR read + welcome trigger (RP2040, Cortex-M0+, Thumb)
@ PIR OUT -> GP2. GP2 must be set as a SIO input by the C code (gpio_init).

        .syntax unified
        .cpu    cortex-m0plus
        .thumb

        .equ    SIO_BASE,    0xD0000000     @ single-cycle IO block
        .equ    GPIO_IN_OFF, 0x04           @ GPIO_IN register offset
        .equ    PIR_PIN,     2              @ GP2

@ ---------------------------------------------------------------
@ uint32_t pir_read(void)
@ Returns 1 if the PIR output is HIGH, else 0.
@ ---------------------------------------------------------------
        .section .text.pir_read, "ax"
        .align  2
        .global pir_read
        .thumb_func
pir_read:
        ldr     r1, =SIO_BASE               @ r1 = 0xD0000000
        ldr     r0, [r1, #GPIO_IN_OFF]      @ r0 = all GPIO input levels
        lsrs    r0, r0, #PIR_PIN            @ move bit 2 down to bit 0
        movs    r1, #1
        ands    r0, r1                      @ keep only bit 0 -> 0 or 1
        bx      lr
        .ltorg

@ ---------------------------------------------------------------
@ uint32_t pir_poll_and_welcome(void)
@ Reads the PIR. On a LOW->HIGH change (new visitor) it calls the
@ C function welcome_sequence() once.
@ Returns 1 if the welcome fired, else 0.
@ ---------------------------------------------------------------
        .section .text.pir_poll_and_welcome, "ax"
        .align  2
        .global pir_poll_and_welcome
        .thumb_func
pir_poll_and_welcome:
        push    {r4, lr}                    @ keeps stack 8-byte aligned
        bl      pir_read                    @ r0 = current PIR level
        ldr     r1, =pir_prev
        ldrb    r2, [r1]                    @ r2 = previous level
        strb    r0, [r1]                    @ remember current level
        cmp     r0, #0
        beq     no_trigger                  @ PIR low -> nothing to do
        cmp     r2, #0
        bne     no_trigger                  @ was already high -> same visitor
        bl      welcome_sequence            @ rising edge -> welcome!
        movs    r0, #1
        pop     {r4, pc}
no_trigger:
        movs    r0, #0
        pop     {r4, pc}
        .ltorg

        .section .bss
        .align  2
pir_prev:
        .space  4