#ifndef __GPIO_H
#define __GPIO_H

int gpio_setup();
void gpio_shutdown();

#define inl(f) inline f __attribute__((always_inline))

uint32_t gpio_word();
uint32_t gpio_set_input(uint32_t pin);
uint32_t gpio_set_output(uint32_t pin);
uint32_t gpio_alternate_function(uint32_t pin, uint32_t alternate);
uint32_t gpio_set(uint32_t pins);
uint32_t gpio_clear(uint32_t pins);

#endif
