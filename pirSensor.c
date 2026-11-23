#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"

#define PIR_GPIO    2
#define LED_GPIO    25
#define I2C_SDA     4
#define I2C_SCL     5
#define LCD_ADDR    0x27

#define BTN_GPIO    14
#define SERVO_GPIO  15
#define BRIGHT_GPIO 16
#define POT_GPIO    26
#define BUZZER_GPIO 12      // drives the S8050 BASE (through 1k), not the buzzer

#define FACT_MS           4000
#define WELCOME_MS        2500
#define STANDBY_MS        10000
#define STANDBY_DIM_PCT   10
#define STANDBY_BACKLIGHT 0

// ---- Implemented in pir_read.S ----
extern uint32_t pir_read(void);
extern uint32_t pir_poll_and_welcome(void);

// ================= Buzzer (active buzzer via S8050) =================
// GP12 HIGH -> base current -> transistor saturates -> buzzer conducts.
// An active buzzer has its own oscillator, so a plain HIGH makes the tone;
// the greeting is a pattern of beeps. Durations in ms: on, off, on, off...
static const uint16_t GREETING[] = { 120, 80, 120, 80, 300 };
#define GREETING_LEN (sizeof(GREETING) / sizeof(GREETING[0]))

static bool     buz_active = false;
static size_t   buz_step   = 0;
static uint32_t buz_t      = 0;

static void buzzer_init(void) {
    gpio_init(BUZZER_GPIO);
    gpio_set_dir(BUZZER_GPIO, GPIO_OUT);
    gpio_put(BUZZER_GPIO, 0);             // transistor off at start-up
}

static void buzzer_stop(void) {
    buz_active = false;
    gpio_put(BUZZER_GPIO, 0);
}

static void buzzer_greet(uint32_t now) {
    buz_active = true;
    buz_step = 0;
    buz_t = now;
    gpio_put(BUZZER_GPIO, 1);             // even steps = tone on
}

// Non-blocking: advances the beep pattern so the PIR keeps being polled.
static void buzzer_update(uint32_t now) {
    if (!buz_active) return;
    if (now - buz_t < GREETING[buz_step]) return;
    buz_step++;
    if (buz_step >= GREETING_LEN) { buzzer_stop(); return; }
    buz_t = now;
    gpio_put(BUZZER_GPIO, (buz_step % 2) == 0);   // even = on, odd = off
}

// ================= LCD driver =================
#define LCD_RS 0x01
#define LCD_EN 0x04
#define LCD_BL 0x08

static bool lcd_bl_on = true;

static void lcd_i2c(uint8_t v) {
    i2c_write_blocking(i2c0, LCD_ADDR, &v, 1, false);
}

static void lcd_nibble(uint8_t nib, uint8_t rs) {
    uint8_t d = (nib << 4) | rs | (lcd_bl_on ? LCD_BL : 0);
    lcd_i2c(d | LCD_EN);
    sleep_us(2);
    lcd_i2c(d & ~LCD_EN);
    sleep_us(60);
}

static void lcd_send(uint8_t v, uint8_t rs) {
    lcd_nibble(v >> 4, rs);
    lcd_nibble(v & 0x0F, rs);
}

static void lcd_set_backlight(bool on) {
    lcd_bl_on = on;
    lcd_i2c(on ? LCD_BL : 0);
}

static void lcd_init(void) {
    sleep_ms(50);
    lcd_nibble(0x3, 0); sleep_ms(5);
    lcd_nibble(0x3, 0); sleep_us(150);
    lcd_nibble(0x3, 0);
    lcd_nibble(0x2, 0);
    lcd_send(0x28, 0);
    lcd_send(0x0C, 0);
    lcd_send(0x06, 0);
    lcd_send(0x01, 0); sleep_ms(2);
}

static void lcd_row(uint8_t row, const char *text) {
    lcd_send(row ? 0xC0 : 0x80, 0);
    size_t len = strlen(text);
    for (size_t i = 0; i < 16; i++)
        lcd_send(i < len ? text[i] : ' ', LCD_RS);
}

static void lcd_show(const char *l1, const char *l2) {
    lcd_row(0, l1);
    lcd_row(1, l2);
}

// ================= Timeline data =================
typedef struct {
    uint8_t     era;
    const char *line1;
    const char *line2;
} fact_t;

static const fact_t facts[] = {
    {0, "Gen 0: 1642-1945", "Mechanical"},
    {0, "Pascal, 1642",     "Mechanical adder"},
    {0, "Babbage 1830s",    "Analytical Eng."},

    {1, "Gen 1: 1945-1955", "Vacuum tubes"},
    {1, "ENIAC 1946",       "~18,000 tubes"},
    {1, "IAS machine",      "Von Neumann arch"},

    {2, "Gen 2: 1955-1965", "Transistors"},
    {2, "Bell Labs 1948",   "Transistor born"},
    {2, "DEC PDP-1 1961",   "Minicomputer era"},

    {3, "Gen 3: 1965-1980", "Integrated ckts"},
    {3, "IBM System/360",   "Family, 1964"},

    {4, "Gen 4: 1980-?",    "VLSI"},
    {4, "Millions of",      "transistors/chip"},
    {4, "IBM PC 1981",      "PC era begins"},

    {5, "Gen 5: Low-power", "& invisible"},
    {5, "Computers hidden", "in everyday life"},
    {5, "Smartphones &",    "wearables"},
};
#define NUM_FACTS (sizeof(facts) / sizeof(facts[0]))

// ================= Servo (SG90) =================
#define ERA_ANGLE(e)  ((e) * 36)

static int servo_angle  = 0;
static int servo_target = 0;

static void servo_write(int angle) {
    uint32_t us = 500 + (uint32_t)angle * 2000 / 180;
    pwm_set_gpio_level(SERVO_GPIO, us);
}

static void servo_init(void) {
    gpio_set_function(SERVO_GPIO, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(SERVO_GPIO);
    pwm_set_clkdiv(slice, 125.0f);
    pwm_set_wrap(slice, 19999);
    pwm_set_enabled(slice, true);
    servo_write(0);
}

static void servo_step(void) {
    if (servo_angle == servo_target) return;
    if (servo_angle < servo_target) {
        servo_angle += 3;
        if (servo_angle > servo_target) servo_angle = servo_target;
    } else {
        servo_angle -= 3;
        if (servo_angle < servo_target) servo_angle = servo_target;
    }
    servo_write(servo_angle);
}

// ================= Brightness =================
static uint8_t dim_pct = 100;

static void brightness_init(void) {
    adc_init();
    adc_gpio_init(POT_GPIO);
    adc_select_input(0);

    gpio_set_function(BRIGHT_GPIO, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(BRIGHT_GPIO);
    pwm_set_wrap(slice, 4095);
    pwm_set_enabled(slice, true);
}

static void brightness_update(void) {
    uint32_t raw  = adc_read();
    uint32_t duty = (raw * raw) / 4095;
    duty = duty * dim_pct / 100;
    pwm_set_gpio_level(BRIGHT_GPIO, duty);
}

// ================= Button =================
static bool button_pressed(uint32_t now) {
    static bool     last = true;
    static uint32_t t_change = 0;
    bool cur = gpio_get(BTN_GPIO);
    if (cur != last && now - t_change >= 30) {
        last = cur;
        t_change = now;
        if (!cur) return true;
    }
    return false;
}

// ================= Exhibit state =================
typedef enum { STANDBY, WELCOME, FACTS } state_t;

static volatile bool visitor_flag = false;
static state_t  state         = STANDBY;
static uint32_t t_state       = 0;
static uint32_t last_activity = 0;
static size_t   fact          = 0;

// Called FROM the assembly routine on a PIR rising edge.
void welcome_sequence(void) {
    visitor_flag = true;
}

static void show_fact(size_t i) {
    lcd_show(facts[i].line1, facts[i].line2);
    servo_target = ERA_ANGLE(facts[i].era);
    printf("Era %u: %s %s (servo %d deg)\n",
           facts[i].era, facts[i].line1, facts[i].line2, servo_target);
}

static size_t next_era(size_t i) {
    uint8_t e = facts[i].era;
    do { i = (i + 1) % NUM_FACTS; } while (facts[i].era == e);
    return i;
}

static void enter_standby(void) {
    state = STANDBY;
    buzzer_stop();
    lcd_show("Time Capsule", "Wave to begin...");
    lcd_set_backlight(STANDBY_BACKLIGHT);
    dim_pct = STANDBY_DIM_PCT;
    servo_target = 0;
    gpio_put(LED_GPIO, 0);
    printf("Standby - dimmed, waiting for a visitor...\n");
}

static void wake_up(uint32_t now) {
    state = WELCOME;
    t_state = now;
    last_activity = now;
    fact = 0;
    dim_pct = 100;
    lcd_set_backlight(true);
    servo_target = ERA_ANGLE(0);
    gpio_put(LED_GPIO, 1);
    lcd_show("Welcome to the", "Time Capsule!");
    buzzer_greet(now);                    // greeting tone via the S8050 stage
    printf("Welcome to the Time Capsule Exhibit!\n");
    printf("Buzzer greeting (GP12 -> S8050 base)\n");
    printf("Starting timeline...\n");
}

int main() {
    stdio_init_all();

    buzzer_init();                        // first, so the buzzer is never left floating

    gpio_init(LED_GPIO); gpio_set_dir(LED_GPIO, GPIO_OUT);
    gpio_init(PIR_GPIO); gpio_set_dir(PIR_GPIO, GPIO_IN);

    gpio_init(BTN_GPIO); gpio_set_dir(BTN_GPIO, GPIO_IN);
    gpio_pull_up(BTN_GPIO);

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    servo_init();
    brightness_init();

    lcd_init();
    lcd_show("Time Capsule", "Wave to begin...");
    sleep_ms(2000);
    printf("Ready - waiting for a visitor...\n");
    enter_standby();

    while (true) {
        pir_poll_and_welcome();
        uint32_t now = to_ms_since_boot(get_absolute_time());

        bool pir_high = pir_read();
        brightness_update();
        bool skip = button_pressed(now);

        if (pir_high) last_activity = now;

        if (visitor_flag) {
            visitor_flag = false;
            if (state == STANDBY) wake_up(now);
        }

        if (skip && state != STANDBY) {
            last_activity = now;
            if (state == WELCOME) {
                state = FACTS;
                t_state = now;
                show_fact(fact);
            } else {
                fact = next_era(fact);
                t_state = now;
                show_fact(fact);
            }
        }

        switch (state) {
        case WELCOME:
            if (now - t_state >= WELCOME_MS) {
                state = FACTS;
                t_state = now;
                show_fact(fact);
            }
            break;
        case FACTS:
            if (now - t_state >= FACT_MS) {
                fact = (fact + 1) % NUM_FACTS;
                t_state = now;
                show_fact(fact);
            }
            break;
        default:
            break;
        }

        if (state != STANDBY && now - last_activity >= STANDBY_MS) {
            enter_standby();
        }

        buzzer_update(now);
        servo_step();
        sleep_ms(20);
    }
}