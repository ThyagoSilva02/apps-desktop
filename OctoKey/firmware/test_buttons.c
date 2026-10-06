#include "buttons.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    struct button_state s;
    button_init(&s,false,0);
    assert(!button_update(&s,true,10));
    assert(!button_update(&s,false,12));
    assert(!button_update(&s,true,15));
    assert(!button_update(&s,true,39));
    assert(button_update(&s,true,40));
    for(uint32_t t=41;t<1000;++t) assert(!button_update(&s,true,t));
    assert(!button_update(&s,false,1000));
    assert(!button_update(&s,false,1025));
    assert(!button_update(&s,true,1030));
    assert(button_update(&s,true,1055));
    button_init(&s,true,0);
    assert(!button_update(&s,true,100));
    assert(!button_update(&s,false,101));
    assert(!button_update(&s,false,126));
    assert(!button_update(&s,true,127));
    assert(button_update(&s,true,152));
    button_init(&s,false,UINT32_MAX-50);
    assert(!button_update(&s,true,UINT32_MAX-10));
    assert(!button_update(&s,true,13));
    assert(button_update(&s,true,14));
    struct button_state all[8];
    for(unsigned i=0;i<8;++i) { button_init(&all[i],false,0); assert(!button_update(&all[i],true,1)); }
    for(unsigned i=0;i<8;++i) assert(button_update(&all[i],true,26));
    puts("Button debounce: bounce, hold, release, boot-held, wraparound and eight simultaneous presses passed.");
}
