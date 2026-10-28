// MHGU 1.4.0 Forge plugin: elapsed-time hunter-name buff carousel.
// Original eligibility and table traversal remain in the game's selector.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define EXPORT __attribute__((visibility("default")))
#define SELECTOR_OFFSET 0x634D00u
#define SELECTOR_SIZE 0x210u
#define SELECTOR_FNV 0x5E82CCF7u
#define TICKS_PER_MS 19200u
#define MAX_GAP_TICKS (250u * TICKS_PER_MS)
#define CLOCK_SLOTS 32
#define DIAGNOSTIC_CAPACITY 256

typedef struct { uint16_t major, minor, patch; char name[64]; } PluginInit;
typedef struct {
    void *target, *detour, *original, *context, *context_slot;
    bool has_context;
    uint8_t pad[3];
} Hook;
typedef struct { uint32_t index; int32_t countdown; uint8_t active, pad[3]; } Carousel;
typedef void (*Selector)(Carousel *, const void *, const void *, uint32_t);

// Hidden diagnostic ABI, described in build.json. Only enabled=1 records. The
// host owns enabled/wrap/reset_request; all other words belong to the plugin.
// Read while paused, or accept a snapshot only when sequence is unchanged/even.
typedef struct {
    uint32_t serial, event;
    uint64_t ticks;
    uint32_t state, table, before_index, index, before_active, active;
    uint32_t dwell_ms;
    int32_t countdown;
    uint32_t timing_flags, table_count;
} DiagnosticRecord;
typedef struct {
    uint32_t magic, version, header_size, record_size, capacity;
    uint32_t enabled, wrap, reset_request, reset_ack, sequence;
    uint32_t record_count, retained, next_slot, dropped, observed_calls, reserved;
    DiagnosticRecord records[DIAGNOSTIC_CAPACITY];
} Diagnostics;
_Static_assert(sizeof(DiagnosticRecord) == 56, "diagnostic record ABI");
_Static_assert(offsetof(Diagnostics, records) == 64, "diagnostic header ABI");
_Static_assert(sizeof(Diagnostics) == 14400, "diagnostic buffer ABI");
EXPORT volatile Diagnostics buff_timing_diagnostics;

static unsigned speed_percent = 100;
unsigned hud_speed_percent(void) { return __atomic_load_n(&speed_percent,__ATOMIC_RELAXED); }
extern bool hud_hooks_validate(uintptr_t);
extern bool hud_hooks_init(uintptr_t);
extern void hud_hooks_deactivate(void);
extern uintptr_t forge_mem_getMainTextAddr(void);
extern Hook forge_hook_create(void *, void *, void **);
extern void forge_log(int level, const char *message, ...);
extern bool igCheckbox(const char *, bool *);
extern bool igSliderInt(const char *, int *, int, int, const char *, int);
extern void igTextUnformatted(const char *, const char *);

typedef struct {
    Carousel *state;
    const void *table;
    uint64_t last, elapsed;
    uint32_t index;
    bool active;
} Clock;

static Clock clocks[CLOCK_SLOTS];
static Selector original_selector;
static uintptr_t main_text;
static bool ready, enabled = true, hold_icon;
static int dwell_ms = 700;
static unsigned clock_lock;

static void lock(void) {
    while (__atomic_exchange_n(&clock_lock, 1, __ATOMIC_ACQUIRE)) {}
}
static void unlock(void) { __atomic_store_n(&clock_lock, 0, __ATOMIC_RELEASE); }
static uint64_t ticks(void) {
    uint64_t value;
    __asm__ volatile("mrrc p15, 0, %Q0, %R0, c14" : "=r"(value));
    return value;
}

// Runs under clock_lock, after the original selector. This writes only our
// diagnostic storage and uses the same timestamp already used for timing.
static void record_transition(uint64_t now, Carousel *state, const void *table, uint32_t count,
                              uint32_t before_index, uint32_t before_active) {
    volatile Diagnostics *d = &buff_timing_diagnostics;
    uint32_t request = d->reset_request;
    bool reset = request != d->reset_ack;
    bool recording = d->enabled == 1;
    if (!reset && !recording) return;
    uint32_t sequence = d->sequence;
    __atomic_store_n(&d->sequence, sequence + 1, __ATOMIC_RELEASE);
    if (reset) {
        d->record_count = d->retained = d->next_slot = d->dropped = d->observed_calls = 0;
        d->reset_ack = request;
    }
    if (recording) {
        d->observed_calls++;
        uint32_t event = (before_index != state->index ? 1u : 0u) |
                         (before_active != state->active ? 2u : 0u);
        if (event) {
            bool full = d->retained == DIAGNOSTIC_CAPACITY;
            if (full && d->wrap != 1) {
                if (d->dropped != UINT32_MAX) d->dropped++;
            } else {
                volatile DiagnosticRecord *r = &d->records[d->next_slot];
                r->serial = d->record_count + 1;
                r->event = event;
                r->ticks = now;
                r->state = (uint32_t)(uintptr_t)state;
                r->table = (uint32_t)(uintptr_t)table;
                r->before_index = before_index; r->index = state->index;
                r->before_active = before_active; r->active = state->active;
                r->dwell_ms = (uint32_t)dwell_ms;
                r->countdown = state->countdown;
                r->timing_flags = (enabled ? 1u : 0u) | (hold_icon ? 2u : 0u);
                r->table_count = count;
                d->record_count++;
                if (!full) d->retained++;
                d->next_slot = (d->next_slot + 1) % DIAGNOSTIC_CAPACITY;
            }
        }
    }
    __atomic_store_n(&d->sequence, sequence + 2, __ATOMIC_RELEASE);
}

static Clock *clock_for(Carousel *state, const void *table, uint64_t now) {
    Clock *oldest = &clocks[0];
    for (unsigned i = 0; i < CLOCK_SLOTS; i++) {
        Clock *c = &clocks[i];
        if (c->state == state && c->table == table) return c;
        if (!c->state) { oldest = c; break; }
        if (c->last < oldest->last) oldest = c;
    }
    *oldest = (Clock){.state = state, .table = table, .last = now};
    return oldest;
}

static void select_timed(Carousel *state, const void *flags, const void *table, uint32_t count) {
    // Only the two observed carousel tables are admitted. Unexpected callers retain original behavior.
    if (!((uintptr_t)table == main_text + 0x1639250u && count == 54) &&
        !((uintptr_t)table == main_text + 0x16395B0u && count == 36)) {
        original_selector(state, flags, table, count);
        return;
    }
    lock();
    uint64_t now = ticks();
    uint32_t before_index = state->index, before_active = state->active;
    Clock *clock = clock_for(state, table, now);
    if (!enabled) {
        clock->active = false;
        clock->last = now;
        // Preserve the unmodified selector, including its call-count behavior, when disabled.
        original_selector(state, flags, table, count);
        record_transition(now, state, table, count, before_index, before_active);
        unlock();
        return;
    }
    bool continuing = clock->active && state->active && clock->index == state->index && state->countdown >= 0;
    uint64_t elapsed = continuing ? clock->elapsed : 0;
    uint64_t delta = now >= clock->last ? now - clock->last : 0;
    // Suspension, loading and long stalls freeze the remaining dwell; there is no burst of catch-up selections.
    if (continuing && delta <= MAX_GAP_TICKS) {
        // Multiply elapsed time, not the threshold: a slider change preserves progress.
        unsigned speed = hud_speed_percent();
        if (speed > 200) speed = 100;
        elapsed += delta * speed / 100;
    }
    uint32_t duration = (uint32_t)dwell_ms * TICKS_PER_MS;
    bool due = continuing && !hold_icon && elapsed >= duration;
    if (state->active) state->countdown = due ? 0 : 1;
    original_selector(state, flags, table, count);
    // A stable eligible icon decrements 1 to 0. A new selection reloads 20. Thus expiry still replaces an icon
    // immediately even when hold is enabled; holding an icon never extends its gameplay status effect.
    bool selected = state->active && state->countdown > 0;
    if (!state->active) elapsed = 0;
    else if (selected) elapsed = due ? elapsed - duration : 0;
    if (hold_icon) elapsed = 0;
    clock->last = now;
    clock->elapsed = elapsed;
    clock->index = state->index;
    clock->active = state->active != 0;
    record_transition(now, state, table, count, before_index, before_active);
    unlock();
}


// Called by the UI: finish the pending interval at the previous rate before
// publishing the new rate. This avoids retroactively speeding up paused time.
static void set_speed(unsigned value) {
    if (value > 200) return;
    lock();
    unsigned previous = hud_speed_percent();
    uint64_t now = ticks();
    for (unsigned i=0;i<CLOCK_SLOTS;i++) {
        Clock *c=&clocks[i];
        uint64_t delta=now>=c->last ? now-c->last : 0;
        if(c->active && delta<=MAX_GAP_TICKS) c->elapsed+=delta*previous/100;
        c->last=now;
    }
    __atomic_store_n(&speed_percent,value,__ATOMIC_RELAXED);
    unlock();
}
EXPORT void forge_onLoad(PluginInit *param) {
    param->major=1;param->minor=5;param->patch=1;
    const char name[]="HUD Speed";
    for(unsigned i=0;i<sizeof name;i++)param->name[i]=name[i];
    buff_timing_diagnostics.magic=0x47445442u;
    buff_timing_diagnostics.version=1;
    buff_timing_diagnostics.header_size=64;
    buff_timing_diagnostics.record_size=sizeof(DiagnosticRecord);
    buff_timing_diagnostics.capacity=DIAGNOSTIC_CAPACITY;
}
EXPORT void forge_onInit(void) {
    main_text=forge_mem_getMainTextAddr();
    const uint8_t *code=(const uint8_t *)(main_text+SELECTOR_OFFSET);
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<SELECTOR_SIZE;i++)hash=(hash^code[i])*16777619u;
    if(hash!=SELECTOR_FNV || !hud_hooks_validate(main_text)) {
        forge_log(2,"HUD Speed: unsupported or already patched game code; no hooks installed.");
        return;
    }
    if(!hud_hooks_init(main_text)) {
        forge_log(2,"HUD Speed: HUD hook installation failed. Restart after removing conflicting plugins.");
        return;
    }
    Hook hook=forge_hook_create((void *)code,(void *)select_timed,(void **)&original_selector);
    ready=original_selector!=NULL && hook.original==(void *)original_selector;
    if(!ready) hud_hooks_deactivate();
    forge_log(ready?1:2,ready?"HUD Speed: standalone HUD and buff hooks installed.":
              "HUD Speed: buff hook failed; restart after removing conflicting plugins.");
}
EXPORT void forge_onImGuiRender(void) {
    if(!ready) {
        igTextUnformatted("HUD Speed inactive or incomplete. See log.txt; restart after removing conflicts.",NULL);
        return;
    }
    int value=(int)hud_speed_percent();
    if(igSliderInt("HUD animation speed",&value,0,200,"%d%%",0) && value>=0 && value<=200)
        set_speed((unsigned)value);
    igTextUnformatted("HUD animations and buff cycling. Menus and quest notices keep normal speed.",NULL);
    igTextUnformatted("0% pauses animations; expired buffs still disappear. Some shared blinks stay normal.",NULL);
    igTextUnformatted("Settings apply for this session. Default: 100%. Gameplay values are unchanged.",NULL);
}
