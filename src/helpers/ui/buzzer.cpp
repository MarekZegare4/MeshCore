#include "Arduino.h"
#ifdef PIN_BUZZER
#include "buzzer.h"

void genericBuzzer::begin() {
    // No real GPIO pin to configure in the sim -- PIN_BUZZER there is just a
    // dummy sentinel value so the #ifdef PIN_BUZZER guards elsewhere (this
    // file included) activate at all; variants/sim/arduino/Arduino.h
    // deliberately has no pinMode()/digitalWrite() shim since nothing else
    // ever needed one before this.
#ifndef SIM_PLATFORM
    #ifdef PIN_BUZZER_EN
      pinMode(PIN_BUZZER_EN, OUTPUT);
      digitalWrite(PIN_BUZZER_EN, HIGH);
    #endif
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); // need to pull low by default to avoid extreme power draw
#endif
#if defined(NRF52_PLATFORM)
    _isr_instance = this;
    NRF_TIMER1->TASKS_STOP  = 1;
    NRF_TIMER1->MODE        = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;
    NRF_TIMER1->BITMODE     = TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos;
    NRF_TIMER1->PRESCALER   = 4;   // 16 MHz / 2^4 = 1 MHz -> 1 us/tick
    NRF_TIMER1->SHORTS      = TIMER_SHORTS_COMPARE0_CLEAR_Msk | TIMER_SHORTS_COMPARE0_STOP_Msk;
    NRF_TIMER1->INTENSET    = TIMER_INTENSET_COMPARE0_Msk;
    // Lowest application priority: this only ever reschedules a tone, it must
    // never contend with anything radio/BLE-timing-critical.
    NVIC_SetPriority(TIMER1_IRQn, 7);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_EnableIRQ(TIMER1_IRQn);
#endif
    startup();
}

void genericBuzzer::quiet(bool buzzer_state) {
    _is_quiet = buzzer_state;
#ifdef PIN_BUZZER_EN
    digitalWrite(PIN_BUZZER_EN, _is_quiet ? LOW : HIGH);
#endif
}

bool genericBuzzer::isQuiet() { return _is_quiet; }

void genericBuzzer::startup()  { play(startup_song); }
void genericBuzzer::shutdown() { play(shutdown_song); }

// ---------------------------------------------------------------------------
// Shared RTTTL parser -- pure string/arithmetic, no hardware access, so both
// the NRF52 direct-PWM player and the sim's poll-only player (below) reuse
// it verbatim instead of each carrying their own copy.
// ---------------------------------------------------------------------------
#if defined(NRF52_PLATFORM) || defined(SIM_PLATFORM)

// Chromatic frequencies for octave 4 (Hz): C C# D D# E F F# G G# A A# B
static const uint16_t CHROM4[12] = { 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494 };

// Map 'a'-'g' → chromatic index within octave
static const uint8_t NOTE_IDX[7] = { 9, 11, 0, 2, 4, 5, 7 };  // a b c d e f g

uint16_t genericBuzzer::_noteFreq(char letter, bool sharp, uint8_t octave) {
    if (letter == 'p') return 0;
    if (letter < 'a' || letter > 'g') return 0;
    uint8_t idx = NOTE_IDX[letter - 'a'];
    if (sharp) { if (++idx >= 12) { idx = 0; octave++; } }
    if (octave < 4) octave = 4;
    if (octave > 8) octave = 8;   // parser accepts octaves 4-8; B8 (~7.9 kHz) is within range
    uint32_t f = (uint32_t)CHROM4[idx] << (octave - 4);
    return (uint16_t)(f > 25000 ? 25000 : f);
}

void genericBuzzer::_parseHeader(const char* melody, uint8_t& def_dur, uint8_t& def_oct,
                                  uint16_t& bpm, const char*& notes) {
    def_dur = 4; def_oct = 5; bpm = 120;
    const char* p = melody;
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p && *p != ':') {
        while (*p == ' ' || *p == ',') p++;
        if      (p[0]=='d' && p[1]=='=') { p+=2; def_dur=(uint8_t)atoi(p); while(*p&&*p!=','&&*p!=':')p++; }
        else if (p[0]=='o' && p[1]=='=') { p+=2; def_oct=(uint8_t)atoi(p); while(*p&&*p!=','&&*p!=':')p++; }
        else if (p[0]=='b' && p[1]=='=') { p+=2; bpm=(uint16_t)atoi(p);    while(*p&&*p!=','&&*p!=':')p++; }
        else { while(*p&&*p!=','&&*p!=':')p++; }
    }
    if (*p == ':') p++;
    notes = p;
}

bool genericBuzzer::_parseNext(const char*& p, uint8_t def_dur, uint8_t def_oct, uint16_t bpm,
                                uint16_t& freq, uint32_t& dur_ms) {
    while (*p == ' ' || *p == ',') p++;
    if (*p == '\0') return false;

    uint8_t dur = def_dur;
    if (*p >= '0' && *p <= '9') { dur=(uint8_t)atoi(p); while(*p>='0'&&*p<='9')p++; }
    if (dur == 0) dur = 4;

    if (*p == '\0') return false;
    char note = *p++;
    bool sharp = (*p == '#') ? (p++, true) : false;
    uint8_t oct = def_oct;
    if (*p >= '4' && *p <= '8') oct = (uint8_t)(*p++ - '0');
    bool dot = (*p == '.') ? (p++, true) : false;

    dur_ms = (60000UL * 4UL) / ((uint32_t)bpm * dur);
    if (dot) dur_ms = dur_ms * 3 / 2;

    freq = _noteFreq(note, sharp, oct);
    return true;
}

#endif // NRF52_PLATFORM || SIM_PLATFORM

// ---------------------------------------------------------------------------
// nRF52 path — direct NRF_PWM2 control, bypasses tone()
// ---------------------------------------------------------------------------
#if defined(NRF52_PLATFORM)

uint8_t genericBuzzer::_dutyPct() const {
    // Inverted polarity (0x8000 bit): duty_HIGH = 100% - PCT.
    // Values chosen for ~6-8 dB perceptual steps: -24/-16/-9/-3/0 dB.
    static const uint8_t PCT[5] = { 2, 5, 12, 25, 50 };
    return PCT[_volume_level < 5 ? _volume_level : 4];
}

void genericBuzzer::_nrfStartPwm(uint16_t freq) {
    if (freq < 20 || freq > 25000) { _nrfStopPwm(); return; }

    uint32_t nrf_pin = g_ADigitalPinMap[PIN_BUZZER];
    uint16_t top = 125000 / freq;
    uint16_t cmp = (uint16_t)(((uint32_t)top * _dutyPct()) / 100);
    if (cmp == 0) cmp = 1;  // inverted polarity: cmp=0 → 100% HIGH → no AC → silence

    // Write duty BEFORE SEQSTART so DMA reads our value on the very first period
    _duty_buf = 0x8000U | cmp;
    __DMB();

    // Only wait for STOPPED if PWM was actually running — TASKS_STOP on a disabled
    // PWM never fires EVENTS_STOPPED, so the wait would always time out at 2 ms.
    if (_pwm_on) {
      NRF_PWM2->TASKS_STOP = 1;
      uint32_t t = millis();
      while (!(NRF_PWM2->EVENTS_STOPPED) && (millis() - t) < 2) {}
      NRF_PWM2->EVENTS_STOPPED = 0;
    }

    NRF_PWM2->PSEL.OUT[0] = nrf_pin;
    NRF_PWM2->PSEL.OUT[1] = 0xFFFFFFFFUL;
    NRF_PWM2->PSEL.OUT[2] = 0xFFFFFFFFUL;
    NRF_PWM2->PSEL.OUT[3] = 0xFFFFFFFFUL;
    NRF_PWM2->ENABLE      = PWM_ENABLE_ENABLE_Enabled << PWM_ENABLE_ENABLE_Pos;
    NRF_PWM2->MODE        = PWM_MODE_UPDOWN_Up << PWM_MODE_UPDOWN_Pos;
    // DIV_128 on 16 MHz = 125 kHz — same clock as tone(), so same frequency math
    NRF_PWM2->PRESCALER   = PWM_PRESCALER_PRESCALER_DIV_128 << PWM_PRESCALER_PRESCALER_Pos;
    NRF_PWM2->COUNTERTOP  = top;
    NRF_PWM2->DECODER     = (PWM_DECODER_LOAD_Common     << PWM_DECODER_LOAD_Pos) |
                             (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
    NRF_PWM2->SHORTS      = PWM_SHORTS_LOOPSDONE_SEQSTART0_Msk;
    NRF_PWM2->LOOP        = 0xFFFFUL << PWM_LOOP_CNT_Pos;

    // Both SEQ0 and SEQ1 point to the same buffer; REFRESH=0 means DMA re-reads every period
    NRF_PWM2->SEQ[0].PTR      = (uint32_t)&_duty_buf;
    NRF_PWM2->SEQ[0].CNT      = 1;
    NRF_PWM2->SEQ[0].REFRESH  = 0;
    NRF_PWM2->SEQ[0].ENDDELAY = 0;
    NRF_PWM2->SEQ[1].PTR      = (uint32_t)&_duty_buf;
    NRF_PWM2->SEQ[1].CNT      = 1;
    NRF_PWM2->SEQ[1].REFRESH  = 0;
    NRF_PWM2->SEQ[1].ENDDELAY = 0;

    NRF_PWM2->TASKS_SEQSTART[0] = 1;
    _pwm_on = true;
}

void genericBuzzer::_nrfStopPwm() {
    NRF_PWM2->TASKS_STOP  = 1;
    NRF_PWM2->PSEL.OUT[0] = 0xFFFFFFFFUL;
    NRF_PWM2->ENABLE      = 0;
    digitalWrite(PIN_BUZZER, LOW);
    _pwm_on = false;
}

genericBuzzer* genericBuzzer::_isr_instance = nullptr;

void genericBuzzer::_armNoteTimer(uint32_t dur_ms) {
    NRF_TIMER1->TASKS_STOP  = 1;
    NRF_TIMER1->TASKS_CLEAR = 1;
    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
    NRF_TIMER1->CC[0] = dur_ms * 1000UL;   // 1 us/tick (see PRESCALER in begin())
    NRF_TIMER1->TASKS_START = 1;
}

// Halt the timer AND drop any interrupt it already latched. Stopping the timer
// and clearing EVENTS_COMPARE[0] de-asserts the IRQ source, but an interrupt
// the NVIC latched just before we stopped stays pending and would fire one
// spurious _nrfAdvance() after we return — skipping the first note of a new
// melody, or sounding a blip just after an explicit stop. Order matters: clear
// the event (with a read-back to flush the write buffer, per the nRF52 event
// anomaly) before clearing the NVIC, else the still-set event re-latches it.
void genericBuzzer::_disarmNoteTimer() {
    NRF_TIMER1->TASKS_STOP = 1;
    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
    (void)NRF_TIMER1->EVENTS_COMPARE[0];
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
}

// Static member (not a free function) so it can reach private state without a
// friend declaration — same trick MomentaryButton's isrTrampolineN() uses.
// The real ISR (TIMER1_IRQHandler, below) is just a one-line dispatch to this.
void genericBuzzer::_timer1ISR() {
    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
    (void)NRF_TIMER1->EVENTS_COMPARE[0];   // flush write buffer so the IRQ doesn't immediately re-fire (nRF52 anomaly)
    if (_isr_instance) _isr_instance->_nrfAdvance();
}

void genericBuzzer::_nrfBegin(const char* melody) {
    _disarmNoteTimer();   // drop any in-flight/pending note advance before reconfiguring
    _nrfStopPwm();
    if (!melody || !*melody) { _rtttl_done = true; return; }
    const char* notes;
    _parseHeader(melody, _def_dur, _def_oct, _def_bpm, notes);
    _rtttl_pos  = notes;
    _rtttl_done = false;
    _nrfAdvance();
}

void genericBuzzer::_nrfAdvance() {
    uint16_t freq; uint32_t dur_ms;
    if (_parseNext(_rtttl_pos, _def_dur, _def_oct, _def_bpm, freq, dur_ms)) {
        _armNoteTimer(dur_ms);
        if (freq > 0) _nrfStartPwm(freq); else _nrfStopPwm();
    } else {
        _nrfStopPwm();
        _rtttl_done = true;
    }
}

void genericBuzzer::applyVolume() {
    if (!_pwm_on) return;
    uint16_t top = (uint16_t)NRF_PWM2->COUNTERTOP;
    uint16_t cmp = (uint16_t)(((uint32_t)top * _dutyPct()) / 100);
    if (cmp == 0) cmp = 1;
    _duty_buf = 0x8000U | cmp;  // DMA picks this up within one period (< 2.3 ms at A4)
}

void genericBuzzer::play(const char* melody) {
    if (_is_quiet) return;
    _nrfBegin(melody);
}

void genericBuzzer::playForced(const char* melody) {
    _nrfBegin(melody);
}

bool genericBuzzer::isPlaying() { return !_rtttl_done; }

void genericBuzzer::stop() {
    _disarmNoteTimer();   // ensure no latched note-advance fires after we stop
    _nrfStopPwm();
    _rtttl_done = true;
}

// No-op: TIMER1's compare interrupt (_timer1ISR -> _nrfAdvance) now drives
// note advancement directly, so timing no longer depends on how often (or
// whether) the caller's loop() gets to run. Kept as a real method, not
// removed, since UITask polls buzzer.loop() unconditionally for both
// platforms.
void genericBuzzer::loop() {}

extern "C" void TIMER1_IRQHandler(void) {
    genericBuzzer::_timer1ISR();
}

void genericBuzzer::setVolume(uint8_t level) {
    _volume_level = level < 5 ? level : 4;
    applyVolume();
}

// ---------------------------------------------------------------------------
// Sim path — no real PWM/timer hardware; just track (freq, note-end-time)
// and let loop() poll millis() to advance, same non-blocking shape as the
// NonBlockingRtttl path below minus the library. A host page polls
// currentFreqHz()/isPlaying() every frame to drive a Web Audio oscillator
// (see variants/sim/web/index.html) instead of sounding real hardware.
// ---------------------------------------------------------------------------
#elif defined(SIM_PLATFORM)

void genericBuzzer::_advance() {
    uint16_t freq; uint32_t dur_ms;
    if (_parseNext(_rtttl_pos, _def_dur, _def_oct, _def_bpm, freq, dur_ms)) {
        _cur_freq = freq;
        _note_end_ms = millis() + dur_ms;
    } else {
        _cur_freq = 0;
        _rtttl_done = true;
    }
}

void genericBuzzer::applyVolume() {
    // No hardware duty cycle to touch -- the host page maps getVolume()
    // (0-4) to a Web Audio gain value itself.
}

void genericBuzzer::play(const char* melody) {
    if (_is_quiet) return;
    playForced(melody);
}

void genericBuzzer::playForced(const char* melody) {
    _rtttl_done = true;
    _cur_freq = 0;
    if (!melody || !*melody) return;
    const char* notes;
    _parseHeader(melody, _def_dur, _def_oct, _def_bpm, notes);
    _rtttl_pos  = notes;
    _rtttl_done = false;
    _advance();
}

bool genericBuzzer::isPlaying() { return !_rtttl_done; }

void genericBuzzer::stop() {
    _rtttl_done = true;
    _cur_freq = 0;
}

void genericBuzzer::loop() {
    if (_rtttl_done) return;
    if ((int32_t)(millis() - _note_end_ms) >= 0) _advance();
}

void genericBuzzer::setVolume(uint8_t level) {
    _volume_level = level < 5 ? level : 4;
}

#else  // NRF52_PLATFORM / SIM_PLATFORM

// ---------------------------------------------------------------------------
// Non-nRF52, non-sim path — NonBlockingRtttl + analogWrite for volume
// ---------------------------------------------------------------------------

void genericBuzzer::applyVolume() {
    // After tone() sets 50% duty, analogWrite overrides duty on the same PWM channel.
    static const uint8_t duty[5] = { 6, 20, 50, 90, 128 };
    uint8_t d = duty[_volume_level < 5 ? _volume_level : 4];
    if (d < 128) analogWrite(PIN_BUZZER, d);
}

void genericBuzzer::play(const char* melody) {
    if (isPlaying()) rtttl::stop();
    if (_is_quiet) return;
    rtttl::begin(PIN_BUZZER, melody);
}

void genericBuzzer::playForced(const char* melody) {
    if (isPlaying()) rtttl::stop();
    rtttl::begin(PIN_BUZZER, melody);
}

bool genericBuzzer::isPlaying() { return rtttl::isPlaying(); }

void genericBuzzer::stop() { rtttl::stop(); }

void genericBuzzer::loop() {
    if (!rtttl::done()) {
        rtttl::play();
        if (_volume_level < 4) applyVolume();
    }
}

void genericBuzzer::setVolume(uint8_t level) {
    _volume_level = level < 5 ? level : 4;
    if (isPlaying()) applyVolume();
}

#endif  // NRF52_PLATFORM / SIM_PLATFORM

#endif  // PIN_BUZZER
