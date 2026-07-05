/**

- ROADRUNNER 0.4 — Prediction Looper
- 
- KNOB_0 (top-left)      : Effect strength (0=none, 1=full)  [white vis]
- KNOB_2 (top-center)    : Loop divider                       [purple vis]
- KNOB_4 (top-right)     : Effect select (LP/HP/verb/delay)   [teal vis]
- KNOB_1 (bottom-left)   : Crossfader (L=live, R=loop)        [white vis]
- KNOB_3 (bottom-center) : Pitch ±12 semi                     [green vis]
- KNOB_5 (right-middle)  : Time stretch (1/8x..8x, in time)   [green vis]
- KNOB_6 (bottom-right)  : End-of-chain volume (right=full)
- 
- Effect engages on the loop signal as crossfader leaves loop side.
- Pitch + speed reset to neutral on loop start (catch back to center).
-
- Toggle 1: When to record (left=next bar, center=+1, right=+2)
- Toggle 2: How many bars (left=1, center=2, right=4)
- Button: Record / Drop
- Gate In: 24PPQN clock
  */

#include "daisy_versio.h"
#include "daisysp.h"
#include "gate_detect.h"
#include "looper_engine.h"
#include "dj_filter.h"
#include "pitch_shifter.h"
#include "shimmer_reverb.h"

using namespace daisy;
// NOTE: do NOT `using namespace daisysp;` — DaisySP has its own
// `Looper` and `PitchShifter` classes which would collide with ours.

// Padé tanh approximation — smooth analog-style soft saturation.
// Linear at low amplitude, gently rolls off above ~0.7.
// Used in the volume overdrive zone (1.00..1.06).
static inline float SoftSat(float x)
{
    if(x >  3.0f) return  1.0f;
    if(x < -3.0f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

static constexpr uint32_t AUDIO_BLOCK      = 256;
static constexpr uint32_t MAX_LOOP_SAMPLES = 1200000;
static uint32_t startup_mute = 4800;

static constexpr float    KNOB_THRESH    = 0.005f;  // ~0.5% knob travel
static constexpr uint32_t VIS_DUR        = 800;      // takeover duration (ms)
static constexpr uint32_t VIS_FADE_TICKS = 250;      // last 250ms is fade-out

static DaisyVersio hw;

static float DSY_SDRAM_BSS loop_buf_l[MAX_LOOP_SAMPLES];
static float DSY_SDRAM_BSS loop_buf_r[MAX_LOOP_SAMPLES];

static Looper looper;
static PitchShifter pitch_l, pitch_r;

// ── Effect switcher (KNOB_4 picks, KNOB_0 sets strength) ──
// Replaces per-side filters. Effect is applied to the LOOP signal,
// scaled by (1 - xfade) so it engages only as the crossfader leaves
// its loop-side default and we transition out.
enum EffectMode { FX_LP = 0, FX_HP = 1, FX_REV = 2, FX_DLY = 3 };

static DJFilter   fx_lp;
static DJFilter   fx_hp;
static ShimmerReverb DSY_SDRAM_BSS fx_reverb;

static constexpr int FX_DELAY_SIZE = 24000;        // 0.5s @ 48kHz
static float DSY_SDRAM_BSS fx_delay_l[FX_DELAY_SIZE];
static float DSY_SDRAM_BSS fx_delay_r[FX_DELAY_SIZE];
static int   fx_delay_wr   = 0;
static int   fx_delay_time = 12000;                // 0.25s default; clock-synced when available

static int EffectModeFromKnob(float k)
{
    if(k < 0.25f) return FX_LP;
    if(k < 0.50f) return FX_HP;
    if(k < 0.75f) return FX_REV;
    return FX_DLY;
}

// Clock
static PPQNClockDetector ppqn;
static bool prev_gate = false;

// Knobs
static float cur_knob[7]    = {};
// Anchor position: each knob's "rest" state used as the reference for
// motion detection. Only updates when the knob is actively triggering
// vis or is the currently-displayed knob. This is what makes slow turns
// reliably detected — they accumulate against a stable reference instead
// of being washed out by per-block delta filtering.
static float anchor_knob[7] = { -1, -1, -1, -1, -1, -1, -1 };
static uint32_t knob_vis_timer = 0;
static uint32_t xfade_vis_timer = 0;
static int knob_vis_which = -1;

// Idle VU meter — peak follower on live input.
// Fast attack (instant), slow release (~150ms).
static float vu_level = 0.0f;

// Track loop_on edge — reset pitch shifter buffers when entering LOOPING
// so any previously-pitched audio doesn't leak into the new loop's first
// 16ms (phase vocoder has internal FIFOs that persist).
static bool prev_loop_on = false;

// Track looper state — when it changes (e.g. IDLE→ARMED on gate press),
// any active knob vis is cleared so the user immediately sees the new
// state animation without waiting for the previous knob's vis to fade.
static LoopState prev_loop_state = LoopState::IDLE;

// ── SHIFT MODE (gate-button hold) ──
// Press fires the tap action immediately for IDLE/ARMED/RECORDING
// (preserves the snappy arm/abort feel). For LOOPING:
//   • Hold ≥ 250ms          → enter SHIFT mode, do NOT drop loop
//   • Quick release (<250ms)→ drop the loop on falling edge
// Uses libDaisy's TimeHeldMs() for reliability.
// In SHIFT mode, effects bypass crossfade gating and apply LIVE.
static bool shift_mode = false;

// Effect routing — DRY/WET SEND ARCHITECTURE
//
// The filter (or reverb/delay) processes the loop signal at FULL VOLUME
// regardless of crossfade position. KNOB_0 is the wet/dry mix amount.
// The wet path then has its own envelope that keeps it punchy through
// the transition — instead of fading proportionally with the loop.
//
//   fx_wet_amount      knob position 0..1 (smoothed)
//                      0 = no effect heard at all
//                      1 = max wet level
//
//   fx_wet_envelope    crossfade-aware envelope for the wet path
//                      Trapezoidal: deadzone right, plateau in middle,
//                      gentle ramp at left so wet survives until almost
//                      full live.
//
//   wet_gain           = fx_wet_amount × fx_wet_envelope
//
// In SHIFT mode (gate held), envelope is forced to 1.0 — full wet
// regardless of crossfade. KNOB_0 still controls how much wet.
static float fx_wet_amount   = 0.0f;
static float fx_wet_envelope = 0.0f;

// Effect secondary parameters (modifiable via SHIFT + KNOB_0).
// These PERSIST across SHIFT toggles — set them once, they stay set.
//   fx_lp_q / fx_hp_q     → resonance (0.5..5.0)
//   fx_reverb_shimmer     → octave-up shimmer amount (0..1)
//   fx_delay_feedback     → echo decay (0.30..0.92)
static float fx_lp_q             = 1.5f;
static float fx_hp_q             = 1.5f;
static float fx_reverb_shimmer   = 0.0f;
static float fx_delay_feedback   = 0.55f;

// SHIFT secondary catch-up: when entering SHIFT, the active secondary
// is "uncaught" — KNOB_0 must travel near the secondary's current value
// before adjustments take effect. Prevents jumps when knob is far from
// the param's current state. Same pattern as pitch/speed catch-up.
static bool fx_secondary_caught = false;
static int  fx_secondary_mode   = -1;   // last mode that captured K0

// ── DELAY DYNAMIC FEEDBACK + LATCH ──
// Feedback amount ramps as you crossfade (0.45 baseline → 0.85 long-tail).
// Below xfade=0.25, input gates off and the now-fat tail decays out.
// Gives the classic DJ "delay throw" feel: more buildup as you throw,
// then a long dense echo-out at the end. See processing logic below.
static bool delay_latched = false;

static uint32_t error_flash = 0;
static uint32_t block_counter = 0;

// ═══════════════════════════════════════════════════════════════════════════

static void AudioCallback(AudioHandle::InputBuffer in,
AudioHandle::OutputBuffer out,
size_t size)
{
hw.ProcessAllControls();
hw.tap.Debounce();
for(int k = 0; k < 7; k++)
    cur_knob[k] = hw.GetKnobValue(k);

// Knob vis trigger — anchored detector.
//
// `anchor[k]` updates only when knob k is the active vis (timer > 0 and
// k is the one being shown). For all other knobs, we compare current
// position against the anchor — meaning slow turns accumulate and
// eventually trip the threshold instead of getting filtered out by
// per-block delta filtering. Same threshold applies to all 7 knobs
// (including crossfader) for consistent feel.
//
// Switching knobs PREEMPTS the previous vis — clears its timer and
// reassigns to the new knob immediately so the user sees the new vis
// without waiting for the old one to finish fading.
for(int k = 0; k < 7; k++)
{
    if(anchor_knob[k] < 0.0f)
        anchor_knob[k] = cur_knob[k];                       // boot init

    bool moved = fabsf(cur_knob[k] - anchor_knob[k]) > KNOB_THRESH;

    if(moved)
    {
        if(k == 1)
        {
            xfade_vis_timer = VIS_DUR;
        }
        else
        {
            // Preempt: if a different knob's vis is showing, clear it
            if(knob_vis_which != k)
                knob_vis_timer = 0;
            knob_vis_timer = VIS_DUR;
            knob_vis_which = k;
        }
        anchor_knob[k] = cur_knob[k];
    }
}

// When knob_vis_timer is active, keep the active knob's anchor synced
// to its live position so the displayed knob doesn't suddenly stop
// updating mid-vis (this also stops jitter on the active knob from
// re-triggering itself).
if(knob_vis_timer > 0 && knob_vis_which >= 0)
    anchor_knob[knob_vis_which] = cur_knob[knob_vis_which];
if(xfade_vis_timer > 0)
    anchor_knob[1] = cur_knob[1];

// ── Toggles ────────────────────────────────────────────────────────
int sw0 = hw.sw[0].Read();
uint32_t delay;
switch(sw0)
{
    case 1: delay = 0; break;
    case 0: delay = 1; break;
    case 2: delay = 2; break;
    default: delay = 0; break;
}
looper.SetDelayBars(delay);

int sw1 = hw.sw[1].Read();
uint32_t bars;
switch(sw1)
{
    case 1: bars = 1; break;
    case 0: bars = 2; break;
    case 2: bars = 4; break;
    default: bars = 2; break;
}
looper.SetBarCount(bars);

// ── Knob routing ───────────────────────────────────────────────────

// Crossfader (independent of effects now — the gating idea was a bug,
// it muted the very signal being processed).
float xfade = looper.GetCrossfade(cur_knob[1]);

// Effect switcher (KNOB_4 picks, KNOB_0 strength)
//   KNOB_4 selects: LP / HP / smeary verb / clock-synced delay
//   KNOB_0 = effect strength (always, both modes — single source of truth).
//
// EFFECT GATING:
//   Normal mode  → effect amount = strength * (1 - xfade)
//                  At full loop (xfade=1): effect SILENT regardless of knob.
//                  As you crossfade left, effect ramps in. DJ "engage on
//                  transition" behavior. This is the default.
//
//   SHIFT mode   → effect amount = strength
//                  Hold the gate button (≥400ms while LOOPING) to lift the
//                  crossfade gating. Effect goes LIVE — heard regardless
//                  of crossfader. Useful for auditioning the loop with
//                  effect at full loop position.
//
// Per-effect secondaries (Q, shimmer, delay feedback) are at fixed
// defaults for now. The SHIFT+K0 mapping that previously controlled them
// has been repurposed for live-effect mode.
int fx_mode = EffectModeFromKnob(cur_knob[4]);

// ── KNOB_0 routing depends on SHIFT mode ──
// Normal mode: KNOB_0 = effect strength (audible level via crossfader).
// SHIFT mode:  KNOB_0 = secondary parameter for current effect:
//                LP/HP → resonance Q (0.5..5.0)
//                REV   → shimmer amount (0..1)
//                DLY   → feedback (0.30..0.92)
//              Effect is NOT made audible by SHIFT — crossfader still
//              governs audibility. SHIFT just changes the parameter.
//              Catch-up: knob must come close to current param value
//              before edits register, so you don't jump the param.
//
// The secondary value PERSISTS after release. Hold, tweak, let go,
// throw the crossfader — your tweaked param is still in effect.
if(shift_mode)
{
    // Reset catch-up if the user changed effect mode while held
    if(fx_secondary_mode != fx_mode)
    {
        fx_secondary_caught = false;
        fx_secondary_mode   = fx_mode;
    }

    // Compute target knob position for current secondary
    float target_pos;
    switch(fx_mode)
    {
        case FX_LP:  target_pos = (fx_lp_q - 0.5f) / 4.5f;            break;
        case FX_HP:  target_pos = (fx_hp_q - 0.5f) / 4.5f;            break;
        case FX_REV: target_pos = fx_reverb_shimmer;                  break;
        case FX_DLY: target_pos = (fx_delay_feedback - 0.30f) / 0.62f; break;
        default:     target_pos = cur_knob[0];                        break;
    }
    if(target_pos < 0.0f) target_pos = 0.0f;
    if(target_pos > 1.0f) target_pos = 1.0f;

    // Catch-up: only adjust once knob is within 5% of param value
    if(!fx_secondary_caught
       && fabsf(cur_knob[0] - target_pos) < 0.05f)
        fx_secondary_caught = true;

    if(fx_secondary_caught)
    {
        switch(fx_mode)
        {
            case FX_LP:  fx_lp_q          = 0.5f + cur_knob[0] * 4.5f;   break;
            case FX_HP:  fx_hp_q          = 0.5f + cur_knob[0] * 4.5f;   break;
            case FX_REV: fx_reverb_shimmer = cur_knob[0];                break;
            case FX_DLY: fx_delay_feedback = 0.30f + cur_knob[0] * 0.62f; break;
            default: break;
        }
    }
}
else
{
    // Reset catch-up tracking when SHIFT releases — next entry recatches
    fx_secondary_caught = false;
    fx_secondary_mode   = -1;
}

// fx_strength is ALWAYS KNOB_0 in normal mode (bipolar — see below).
// In SHIFT, it freezes at whatever value it had when SHIFT was entered,
// so audible effect level doesn't jump while you tweak the secondary.
//
// BIPOLAR KNOB_0:
//   Knob position determines BOTH effect strength AND which side of the
//   crossfader the effect lives on:
//
//     left of center  → effect lives on the LIVE signal (incoming track).
//                       Best when crossfading live→loop. Effect tail
//                       trails the disappearing live signal.
//     center (±5%)    → no effect anywhere (deadzone).
//     right of center → effect lives on the LOOP signal (current default).
//                       Best when crossfading loop→live. Effect tail
//                       trails the disappearing loop.
//
//   strength magnitude scales linearly from center: 0% at deadzone edge,
//   100% at full deflection on either side.
static float fx_strength = 0.0f;
static bool  fx_on_loop  = true;   // false = effect on live signal
if(!shift_mode)
{
    float k0 = cur_knob[0];
    float bipolar = k0 - 0.5f;             // -0.5 .. +0.5
    if(fabsf(bipolar) < 0.05f)
    {
        fx_strength = 0.0f;
        // fx_on_loop stays at last value — avoid flapping in deadzone
    }
    else if(bipolar > 0.0f)
    {
        fx_on_loop  = true;
        fx_strength = (bipolar - 0.05f) / 0.45f;   // 0..1
    }
    else
    {
        fx_on_loop  = false;
        fx_strength = (-bipolar - 0.05f) / 0.45f;
    }
    if(fx_strength < 0.0f) fx_strength = 0.0f;
    if(fx_strength > 1.0f) fx_strength = 1.0f;
}

// EFFECTIVE WET LEVEL = how loud the wet signal hits the output.
// CROSSFADER ALWAYS GOVERNS AUDIBILITY — SHIFT does not bypass.
//
// Envelope shape now depends on which side the effect is on, since
// "leaving" is a different gesture in each direction:
//   FX on LOOP   → effect builds as you move LEFT (away from full loop)
//                  envelope = function of xfade descending
//   FX on LIVE   → effect builds as you move RIGHT (away from full live)
//                  envelope = mirrored — function of (1-xfade) descending
//
// Either way the effect is silent at the "home" extreme, ramps in fast,
// plateaus through the bulk of the throw, and fades out at the far end.
float wet_amount_target = fx_strength;
float wet_env_target;
{
    // Distance from "home" extreme (where this effect is silent):
    //   FX on LOOP → home is xfade=1.0 → distance = 1 - xfade
    //   FX on LIVE → home is xfade=0.0 → distance = xfade
    float d = fx_on_loop ? (1.0f - xfade) : xfade;

    if(d <= 0.05f)
        wet_env_target = 0.0f;                         // home deadzone
    else if(d <= 0.20f)
        wet_env_target = (d - 0.05f) / 0.15f;          // snap in
    else if(d <= 0.95f)
        wet_env_target = 1.0f;                         // long plateau
    else
        wet_env_target = (1.0f - d) / 0.05f;           // far-end fade
}

fx_wet_amount   += (wet_amount_target - fx_wet_amount)   * 0.20f;
fx_wet_envelope += (wet_env_target    - fx_wet_envelope) * 0.20f;
float wet_gain = fx_wet_amount * fx_wet_envelope;

// Drive filter cutoff from KNOB_0 directly (full depth, independent
// of crossfade). When wet_gain is 0 the filter output isn't heard
// anyway, so cutoff position is moot. When wet_gain > 0 the filter
// is at full intensity from the moment the wet path opens up.
//
// Filter knob mapping:
//   strength=0 → SetKnob(0.5) → flat (no DSP cost on bypass)
//   strength=1 → SetKnob(0)/(1) → full sweep
fx_lp.SetKnob(0.5f - 0.5f * fx_strength);
fx_hp.SetKnob(0.5f + 0.5f * fx_strength);

// Push secondary params to engines every block. They've persisted
// since the last SHIFT tweak (or boot defaults).
fx_lp.SetResonance(fx_lp_q);
fx_hp.SetResonance(fx_hp_q);
fx_reverb.SetShimmer(fx_reverb_shimmer);

// Sync delay to clock — 3/4 of a beat, the DJ transition sweet spot.
// Falls back to ~285ms if no clock locked.
if(ppqn.has_clock)
{
    uint32_t bp = ppqn.GetBeatPeriod();
    if(bp > 0)
    {
        // 3/4 beat = bp * 3 / 4
        int t = static_cast<int>((bp * 3) / 4);
        if(t > 0 && t < FX_DELAY_SIZE - 16)
            fx_delay_time = t;
    }
}

// Divide, speed
looper.SetDivide(cur_knob[2]);
looper.SetSpeed(cur_knob[5]);

// Pitch: update factor, set on pitch shifter
if(looper.CatchPitch(cur_knob[3]))
{
    float pf = looper.GetPitchFactor();
    pitch_l.SetFactor(pf);
    pitch_r.SetFactor(pf);
}
else
{
    pitch_l.SetFactor(1.0f);
    pitch_r.SetFactor(1.0f);
}

// END-OF-CHAIN VOLUME — knob mapping with overdrive zone.
//
//   0..1% knob          → 0.0 (hard zero — kills DAC bleed at full CCW)
//   1..90% knob         → linear 0.0..1.0  (clean operating range)
//   90..100% knob       → 1.0..1.20 (analog-style soft overdrive zone)
//
// The last 10% of knob travel pushes signal +1.6 dB through tanh-style
// soft saturation. That extra headroom is gentle harmonic warmth, not
// digital clipping. The DAC handles the over-unity range cleanly because
// the soft-sat curve maps the gained signal back into [-1, 1] before
// it leaves the buffer.
float vk = cur_knob[6];
float vol;
if(vk < 0.01f)
    vol = 0.0f;                              // hard zero
else if(vk < 0.90f)
    vol = vk * (1.0f / 0.90f);               // 0..1.0 over first 90%
else
    vol = 1.0f + (vk - 0.90f) * 2.0f;        // 1.0..1.20 over last 10%
if(vol < 0.0f) vol = 0.0f;

// ── Button (immediate fire in IDLE/ARMED/RECORDING; deferred in LOOPING) ──
//
// IDLE/ARMED/RECORDING: rising edge fires OnButton() immediately —
// preserves the snappy "press to arm" feel.
//
// LOOPING: rising edge does NOTHING. We watch TimeHeldMs():
//   • Past SHIFT_HOLD_MS while still pressed → enter SHIFT (no drop)
//   • Released BEFORE SHIFT engaged          → drop on falling edge
// This guarantees a long hold can't trigger a drop, and a short press
// drops cleanly on release. Uses libDaisy's built-in hold timer for
// reliability — no sample counters that could drift.
constexpr uint32_t SHIFT_HOLD_MS = 250;

if(hw.tap.RisingEdge())
{
    LoopState st = looper.GetState();
    if(st != LoopState::LOOPING)
    {
        // Immediate fire in non-looping states
        bool ok = looper.OnButton();
        if(!ok && looper.GetState() == LoopState::IDLE)
            error_flash = 48000;
    }
    // In LOOPING: do nothing on rising edge. Decision made on hold/release.
}

if(looper.GetState() == LoopState::LOOPING && hw.tap.Pressed())
{
    // Held long enough → enter SHIFT
    if(!shift_mode && hw.tap.TimeHeldMs() >= SHIFT_HOLD_MS)
        shift_mode = true;
}

if(hw.tap.FallingEdge())
{
    // Released. If we never entered SHIFT and we were in LOOPING, drop.
    if(!shift_mode && looper.GetState() == LoopState::LOOPING)
        looper.OnButton();
    shift_mode = false;
}

// State-change preemption: when the looper transitions to a new state
// (gate press → ARMED, bar tick → RECORDING/LOOPING, drop → IDLE),
// any active knob vis gets cleared so the state LEDs are immediately
// visible. Knob vis lingering from a recent turn shouldn't obscure a
// state change the user just triggered.
{
    LoopState now_state = looper.GetState();
    if(now_state != prev_loop_state)
    {
        knob_vis_timer  = 0;
        knob_vis_which  = -1;
        xfade_vis_timer = 0;
    }
    prev_loop_state = now_state;
}

// ── Audio ──────────────────────────────────────────────────────────
for(size_t i = 0; i < size; i++)
{
    float in_l = in[0][i];
    float in_r = in[1][i];

    if(startup_mute > 0)
    {
        startup_mute--;
        out[0][i] = 0.0f;
        out[1][i] = 0.0f;
        continue;
    }

    // Clock — hw.Gate() exactly like Hillside
    bool gate_now = hw.Gate();
    bool rising = gate_now && !prev_gate;
    prev_gate = gate_now;

    ppqn.bar_length = 4;
    ppqn.Process(rising);

    if(ppqn.has_clock)
        looper.SetBeatPeriod(ppqn.GetBeatPeriod());

    if(ppqn.bar_tick)
        looper.OnBarTick();

    // (VU compute moved to end-of-chain — see post-output block below.)

    // Looper
    float live_l, live_r, loop_l, loop_r;
    bool loop_on = looper.Process(in_l, in_r,
                                   live_l, live_r,
                                   loop_l, loop_r);

    // Loop just engaged — reset pitch shifter so any old pitched
    // content in its FIFOs doesn't leak into the new loop's first 16ms.
    if(loop_on && !prev_loop_on)
    {
        pitch_l.ResetForDiscontinuity();
        pitch_r.ResetForDiscontinuity();
    }
    prev_loop_on = loop_on;

    // Pitch shift loop (only when shifted AND loop is long enough)
    if(loop_on)
    {
        float pf = looper.GetPitchFactor();
        if(fabsf(pf - 1.0f) > 0.01f)
        {
            loop_l = pitch_l.Process(loop_l);
            loop_r = pitch_r.Process(loop_r);
        }
    }

    // ── EFFECT SOURCE SELECTION ──
    // The effect processes whichever signal is on the side KNOB_0 selected:
    //   fx_on_loop=true  → effect input is loop_l/r (default; trails the
    //                      loop as you crossfade left)
    //   fx_on_loop=false → effect input is live_l/r (trails the live track
    //                      as you crossfade right)
    float fx_in_l = fx_on_loop ? loop_l : live_l;
    float fx_in_r = fx_on_loop ? loop_r : live_r;

    // Reverb tail — fed from the selected source.
    float rev_l, rev_r;
    fx_reverb.Process(fx_in_l, fx_in_r, &rev_l, &rev_r);

    // Delay tap + write — feedback governed by fx_delay_feedback (set
    // via SHIFT+K0, persists across releases). Input latches when the
    // crossfader leaves the SOURCE SIDE so existing repeats decay out.
    //   fx_on_loop=true  → latch when xfade < 0.25 (leaving loop)
    //   fx_on_loop=false → latch when xfade > 0.75 (leaving live)
    bool want_latch = fx_on_loop ? (xfade < 0.25f) : (xfade > 0.75f);
    delay_latched   = want_latch;

    int rd = fx_delay_wr - fx_delay_time;
    if(rd < 0) rd += FX_DELAY_SIZE;
    float dly_l = fx_delay_l[rd];
    float dly_r = fx_delay_r[rd];

    // Input gate: silent when latched, source signal otherwise. Feedback always.
    float in_to_delay_l = delay_latched ? 0.0f : fx_in_l;
    float in_to_delay_r = delay_latched ? 0.0f : fx_in_r;
    fx_delay_l[fx_delay_wr] = in_to_delay_l + dly_l * fx_delay_feedback;
    fx_delay_r[fx_delay_wr] = in_to_delay_r + dly_r * fx_delay_feedback;
    fx_delay_wr++;
    if(fx_delay_wr >= FX_DELAY_SIZE) fx_delay_wr = 0;

    // ── WET PATH (filter inserts) ──
    // Process a COPY of the selected source signal at full level. The
    // dry path continues into the crossfade unmodified. This is the key
    // to keeping wet punchy through the transition: filter input stays
    // hot regardless of crossfade position.
    float wet_l = 0.0f;
    float wet_r = 0.0f;
    if(loop_on && (fx_mode == FX_LP || fx_mode == FX_HP))
    {
        wet_l = fx_in_l;
        wet_r = fx_in_r;
        if(fx_mode == FX_LP)
            fx_lp.ProcessStereo(wet_l, wet_r);
        else
            fx_hp.ProcessStereo(wet_l, wet_r);
    }

    // ── CROSSFADE: equal-power with bias toward loop side ──
    //
    // Linear crossfade has a +3dB midpoint bump (incoherent signals
    // sum louder at 50%) and the live signal comes in too aggressive
    // too early. Equal-power curves (sin/cos) keep perceived loudness
    // flat. Plus a bias: live signal stays quieter for the first 30%
    // of the throw, giving the loop + FX time to dominate.
    //
    //   xfade = 1.0 (right) → loop full, live silent
    //   xfade = 0.7         → loop ~0.85, live ~0.20  (FX stage time)
    //   xfade = 0.5         → loop ~0.71, live ~0.50  (mid-throw)
    //   xfade = 0.3         → loop ~0.45, live ~0.85
    //   xfade = 0.0 (left)  → loop silent, live full
    //
    // Loop curve: pure equal-power cos(π/2 × (1-xfade))
    // Live curve: equal-power sin(π/2 × xfade) with delayed onset
    //   — first 25% of throw, live stays near silent
    //   — biased so loop + FX have room to breathe before live hits.
    float loop_gain, live_gain;
    if(loop_on)
    {
        // Equal-power loop fade (stays loud through the throw)
        float loop_phase = (1.0f - xfade) * 1.5707963f;   // π/2
        loop_gain = cosf(loop_phase);

        // Live ramp-in with bias — quieter early, ramps up after 25%
        // Maps xfade ∈ [1.0, 0.0] → live_t ∈ [0, 1] but skewed:
        //   xfade ≥ 0.75 → live_t = 0..0.10  (heavily attenuated)
        //   xfade < 0.75 → live_t ramps to 1 with sin curve
        float live_t;
        if(xfade >= 0.75f)
            live_t = (1.0f - xfade) * 0.40f;     // 0..0.10 over first 25%
        else
            live_t = 0.10f + (0.75f - xfade) * (0.90f / 0.75f);
        if(live_t > 1.0f) live_t = 1.0f;
        live_gain = sinf(live_t * 1.5707963f);
    }
    else
    {
        loop_gain = 0.0f;
        live_gain = 1.0f;     // No loop: live always at full
    }

    float out_l, out_r;
    if(loop_on)
    {
        out_l = live_l * live_gain + loop_l * loop_gain;
        out_r = live_r * live_gain + loop_r * loop_gain;
    }
    else
    {
        out_l = live_l;
        out_r = live_r;
    }

    // ── WET ADD (post-crossfade) ──
    // Filter wet, reverb wet, delay wet — all ride around the crossfader
    // at full punch, scaled by wet_gain (= KNOB_0 × envelope). Means
    // the effect can carry through the transition even after the dry
    // loop has faded out. Classic DJ "filter throw" + "echo out" feel.
    if(fx_mode == FX_LP || fx_mode == FX_HP)
    {
        out_l += wet_l * wet_gain;
        out_r += wet_r * wet_gain;
    }
    else if(fx_mode == FX_REV)
    {
        out_l += rev_l * wet_gain;
        out_r += rev_r * wet_gain;
    }
    else if(fx_mode == FX_DLY)
    {
        // Delay wet curve — louder through the throw, then echo-out fade.
        // Side-aware: distance d from the "home" extreme (where the
        // effect is silent) drives the curve regardless of which side
        // KNOB_0 selected.
        //
        //   d ∈ [0.00, 0.05]     → 0          (home deadzone)
        //   d ∈ [0.05, 0.75]     → ramp 0 → 1.5  (build through throw)
        //   d ∈ [0.75, 1.00]     → fade 1.5 → 0  (echo-out at far end)
        float d_dly = fx_on_loop ? (1.0f - xfade) : xfade;
        float dly_env;
        if(d_dly <= 0.05f)
            dly_env = 0.0f;
        else if(d_dly <= 0.75f)
            dly_env = 1.5f * (d_dly - 0.05f) / 0.70f;
        else
            dly_env = 1.5f * (1.0f - d_dly) / 0.25f;
        float dly_gain = fx_wet_amount * dly_env;
        if(shift_mode) dly_gain = fx_wet_amount * 1.5f;   // SHIFT: full punch
        out_l += dly_l * dly_gain;
        out_r += dly_r * dly_gain;
    }

    // END OF CHAIN VOLUME — clean linear up to 1.0, soft sat above.
    out_l *= vol;
    out_r *= vol;
    if(vol > 1.0f)
    {
        out_l = SoftSat(out_l);
        out_r = SoftSat(out_r);
    }

    // ── VU METER (post-volume, used for IDLE LED display) ──
    // Reflects what's actually going to the DAC — so the volume knob
    // affects the meter, transient claps don't spike harder than
    // sustained kicks. Smooth attack (~30ms) tames transients;
    // moderate release (~250ms) keeps the meter readable.
    {
        float p = fabsf(out_l);
        float pr = fabsf(out_r);
        if(pr > p) p = pr;
        // Smoothed attack — transients integrated over ~30ms instead
        // of slamming the meter to peak in one sample.
        if(p > vu_level) vu_level += (p - vu_level) * 0.0007f;
        else             vu_level *= 0.99991f;  // ~250ms release @ 48k
    }

    out[0][i] = out_l;
    out[1][i] = out_r;
}

block_counter++;
}

// ═══════════════════════════════════════════════════════════════════════════
// LEDs
// ═══════════════════════════════════════════════════════════════════════════

static void UpdateLEDs()
{
if(knob_vis_timer > 0) knob_vis_timer--;
if(xfade_vis_timer > 0) xfade_vis_timer--;
if(error_flash > 0) error_flash--;
LoopState st = looper.GetState();

// Error flash
if(error_flash > 0)
{
    bool on = ((error_flash / 8000) % 2) == 0;
    float v = on ? 0.8f : 0.0f;
    for(int i = 0; i < 4; i++)
        hw.SetLed(i, v, 0.0f, 0.0f);
}
else if(st == LoopState::IDLE)
{
    // VU meter — green → yellow → red as signal level rises.
    //
    // Calibrated for modular line-level signals:
    //   amplitude 0.30 (low) → 1 LED green
    //   amplitude 0.60 (mod) → 2 LEDs green
    //   amplitude 0.85 (hot) → 3 LEDs (yellow)
    //   amplitude 1.00 (uni) → 3.5 LEDs (yellow → red transition)
    //   amplitude > 1.0      → 4 LEDs red (clipping warning)
    //
    // Volume knob now affects this since the VU samples post-volume.
    float v = vu_level * 3.5f;
    if(v > 4.0f) v = 4.0f;
    for(int i = 0; i < 4; i++)
    {
        float headroom = v - static_cast<float>(i);
        float intensity;
        if(headroom >= 1.0f)      intensity = 1.0f;
        else if(headroom > 0.0f)  intensity = headroom;
        else                      intensity = 0.0f;

        // Color stops:
        //   LED 0 → bright green
        //   LED 1 → green-yellow
        //   LED 2 → yellow-orange
        //   LED 3 → red
        float r, g;
        if(i == 0)        { r = 0.0f;             g = 0.95f * intensity; }
        else if(i == 1)   { r = 0.30f * intensity; g = 0.95f * intensity; }
        else if(i == 2)   { r = 0.85f * intensity; g = 0.55f * intensity; }
        else              { r = 1.0f * intensity;  g = 0.0f; }
        hw.SetLed(i, r, g, 0.0f);
    }
}
else if(st == LoopState::ARMED)
{
    // Hard blink yellow/orange — ~2Hz so it's unmistakable
    // block_counter ticks ~187 Hz at 48k/256, so /48 → ~250ms phases
    bool on = ((block_counter / 48) % 2) == 0;
    float r = on ? 1.0f : 0.05f;
    float g = on ? 0.5f : 0.02f;  // yellow-orange = R full, G half
    for(int i = 0; i < 4; i++)
        hw.SetLed(i, r, g, 0.0f);
}
else if(st == LoopState::RECORDING)
{
    float prog = looper.GetRecordProgress();
    for(int i = 0; i < 4; i++)
    {
        float s = static_cast<float>(i) / 4.0f;
        float e = static_cast<float>(i + 1) / 4.0f;
        if(prog >= e)
            hw.SetLed(i, 0.0f, 0.7f, 0.0f);
        else if(prog > s)
            hw.SetLed(i, 0.0f, 0.08f + ((prog-s)/0.25f)*0.62f, 0.0f);
        else
            hw.SetLed(i, 0, 0, 0);
    }
}
else if(st == LoopState::LOOPING)
{
    float prog = looper.GetPlayProgress();
    int active = static_cast<int>(prog * 4.0f);
    if(active > 3) active = 3;
    for(int i = 0; i < 4; i++)
    {
        if(i == active)
            hw.SetLed(i, 0.0f, 0.85f, 0.0f);   // bright green playhead
        else
            hw.SetLed(i, 0.0f, 0.04f, 0.0f);   // dim green trail
    }
}

// ── SHIFT-PENDING OVERLAY ──
// While button is held in LOOPING but hasn't triggered SHIFT yet,
// fade in a blue tint as the hold approaches threshold. Visual
// confirmation "yes, I see you holding, keep going."
if(hw.tap.Pressed() && !shift_mode
   && looper.GetState() == LoopState::LOOPING)
{
    float held_ms = static_cast<float>(hw.tap.TimeHeldMs());
    float progress = held_ms / 250.0f;
    if(progress > 1.0f) progress = 1.0f;
    // Only paint once we're past 30% — first chunk is "just a tap" zone.
    if(progress > 0.30f)
    {
        float t = (progress - 0.30f) / 0.70f;   // 0..1 over remaining 70%
        for(int i = 0; i < 4; i++)
            hw.SetLed(i, 0.0f, 0.10f * (1.0f - t), 0.85f * t);
    }
}

// ── SHIFT MODE OVERLAY ──
// Once held past the threshold, paint all 4 LEDs with a slow blue
// pulse — "knobs are doing secondary stuff right now."
if(shift_mode)
{
    float pulse = 0.45f + 0.30f
                * sinf(static_cast<float>(block_counter) * 0.05f);
    for(int i = 0; i < 4; i++)
        hw.SetLed(i, 0.0f, pulse * 0.25f, pulse);
}

// Knob vis
if(knob_vis_timer > 0 && knob_vis_which >= 0)
{
    float fade = (knob_vis_timer > VIS_FADE_TICKS)
                  ? 1.0f
                  : static_cast<float>(knob_vis_timer)
                    / static_cast<float>(VIS_FADE_TICKS);
    float pos = cur_knob[knob_vis_which];

    // ── KNOB_4: EFFECT SELECT — one of four LEDs lit teal ──
    if(knob_vis_which == 4)
    {
        int fx = EffectModeFromKnob(pos);
        for(int i = 0; i < 4; i++)
        {
            if(i == fx)
                hw.SetLed(i, 0.0f, 0.7f * fade, 0.7f * fade);   // teal
            else
                hw.SetLed(i, 0.0f, 0.05f * fade, 0.05f * fade); // dim teal
        }
    }
    // ── KNOB_2: DIVIDER — purple bar that shrinks as you divide down ──
    else if(knob_vis_which == 2)
    {
        float led_count = looper.GetDivideLEDCount();
        for(int i = 0; i < 4; i++)
        {
            float remaining = led_count - static_cast<float>(i);
            float v;
            if(remaining >= 1.0f)      v = 1.0f;
            else if(remaining > 0.0f)  v = remaining;
            else                       v = 0.0f;
            hw.SetLed(i, v * fade * 0.9f, 0.0f, v * fade * 0.9f);
        }
    }
    // ── KNOB_3 / KNOB_5: Pitch / speed — green crossfader-style ──
    else if(knob_vis_which == 3 || knob_vis_which == 5)
    {
        float b[4] = { 0, 0, 0, 0 };
        if(pos < 0.2f)       b[0] = 0.9f;
        else if(pos < 0.4f)  b[1] = 0.9f;
        else if(pos < 0.6f)  { b[1] = 0.9f; b[2] = 0.9f; }
        else if(pos < 0.8f)  b[2] = 0.9f;
        else                 b[3] = 0.9f;
        for(int i = 0; i < 4; i++)
            hw.SetLed(i, 0.0f, b[i] * fade, 0.0f);
    }
    // ── KNOB_0 (effect strength + side): bipolar vis ──
    // Center → all dim. Left of center → LEDs 0,1 light cyan-ish (live
    // side). Right of center → LEDs 2,3 light magenta-ish (loop side).
    // Brightness scales with deflection magnitude.
    else if(knob_vis_which == 0)
    {
        float bipolar = pos - 0.5f;
        float mag;
        if(fabsf(bipolar) < 0.05f)
            mag = 0.0f;
        else
            mag = (fabsf(bipolar) - 0.05f) / 0.45f;
        if(mag > 1.0f) mag = 1.0f;
        // base dim glow on all 4 so the bipolar layout is visible
        for(int i = 0; i < 4; i++)
            hw.SetLed(i, 0.05f * fade, 0.05f * fade, 0.05f * fade);
        if(bipolar > 0.05f)   // right → loop side (LEDs 2,3 magenta)
        {
            float v = mag * 0.8f * fade;
            hw.SetLed(2, v, 0.0f, v);
            hw.SetLed(3, v, 0.0f, v);
        }
        else if(bipolar < -0.05f)   // left → live side (LEDs 0,1 cyan)
        {
            float v = mag * 0.8f * fade;
            hw.SetLed(0, 0.0f, v, v);
            hw.SetLed(1, 0.0f, v, v);
        }
    }
    // ── KNOB_6 (volume): white fill bar ──
    else
    {
        for(int i = 0; i < 4; i++)
        {
            float e = static_cast<float>(i + 1) / 4.0f;
            float v;
            if(pos >= e) v = 0.8f;
            else if(pos > e - 0.25f)
                v = 0.05f + ((pos - (e - 0.25f)) / 0.25f) * 0.75f;
            else v = 0.05f;
            hw.SetLed(i, v * fade, v * fade, v * fade);
        }
    }
}

// Crossfader vis (white, highest priority)
if(xfade_vis_timer > 0)
{
    float fade = (xfade_vis_timer > VIS_FADE_TICKS)
                  ? 1.0f
                  : static_cast<float>(xfade_vis_timer)
                    / static_cast<float>(VIS_FADE_TICKS);
    float pos = cur_knob[1];
    float b[4] = { 0, 0, 0, 0 };
    if(pos < 0.2f)       b[0] = 0.9f;
    else if(pos < 0.4f)  b[1] = 0.9f;
    else if(pos < 0.6f)  { b[1] = 0.9f; b[2] = 0.9f; }
    else if(pos < 0.8f)  b[2] = 0.9f;
    else                 b[3] = 0.9f;
    for(int i = 0; i < 4; i++)
        hw.SetLed(i, b[i]*fade, b[i]*fade, b[i]*fade);
}

hw.UpdateLeds();
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(void)
{
hw.Init(true);
hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
hw.SetAudioBlockSize(AUDIO_BLOCK);
float sr = hw.AudioSampleRate();
// Startup animation
for(int r = 0; r < 5; r++)
{
    for(int i = 0; i < 4; i++) hw.SetLed(i, 0, 0, 0);
    hw.UpdateLeds(); hw.DelayMs(20);
}
hw.DelayMs(100);
for(int r = 0; r < 10; r++)
{
    hw.SetLed(0, .7f,.7f,.7f); hw.SetLed(1, 0,0,0);
    hw.SetLed(2, 0,0,0);       hw.SetLed(3, .7f,.7f,.7f);
    hw.UpdateLeds(); hw.DelayMs(25);
}
for(int r = 0; r < 10; r++)
{
    for(int i = 0; i < 4; i++) hw.SetLed(i, .7f,.7f,.7f);
    hw.UpdateLeds(); hw.DelayMs(25);
}
for(int r = 0; r < 12; r++)
{
    for(int i = 0; i < 4; i++) hw.SetLed(i, 1,1,1);
    hw.UpdateLeds(); hw.DelayMs(25);
}
for(int s = 20; s >= 0; s--)
{
    float v = static_cast<float>(s) / 20.0f;
    for(int i = 0; i < 4; i++) hw.SetLed(i, v,v,v);
    hw.UpdateLeds(); hw.DelayMs(40);
}
hw.DelayMs(100);

// Init
looper.Init(loop_buf_l, loop_buf_r, MAX_LOOP_SAMPLES, sr);
pitch_l.Init(sr);
pitch_r.Init(sr);

// Effects
fx_lp.Init(sr);
fx_hp.Init(sr);
fx_reverb.Init(sr);
fx_reverb.SetFeedback(0.86f);   // longer tail — feeds shimmer well
fx_reverb.SetDamping(0.55f);    // moderately smeary
fx_reverb.SetShimmer(0.0f);     // off by default; SHIFT+K0 to engage
memset(fx_delay_l, 0, sizeof(fx_delay_l));
memset(fx_delay_r, 0, sizeof(fx_delay_r));
fx_delay_wr   = 0;
fx_delay_time = 12000;

// Start — EXACT same order as Hillside
hw.StartAdc();
hw.StartAudio(AudioCallback);

while(1)
{
    UpdateLEDs();
    System::Delay(1);
}
}