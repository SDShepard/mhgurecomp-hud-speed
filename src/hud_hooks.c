// MHGU 1.4.0: standalone Forge HUD clock adapters.
// No runtime-specific imports and no game code/assets embedded in this plugin.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void *target, *detour, *original, *context, *context_slot;
    bool has_context;
    uint8_t pad[3];
} Hook;
extern Hook forge_hook_create(void *, void *, void **);
extern unsigned hud_speed_percent(void);
extern void hud_gui_entry(void);

static uintptr_t main_text;
static bool ready;
static void (*original_plate)(void);
void *hud_gui_target;

static uint32_t hash(const uint8_t *bytes, unsigned length) {
    uint32_t value=2166136261u;
    for(unsigned i=0;i<length;i++) value=(value^bytes[i])*16777619u;
    return value;
}

// QueryMemory is the normal Horizon ARM32 SVC ABI, supported by public Recomp.
// No host pointer is used. Unknown/unreadable resources retain normal timing.
static bool readable(uintptr_t address, unsigned length) {
    struct { uint64_t base, size; uint32_t type, attr, perm, ipc, device, pad; } info;
    if(address<main_text || (uint64_t)address+length>0x100000000ull) return false;
    register uintptr_t r0 __asm__("r0")=(uintptr_t)&info;
    register uintptr_t r1 __asm__("r1")=0;
    register uintptr_t r2 __asm__("r2")=address;
    __asm__ volatile("svc #6" : "+r"(r0), "+r"(r1), "+r"(r2) : : "r3", "memory", "cc");
    return r0==0 && (info.perm&1u) && info.base<=address &&
           (uint64_t)address+length<=info.base+info.size;
}

static bool equal(const char *a,const char *b) {
    for(unsigned i=0;i<64;i++) {
        if(a[i]!=b[i]) return false;
        if(!a[i]) return true;
    }
    return false;
}

static bool adjustable(uintptr_t owner) {
    if(!readable(owner,0xf4) || *(const uint32_t *)owner!=main_text+0x176c620u) return false;
    uintptr_t resource=*(const uint32_t *)(owner+0xf0);
    if(!readable(resource,72) || *(const uint32_t *)resource!=main_text+0x1794c9cu) return false;
    const char *name=(const char *)(resource+8);
    bool terminated=false;
    for(unsigned i=0;i<64;i++) if(!name[i]) {terminated=true;break;}
    if(!terminated) return false;
    if(!((name[0]=='e' && name[1]=='n' && name[2]=='g') ||
         (name[0]=='f' && name[1]=='r' && name[2]=='e') ||
         (name[0]=='s' && name[1]=='p' && name[2]=='a') ||
         (name[0]=='g' && name[1]=='e' && name[2]=='r') ||
         (name[0]=='i' && name[1]=='t' && name[2]=='a'))) return false;
    static const char *const names[]={
        "\\GUI\\01_quest\\hud\\hud0", "\\GUI\\01_quest\\hud\\hud1_item",
        "\\GUI\\01_quest\\hud\\hud2_bullet", "\\GUI\\01_quest\\hud\\hud4_t_sword",
        "\\GUI\\01_quest\\hud\\hud5_l_sword", "\\GUI\\01_quest\\hud\\hud6_scope",
        "\\GUI\\01_quest\\hud\\hud7_bowgun", "\\GUI\\01_quest\\hud\\hud8_bow",
        "\\GUI\\01_quest\\hud\\hud9_g_lance", "\\GUI\\01_quest\\hud\\hud10_other_g",
        "\\GUI\\01_quest\\hud\\hud11_s_axe", "\\GUI\\01_quest\\hud\\hud12_i_club",
        "\\GUI\\01_quest\\hud\\hud13_flute", "\\GUI\\01_quest\\hud\\hud14_chargeAx",
        "\\GUI\\01_quest\\hud\\hud15_name", "\\GUI\\01_quest\\hud\\hud16",
        "\\GUI\\01_quest\\hud\\hud17_barrel", "\\GUI\\01_quest\\hud\\hud_ny0",
        "\\GUI\\01_quest\\hud\\hud_ny1", "\\GUI\\01_quest\\map\\map_icon",
        "\\GUI\\01_quest\\custom\\custom_panel"
    };
    for(unsigned i=0;i<sizeof names/sizeof names[0];i++) if(equal(name+3,names[i])) return true;
    return false;
}

static uint32_t bits(float value) { union {float f;uint32_t u;} v={.f=value};return v.u; }
static bool finite(float value) {return (bits(value)&0x7f800000u)!=0x7f800000u;}

// Called by the assembly adapter with an aligned four-word scratch buffer.
// Reproduce the original entry's r1/s0/s2/s4 output; only s0 is scaled.
void hud_gui_prepare(uintptr_t owner,uint32_t out[4]) {
    uintptr_t indirect=*(const uint32_t *)(main_text+0x1838d84u);
    uintptr_t driver=*(const uint32_t *)indirect;
    float authored=*(const float *)(owner+0x10c);
    float divisor=*(const float *)(driver+0x38);
    float delta=*(const float *)(owner+0x1c)*authored/divisor;
    unsigned speed=hud_speed_percent();
    if(ready && speed!=100 && speed<=200 && adjustable(owner)) delta*=speed/100.0f;
    out[0]=driver;out[1]=bits(delta);out[2]=bits(authored);out[3]=bits(divisor);
}

static void plate_update(void) {
    static double fraction;
    static uint32_t last=0xffffffffu;
    if(!ready) { original_plate();return; }
    uintptr_t indirect=*(const uint32_t *)(main_text+0x1838d84u);
    uintptr_t driver=*(const uint32_t *)indirect;
    uint32_t *counter=*(uint32_t **)(main_text+0x183f224u);
    float delta=*(const float *)(driver+0x68);
    unsigned speed=hud_speed_percent();
    if(speed>200) speed=100;
    float step=delta*(speed/100.0f);
    uint32_t current=*counter;
    if(current>=80 || !finite(step) || step<0 || step>10000) {
        fraction=0;last=0xffffffffu;original_plate();return;
    }
    if(current!=last) fraction=0;
    double sum=(double)current+(double)step+fraction;
    uint32_t whole=(uint32_t)sum;
    fraction=sum-(double)whole;
    last=whole%80u;
    *counter=last;
}

bool hud_hooks_validate(uintptr_t main) {
    return main && hash((const uint8_t *)(main+0x9194e0u),40)==0x81db1082u &&
           hash((const uint8_t *)(main+0x919508u),8)==0x0c595f5du &&
           hash((const uint8_t *)(main+0x633be8u),84)==0x824d5f43u;
}

void hud_hooks_deactivate(void) { ready=false; }

bool hud_hooks_init(uintptr_t main) {
    if(!hud_hooks_validate(main)) return false;
    main_text=main;hud_gui_target=(void *)(main+0x919508u);
    void *original_gui=NULL;
    Hook gui=forge_hook_create((void *)(main+0x9194e0u),(void *)hud_gui_entry,&original_gui);
    if(!original_gui || gui.original!=original_gui) return false;
    Hook plate=forge_hook_create((void *)(main+0x633be8u),(void *)plate_update,(void **)&original_plate);
    if(!original_plate || plate.original!=(void *)original_plate) return false;
    ready=true;
    return true;
}
