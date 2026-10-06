#include <stdint.h>
extern uint32_t _stack,_data_start,_data_end,_data_load,_bss_start,_bss_end;
int main(void); void sys_tick_handler(void);
static void fault(void) { while(1) {} }
void reset_handler(void) {
    volatile uint32_t *vtor=(uint32_t *)0xe000ed08; *vtor=0x08000000;
    uint32_t *src=&_data_load;
    for(uint32_t *p=&_data_start;p<&_data_end;) *p++=*src++;
    for(uint32_t *p=&_bss_start;p<&_bss_end;) *p++=0;
    main(); fault();
}
typedef void (*isr)(void);
__attribute__((section(".vectors"),used)) const isr vectors[16+60]={
    [0]=(isr)&_stack,[1]=reset_handler,[2]=fault,[3]=fault,[4]=fault,
    [5]=fault,[6]=fault,[11]=fault,[12]=fault,[14]=fault,[15]=sys_tick_handler
};
