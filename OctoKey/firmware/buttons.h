#pragma once
#include <stdint.h>
#include <stdbool.h>
struct button_state { bool stable, candidate; uint32_t since; };
static inline void button_init(struct button_state *s,bool pressed,uint32_t now) {
    s->stable=pressed; s->candidate=pressed; s->since=now;
}
static inline bool button_update(struct button_state *s,bool pressed,uint32_t now) {
    if(pressed != s->candidate) { s->candidate=pressed; s->since=now; }
    if(s->stable != s->candidate && (uint32_t)(now-s->since)>=25) {
        s->stable=s->candidate; return s->stable;
    }
    return false;
}
