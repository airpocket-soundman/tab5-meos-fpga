#pragma once
static void esp_rom_gpio_connect_in_signal(unsigned pin,unsigned sig,bool invert){assert(pin==63);assert(sig==13 && !invert);}
static void esp_rom_gpio_connect_out_signal(unsigned pin,unsigned sig,bool a,bool b){(void)pin;(void)sig;(void)a;(void)b;output_routes++;}
