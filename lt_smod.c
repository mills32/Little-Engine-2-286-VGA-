// minmod.c
// Minimal ProTracker MOD player for Turbo C 3.0, LARGE memory model.
// SB 8-bit DMA output, IRQ-driven.  80186/286.
//
//
// Simplifications:
//   - Every note plays at FULL volume: a channel is either playing or silent (the
//     sample ended).  There is no volume table, no volume column and no Cxx.
//   - The samples are attenuated once, when the MOD is loaded (signed 8 bit -> signed
//     6 bit, sample >> 2).  Four channels can then be added without any overflow.
//   - Effects: 9xx sample offset, Bxx position jump, Dxx pattern break, Fxx speed/tempo.
//
// Sound output (auto-init DMA, needs a Sound Blaster 2.0 or later):
//   - The NBUF buffers form a ring.  The DMA controller and the DSP loop over the ring by
//     themselves (DSP command 1Ch).  The sound card raises an IRQ at the end of every buffer,
//     the IRQ handler only counts them.
//   - Call mod_update() from the main loop, for example once per vertical retrace.  It mixes
//     the buffers that have been played.  It must be called at least once every
//     (NBUF-1) * BUF_SAMPLES / MIX_RATE seconds: 69 ms at 22 kHz, 190 ms at 8 kHz.
//     If it is called too late the ring plays old data once more; mod_update() notices
//     that (dma_underruns) and goes on with the next buffer.
//
// Mixer design (taken from Mod Master XT, MIXAGE.ASM, 8-bit path):
//   - Register layout while mixing:
//         DS:SI  sample segment : position   (the sample byte is added straight from memory)
//         ES:DI  output buffer
//         DX = fraction accumulator, CX = fraction increment, BP = integer increment
//         BX = 8080h (first channel only: xor flips the sign bits, signed -> unsigned PCM)
//     The DMA buffers and the mixer parameters live in ONE far segment (mix_seg).
//   - Every sample is copied into its own segment, so the position IS the offset
//     (no base add).  The tail of each sample is padded with PAD bytes: a copy of
//     the loop start for looping samples, zeros for one-shots.  Because of the
//     padding the mixer may run past the loop end by up to one block, so the
//     loop test runs once per BLOCK samples instead of once per sample.
//   - Two samples per iteration, one word store (stosw) per two samples.
//   - The first channel of a chunk uses MOV (no buffer read, no buffer clear), the
//     other channels use ADD.  The first channel also flips the sign bit of every byte
//     (xor 8080h), so the buffer comes out as unsigned 8 bit PCM: no conversion pass.
//     (Adding the other channels as signed bytes keeps the 80h offset intact.)
//   - Mixing goes straight into the DMA buffer, there is no copy.
//   - The mixing loop is fully unrolled (BLOCK samples) and works on whole blocks.
//     Tracker ticks are scheduled on block boundaries (error is carried over,
//     so the tempo stays exact; timing jitter is at most BLOCK samples).
//   - One-shot samples are stopped by C code (exact multiply-compare test),
//     the mixer itself never tests for the end of a one-shot sample.
//   - Adaptive channel limiting (when the CPU cannot keep up):
//       Only LOOPING non-fresh sustained notes are ever culled, oldest first.
//       Fresh notes (age == 0) are always mixed.
//       Non-looping one-shots are always mixed.
//   - Culled (looping) channels advance silently so they stay in phase.
//   - Same-sample retrigger shortcut, 9xx sample offset consumed on next trigger.
//   - The complete MOD file (header + patterns + samples) must be smaller than 64 KB.
//     It is loaded into ONE block (mod_data); patterns and samples are used in place.
//     The only change to the data is that every sample is followed by PAD bytes, so each
//     sample starts on a paragraph boundary and can be addressed by its own segment.

#include "lt__eng.h"
#include <dos.h>

void LT_memset(void *ptr, byte val, word number);
void LT_Error(char *error, char *file);
void far *LT_calloc0(word para);
void LT_free(void far* ptr);
void interrupt (far * LT_getvect(byte intr))();
void LT_setvect(byte intr, void interrupt (far *func)());
word LT_fopen(unsigned char *file, byte mode);
void LT_fread(word file_handle,word bytes,void *buffer);
unsigned long LT_fseek(word file_handle,unsigned long bytes,byte origin);
void LT_fclose(word file_handle);
extern byte far *LT_music_sdata;
extern byte LT_MUSIC_ENABLE;
extern byte LT_MUSIC_MODE;
extern byte LT_MUSIC_LOADED;

//do not change
#define NUM_CHANNELS 4
#define BUF_SAMPLES  256
#define NBUF         8
#define BLOCK        64
#define BLOCK_SHIFT  6
#define BUF_BLOCKS   (BUF_SAMPLES / BLOCK)

#define SB_BASE      0x220
#define SB_DMA_CH    1
#define SB_IRQ       7

#define MIN_PERIOD   57
#define MAX_PERIOD   1712

#define PAD          (NBUF*64)+32

#define SLOW_TRIGGER   8
#define SLOW_RELEASE   4

// Layout of the shared mixing segment (mix_seg).  The numbers are written out
// because they are also used inside asm blocks; the typedefs below check them.
//   0     .. 2047   NBUF DMA buffers of BUF_SAMPLES bytes
//   2048  ..        mixer parameters (loop end, loop length, block counter)
#define BUF_BASE     0
#define PM_END       2048
#define PM_LEN       2050
#define PM_CNT       2052
#define SEG_BYTES    2064

typedef char chk_pm_base  [(PM_END == BUF_BASE + 1L * NBUF * BUF_SAMPLES) ? 1 : -1];
typedef char chk_block    [(BLOCK == 64) ? 1 : -1];
typedef char chk_buf_mult [((BUF_SAMPLES % BLOCK) == 0) ? 1 : -1];
typedef char chk_pad      [(PAD >= BLOCK * NBUF) ? 1 : -1];
typedef char chk_nbuf     [((NBUF & (NBUF - 1)) == 0) ? 1 : -1];     // power of 2: the counters wrap at 65536

int MIX_RATE = 16000;
void         tick(void);

// ---------------- MOD data ----------------

unsigned char *mod_raw  = 0;     // block returned by farmalloc
unsigned char *mod_data = 0;     // start of the module (offset 0 of segment mod_seg)
unsigned int       mod_seg  = 0;

unsigned char *pat_ptr[128];

unsigned char  hdr[1084];
unsigned char  order[128];
unsigned char  num_orders, restart_pos, num_patterns;

unsigned int   smpl_seg[31];
unsigned long  smpl_off[31];            // offset of the sample in mod_data
unsigned long  smpl_fpos[31];           // position of the sample data in the file
unsigned int   smpl_fdl[31];            // bytes of the sample read from the file
unsigned int   smpl_len[31];
signed   char  smpl_ft [31];
unsigned int   smpl_lps[31];
unsigned int   smpl_lpe[31];

// ---------------- Channel state ----------------

unsigned int   ch_active    [NUM_CHANNELS];
unsigned int   ch_smp_seg   [NUM_CHANNELS];
unsigned int   ch_smp_len   [NUM_CHANNELS];
unsigned int   ch_lps       [NUM_CHANNELS];
unsigned int   ch_lpe       [NUM_CHANNELS];
unsigned int   ch_end       [NUM_CHANNELS];
unsigned int   ch_looplen   [NUM_CHANNELS];
unsigned int   ch_pos_i     [NUM_CHANNELS];
unsigned int   ch_pos_f     [NUM_CHANNELS];
unsigned int   ch_inc_i     [NUM_CHANNELS];
unsigned int   ch_inc_f     [NUM_CHANNELS];
unsigned char  ch_loop_type [NUM_CHANNELS];
unsigned char  ch_age_ticks [NUM_CHANNELS];
unsigned char  ch_priority  [NUM_CHANNELS];
unsigned char  ch_smp_cur   [NUM_CHANNELS];
unsigned int   ch_per_cur   [NUM_CHANNELS];
unsigned char  ch_smp_start [NUM_CHANNELS];

// ---------------- Mixing segment ----------------

unsigned char far *mix_raw = 0;
unsigned int       mix_seg = 0;
unsigned int       mix_buf_off = BUF_BASE;

// ---------------- Precomputed tables ----------------

unsigned int   inc_i_tbl[MAX_PERIOD + 1];
unsigned int   inc_f_tbl[MAX_PERIOD + 1];
unsigned int   tick_tbl[256];

signed int     ft_delta[16] = {
    -1839, -1615, -1389, -1162, -933, -702, -470, -236,
    0,
    237,   477,   718,   960,  1205, 1451, 1699
};

// ---------------- Mixer parameters (read by the asm blocks) ----------------

unsigned int  mix_ch2;
unsigned int  mix_out;
unsigned int  mix_nblk;

// ---------------- Channel selection state ----------------

unsigned char mix_channel_allowed[NUM_CHANNELS];
unsigned char mix_list[NUM_CHANNELS];
unsigned char mix_list_n = 0;
unsigned char silent_list[NUM_CHANNELS];
unsigned char silent_list_n = 0;
unsigned char mix_ch_limit     = 0;
unsigned char mix_ch_limit_max = 0;
unsigned char mix_slow_frames  = 0;

// ---------------- Tracker state ----------------

unsigned int   cur_order, cur_row, tick_cnt;
unsigned int   tick_speed = 6;
unsigned char  bpm = 125;
int            tick_left = 0;
unsigned char  pos_jump  = 0;
unsigned char  pat_break = 0;
unsigned int   jump_order = 0;
unsigned int   break_row  = 0;

unsigned long  trig_count = 0;
unsigned long  row_count  = 0;

unsigned char stop_flag = 0;

unsigned int  dma_blocks    = 0;         // buffers played so far (counted by the IRQ)
unsigned int  ring_filled   = 0;         // buffers mixed so far
unsigned int  dma_underruns = 0;         // times mod_update() came too late
unsigned char dsp_major = 0, dsp_minor = 0;


void interrupt (*old_sb_isr)(void) = 0;

// ==================== Sound Blaster DSP ====================

void sb_write(unsigned char v){
    unsigned int t = 0xFFFF;
    while ((inportb(SB_BASE + 0x0C) & 0x80) && t) t--;
    outportb(SB_BASE + 0x0C, v);
}

int sb_reset(void){
    int i;
    outportb(SB_BASE + 0x06, 1);
    for (i = 0; i < 400; i++) inportb(SB_BASE + 0x06);
    outportb(SB_BASE + 0x06, 0);
    for (i = 0; i < 400; i++)
        if (inportb(SB_BASE + 0x0A) == 0xAA) return 1;
        return 0;
}

void sb_get_version(void){
    unsigned int t;
    sb_write(0xE1);
    t = 0xFFFF; while (!(inportb(SB_BASE + 0x0E) & 0x80) && t) t--;
    dsp_major = inportb(SB_BASE + 0x0A);
    t = 0xFFFF; while (!(inportb(SB_BASE + 0x0E) & 0x80) && t) t--;
    dsp_minor = inportb(SB_BASE + 0x0A);
}

// ==================== DMA ====================

unsigned int dma_port_addr, dma_port_count, dma_port_page;

void dma_init_ports(void){
    switch (SB_DMA_CH) {
        case 0: dma_port_addr=0x00; dma_port_count=0x01; dma_port_page=0x87; break;
        case 1: dma_port_addr=0x02; dma_port_count=0x03; dma_port_page=0x83; break;
        case 2: dma_port_addr=0x04; dma_port_count=0x05; dma_port_page=0x81; break;
        case 3: dma_port_addr=0x06; dma_port_count=0x07; dma_port_page=0x82; break;
    }
}

void dma_program_phys(unsigned int seg, unsigned int off, unsigned int len){
    unsigned long phys = ((unsigned long)seg << 4) + off;
    unsigned char page = (unsigned char)(phys >> 16);

    outportb(0x0A, 0x04 | SB_DMA_CH);
    outportb(0x0C, 0);
    outportb(0x0B, 0x58 | SB_DMA_CH);          // single mode, memory -> device, AUTO-INIT
    outportb(dma_port_addr,  (unsigned char)(phys & 0xFF));
    outportb(dma_port_addr,  (unsigned char)((phys >> 8) & 0xFF));
    outportb(dma_port_page,  page);
    outportb(dma_port_count, (unsigned char)((len - 1) & 0xFF));
    outportb(dma_port_count, (unsigned char)(((len - 1) >> 8) & 0xFF));
    outportb(0x0A, SB_DMA_CH);
}

// ==================== SB IRQ ====================

void interrupt sb_isr(void){
    #if SB_IRQ == 7
    // IRQ 7 can also be a spurious interrupt (printer port): such an IRQ is not in service
    // in the PIC and must neither be counted nor get an EOI.
    outportb(0x20, 0x0B);
    if (!(inportb(0x20) & 0x80)) return;
    #endif
    inportb(SB_BASE + 0x0E);                   // acknowledge the sound card
    outportb(0x20, 0x20);                      // end of interrupt
    dma_blocks++;
}

int sb_set_irq(void){
    old_sb_isr = LT_getvect(0x08 + SB_IRQ);
    LT_setvect(0x08 + SB_IRQ, sb_isr);
    outportb(0x21, inportb(0x21) & ~(1 << SB_IRQ));

    sb_write(0xD1);
    sb_write(0x40);
    sb_write((unsigned char)(256 - (1000000L / MIX_RATE)));
    return 1;
}

void sb_stop_irq(void){
    sb_write(0xD0);                            // halt the 8 bit DMA
    sb_write(0xD3);                            // speaker off
    outportb(0x0A, 0x04 | SB_DMA_CH);          // mask the DMA channel
    sb_reset();                                // leave the DSP idle
    LT_setvect(0x08 + SB_IRQ, old_sb_isr);
    outportb(0x21, inportb(0x21) | (1 << SB_IRQ));
}

// Start the ring: one DMA transfer over all NBUF buffers that the DMA controller repeats
// by itself, the DSP raises an IRQ after every BUF_SAMPLES bytes.
void sb_start_autoinit(void){
    dma_blocks = 0;
    dma_program_phys(mix_seg, BUF_BASE, NBUF * BUF_SAMPLES);
    sb_write(0x48);                            // DMA block size
    sb_write((BUF_SAMPLES - 1) & 0xFF);
    sb_write(((BUF_SAMPLES - 1) >> 8) & 0xFF);
    sb_write(0x1C);                            // start 8 bit auto-init DMA output
}

// ==================== Table init ====================


void init_tables(void){
    int b;
    unsigned int p;

    for (p = 0; p <= MAX_PERIOD; p++) {
        if (p < MIN_PERIOD) {
            inc_i_tbl[p] = 0;
            inc_f_tbl[p] = 0;
        } else {
            unsigned long hz    = 3546895UL / (unsigned long)p;
            unsigned long inc32 = (hz << 16) / (unsigned long)MIX_RATE;
            if (inc32 > 0x7FFFFUL) inc32 = 0x7FFFFUL;       // integer increment below 8 (see PAD)
            inc_i_tbl[p] = (unsigned int)(inc32 >> 16);
            inc_f_tbl[p] = (unsigned int)(inc32 & 0xFFFF);
        }
    }

    for (b = 0; b < 256; b++)
        tick_tbl[b] = (b >= 32)
        ? (unsigned int)((unsigned long)MIX_RATE * 5UL
        / (2UL * (unsigned long)b))
        : 0;
}

// ==================== Priority / limit ====================

void adjust_channel_limit(unsigned short pending){
    if (pending < NBUF - 1) {
        if (mix_slow_frames > 0) mix_slow_frames--;
        if (mix_slow_frames == 0 && mix_ch_limit > 0) {
            mix_ch_limit++;
            if (mix_ch_limit > mix_ch_limit_max) mix_ch_limit = 0;
        }
    } else if (mix_slow_frames < 255) mix_slow_frames++;

    if (mix_slow_frames < SLOW_TRIGGER) return;

    if (mix_ch_limit == 0) {
        int live = 0, i;
        for (i = 0; i < NUM_CHANNELS; i++) if (ch_active[i]) live++;
        if (live < 3) live = 3;
        mix_ch_limit     = (unsigned char)live;
        mix_ch_limit_max = mix_ch_limit;
    }
    if (mix_ch_limit > 2) mix_ch_limit--;
}

// Two hard protections:
//   - Fresh notes (age 0): never culled.
//   - Non-looping one-shots: never culled.
// Only looping, non-fresh notes are eligible for the runtime channel limit,
// the oldest ones are cut first.
// Builds mix_list (channels that are mixed) and silent_list (looping channels
// that only advance their position), like the channel list of Mod Master XT.
// Called once per tick (new notes can only start there).  A one-shot that ends in between
// stays in the list, mix_chunk skips channels that are no longer active.
void select_channels(void){
    unsigned char score    [NUM_CHANNELS];
    unsigned char cuttable [NUM_CHANNELS];
    unsigned char age;
    int c, n, n_cuttable;

    if (mix_ch_limit == 0) {
        mix_list_n = 0;
        silent_list_n = 0;
        if (ch_active[0]) mix_list[mix_list_n++] = 0;
        if (ch_active[1]) mix_list[mix_list_n++] = 1;
        if (ch_active[2]) mix_list[mix_list_n++] = 2;
        if (ch_active[3]) mix_list[mix_list_n++] = 3;
        return;
    }
    // Priority: 0 fresh .. 3 stale.  Higher = more cuttable.
    for (c = 0; c < NUM_CHANNELS; c++) {
        unsigned char prio;
        if (!ch_active[c]) { ch_priority[c] = 0; continue; }
        prio = ch_loop_type[c];
        if (ch_age_ticks[c] >  3) prio++;
        if (ch_age_ticks[c] > 10) prio++;
        ch_priority[c] = prio;
    }

    n = 0;
    n_cuttable = 0;
    for (c = 0; c < NUM_CHANNELS; c++) {
        mix_channel_allowed[c] = 0;
        cuttable[c] = 0;
        if (!ch_active[c]) continue;
        // Fresh note: always mix, always protect.
        if (ch_age_ticks[c] == 0) {mix_channel_allowed[c] = 1;n++;continue;}
        // Non-looping one-shot: always mix, it will end soon anyway.
        if (ch_lpe[c] == 0) {mix_channel_allowed[c] = 1; n++; continue;}

        // Sustained looping voice: subject to the limit.  Low score = cut first.
        mix_channel_allowed[c] = 1;
        cuttable[c] = 1;
        age = ch_age_ticks[c];
        if (age > 63) age = 63;
        score[c] = (unsigned char)(((3 - ch_priority[c]) << 6) | (63 - age));
        n++;
        n_cuttable++;
    }

    if (mix_ch_limit && n > mix_ch_limit) {
        while (n > mix_ch_limit && n_cuttable > 0) {
            int victim = -1;
            unsigned char low = 0xFF;
            for (c = 0; c < NUM_CHANNELS; c++) {
                if (!cuttable[c]) continue;
                if (!mix_channel_allowed[c]) continue;
                if (score[c] < low) { low = score[c]; victim = c; }
            }
            if (victim < 0) break;
            mix_channel_allowed[victim] = 0;
            n--;
            n_cuttable--;
        }
    }

    mix_list_n = 0;
    silent_list_n = 0;
    for (c = 0; c < NUM_CHANNELS; c++) {
        if (!ch_active[c]) continue;
        if (mix_channel_allowed[c]) mix_list[mix_list_n++] = (unsigned char)c;
        else                        silent_list[silent_list_n++] = (unsigned char)c;
    }
}

// ==================== Silent position advance ====================

// Advance a culled looping channel so it stays in phase.  Only called
// for looping channels; non-looping channels are never culled.
// If the whole `count` fits before end_pos a 32x16 multiply-compare is used,
// the 32/32 divide only runs when the loop end is inside the chunk.
void advance_channel_silent(int ch, int count){
    while (count > 0) {
        unsigned int end_pos;
        unsigned long inc32, rhs, max_j, safe_count;
        unsigned long newpos, pos64, need, limit_ul;
        unsigned int int_pos, frac;
        unsigned int loop_len;

        if (!ch_active[ch]) return;

        end_pos = ch_lpe[ch] ? ch_lpe[ch] : ch_smp_len[ch];

        if (ch_pos_i[ch] >= end_pos) {
            if (ch_lpe[ch]) {ch_pos_i[ch] = ch_lps[ch];continue;}
            else { ch_active[ch] = 0;return;}
        }

        inc32 = ((unsigned long)ch_inc_i[ch] << 16) | ch_inc_f[ch];
        rhs = ((unsigned long)(end_pos - ch_pos_i[ch]) << 16) - (unsigned long)ch_pos_f[ch];
        if (rhs <= 0) rhs = 1;

        limit_ul = (unsigned long)count;
        pos64    = (((unsigned long)ch_pos_i[ch] << 16) | ch_pos_f[ch]);
        need     = inc32 * limit_ul;    // inc32==0 -> need==0 -> fast path

        // Fast path: whole remaining count fits before end_pos.
        if (need < rhs) {
            newpos = pos64 + need;
            ch_pos_i[ch] = (unsigned int)(newpos >> 16);
            ch_pos_f[ch] = (unsigned int)(newpos & 0xFFFF);
            return;
        }

        // Slow path: end_pos is inside this chunk.  Divide.
        max_j      = (rhs - 1) / inc32;
        safe_count = max_j + 1;

        newpos  = pos64 + inc32 * safe_count;
        int_pos = (unsigned int)(newpos >> 16);
        frac    = (unsigned int)(newpos & 0xFFFF);

        if (safe_count >= limit_ul) {
            ch_pos_i[ch] = int_pos;
            ch_pos_f[ch] = frac;
            return;
        }

        if (ch_lpe[ch]) {
            loop_len = end_pos - ch_lps[ch];
            int_pos  = ch_lps[ch] + (int_pos - end_pos);
            while (int_pos >= end_pos) int_pos -= loop_len;
            ch_pos_i[ch] = int_pos;
            ch_pos_f[ch] = frac;
            count -= (int)safe_count;
        } else {
            ch_active[ch] = 0;
            return;
        }
    }
}

// ==================== Mixer ====================

// Parameters (globals, read by the asm blocks):
//   mix_ch2 = channel*2   mix_out = output offset in mix_seg
//   mix_nblk = number of BLOCKs
// The channel position / fraction are read from and written back to ch_pos_i / ch_pos_f.


// How many blocks of a ONE-SHOT channel are worth mixing in this chunk: the last
// useful block is the one that starts inside the sample.  The mixer reads the
// zero padding for the rest of that block.  Almost always the answer is nblk and
// only 16 bit arithmetic is needed.
unsigned int oneshot_blocks(int ch, unsigned int nblk){
    unsigned int left = ch_smp_len[ch] - ch_pos_i[ch];
    unsigned long inc32, rhs, step, nb;

    if (left > (ch_inc_i[ch] + 1) * (nblk << BLOCK_SHIFT)) return nblk;

    inc32 = ((unsigned long)ch_inc_i[ch] << 16) | ch_inc_f[ch];
    step  = inc32 << BLOCK_SHIFT;
    if (step == 0) return nblk;
    rhs = ((unsigned long)left << 16) - (unsigned long)ch_pos_f[ch];
    if (rhs == 0) rhs = 1;
    nb = (rhs - 1) / step + 1;
    if (nb > nblk) nb = nblk;
    return (unsigned int)nb;
}

// Mix nblk blocks, starting at offset `out` of the mix segment.
void mix_chunk(unsigned int out, unsigned int nblk){
    int i;
    int ch;
    int first = 1;
    unsigned int nb;
    unsigned int count = nblk << BLOCK_SHIFT;

    for (i = 0; i < mix_list_n; i++) {
        ch = mix_list[i];
        if (!ch_active[ch]) continue;

        nb = nblk;
        if (ch_lpe[ch] == 0) nb = oneshot_blocks(ch, nblk);

        mix_ch2  = (unsigned int)ch << 1;
        mix_out  = out;
        mix_nblk = nb;

        if (first) {//mix_mov
            asm {
                push ds; push es; push si; push di; push bp;
                //cld
                mov  bx, word ptr mix_ch2
                push bx
                mov  ax, word ptr mix_seg
                mov  es, ax
                mov  ax, word ptr [bx + ch_end]
                mov  word ptr es:[PM_END], ax
                mov  ax, word ptr [bx + ch_looplen]
                mov  word ptr es:[PM_LEN], ax
                mov  ax, word ptr mix_nblk
                mov  word ptr es:[PM_CNT], ax
                mov  si, word ptr [bx + ch_pos_i]
                mov  dx, word ptr [bx + ch_pos_f]
                mov  cx, word ptr [bx + ch_inc_f]
                mov  bp, word ptr [bx + ch_inc_i]
                mov  di, word ptr mix_out
                mov  ax, word ptr [bx + ch_smp_seg]
                mov  bx, 0x8080
                mov  ds, ax
            }
            lb_mov_top:
            asm {
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
                mov al,[si]; add dx,cx; adc si,bp; mov ah,[si]; add dx,cx; adc si,bp; xor ax,bx; stosw;
            }
            lb_mov_wrap:
            asm {
                cmp  si, word ptr es:[PM_END]
                jb   lb_mov_next
                sub  si, word ptr es:[PM_LEN]
                jmp  lb_mov_wrap
            }
            lb_mov_next:
            asm {
                dec  word ptr es:[PM_CNT]
                jz   lb_mov_done
                jmp  lb_mov_top
            }
            lb_mov_done:
            asm {
                mov  ax, si
                mov  cx, dx
                pop bx; pop bp; pop di; pop si; pop es; pop ds;
                mov  word ptr [bx + ch_pos_i], ax
                mov  word ptr [bx + ch_pos_f], cx
            }                                         // writes, flips the sign bits
            if (nb < nblk) {
                unsigned short dst_off = out + (nb << BLOCK_SHIFT);
                unsigned short nbytes = (nblk - nb) << BLOCK_SHIFT;
                asm {//fill silence
                    push es; push di;
                    mov di,dst_off; mov cx,nbytes;
                    mov ax,mix_seg;
                    mov es,ax; shr cx,1;
                    mov ax,0x8080; rep stosw;
                    pop di; pop es;
                }//fill_silence(out + (nb << BLOCK_SHIFT), (nblk - nb) << BLOCK_SHIFT);
            }
            first = 0;
        } else {//mix add
            asm {
                push ds; push es; push si; push di; push bp;
                //cld
                mov  bx, word ptr mix_ch2
                push bx
                mov  ax, word ptr mix_seg
                mov  es, ax
                mov  ax, word ptr [bx + ch_end]
                mov  word ptr es:[PM_END], ax
                mov  ax, word ptr [bx + ch_looplen]
                mov  word ptr es:[PM_LEN], ax
                mov  ax, word ptr mix_nblk
                mov  word ptr es:[PM_CNT], ax
                mov  si, word ptr [bx + ch_pos_i]
                mov  dx, word ptr [bx + ch_pos_f]
                mov  cx, word ptr [bx + ch_inc_f]
                mov  bp, word ptr [bx + ch_inc_i]
                mov  di, word ptr mix_out
                mov  ax, word ptr [bx + ch_smp_seg]
                mov  ds, ax
            }
            lb_add_top:
            asm {
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
                mov ax,es:[di]; add al,[si]; add dx,cx; adc si,bp; add ah,[si]; add dx,cx; adc si,bp; stosw;
            }
            lb_add_wrap:
            asm {
                cmp  si, word ptr es:[PM_END]
                jb   lb_add_next
                sub  si, word ptr es:[PM_LEN]
                jmp  lb_add_wrap
            }
            lb_add_next:
            asm {
                dec  word ptr es:[PM_CNT]
                jz   lb_add_done
                jmp  lb_add_top
            }
            lb_add_done:
            asm {
                mov  ax, si
                mov  cx, dx
                pop bx;
                pop  bp; pop  di; pop  si; pop  es; pop  ds;
                mov  word ptr [bx + ch_pos_i], ax
                mov  word ptr [bx + ch_pos_f], cx
            }                                          // adds to the buffer
        }

        if (ch_lpe[ch] == 0 && ch_pos_i[ch] >= ch_smp_len[ch]) ch_active[ch] = 0;
    }

    if (first) {
        unsigned short dst_off = out;
        asm {//fill silence
            push es; push di;
            mov di,dst_off; mov cx,count;
            mov ax,mix_seg;
            mov es,ax; shr cx,1;
            mov ax,0x8080; rep stosw;
            pop di; pop es;
        }//fill_silence(out, count);
    }
    for (i = 0; i < silent_list_n; i++)
        advance_channel_silent(silent_list[i], (int)count);
}

// ==================== mix_and_advance ====================

// Fill the DMA buffer at offset mix_buf_off of mix_seg.
void mix_and_advance(void){
    int pos = 0;
    int n;

    // The channel lists only change when a tick starts new notes, so they are rebuilt
    // there and not for every buffer.  (A change of the channel limit takes effect at
    // the next tick.)
    while (pos < BUF_BLOCKS) {
        if (tick_left <= 0) {
            tick();
            tick_left += (int)tick_tbl[bpm];
            select_channels();
        }

        // blocks needed to cover the rest of this tick, at least one
        n = (tick_left + BLOCK - 1) >> BLOCK_SHIFT;
        if (n < 1) n = 1;
        if (n > BUF_BLOCKS - pos) n = BUF_BLOCKS - pos;

        mix_chunk(mix_buf_off + (pos << BLOCK_SHIFT), (unsigned int)n);
        pos += n;
        tick_left -= (n << BLOCK_SHIFT);
    }
}

// ==================== Buffer allocation ====================

// One far block holds the NBUF DMA buffers and the mixer parameters.  It is placed
// so that the DMA buffers do not cross a 64 KB page.
int Set_DMA_buffers(){
    unsigned long need = (unsigned long)SEG_BYTES + 2048UL + 32UL;
    unsigned long lin;

    mix_raw = LT_calloc0((need>>4)+1);
    if (!mix_raw) LT_Error("malloc failed $\n",0);

    lin = ((unsigned long)FP_SEG(mix_raw) << 4) + FP_OFF(mix_raw);
    lin = (lin + 15UL) & 0xFFFFFFF0UL;
    while ((((lin + BUF_BASE) & 0xFFFFUL) + (unsigned long)NBUF * BUF_SAMPLES) > 0x10000UL)
        lin += 16UL;
    mix_seg = (unsigned int)(lin >> 4);

    //printf("mix segment %04X, DMA buffers at phys %05lX\n", mix_seg, lin + BUF_BASE);
    return 1;
}

// ==================== MOD loader ====================

int mod_load(char *fn){
    word f;
    long size;
    unsigned int i, j;
    unsigned int got;
    unsigned long total, fpos;
    unsigned char far *p;

    f = LT_fopen(fn,0);
    if (!f) LT_Error("File not found $\n",fn);
    LT_fseek(f, 0, SEEK_END); size = LT_fseek(f, 0, SEEK_CUR); LT_fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 65535L) LT_Error("MOD > 64K $\n",fn);
    LT_fread(f,1084,hdr);
    if (hdr[1080]!='M' || hdr[1081]!='.' || hdr[1082]!='K' || hdr[1083]!='.') {
        LT_Error("Not an M.K. 4-channel MOD $\n",fn);
    }

    num_orders  = hdr[950];
    restart_pos = hdr[951];
    if (num_orders > 128) num_orders = 128;
    if (num_orders == 0) LT_Error("No orders $\n",fn);
    for (i = 0; i < 128; i++) order[i] = hdr[952 + i];

    num_patterns = 0;
    for (i = 0; i < num_orders; i++)
        if (order[i] > num_patterns) num_patterns = order[i];
        num_patterns++;
    if (num_patterns > 128) num_patterns = 128;

    // Pass 1: look at the 31 sample headers, work out where everything goes.
    // Layout of the block: the patterns (1 KB each), then every sample followed by
    // PAD bytes and rounded up to a paragraph.
    total = (unsigned long)num_patterns * 1024UL;
    fpos  = 1084UL + total;
    for (i = 0; i < 31; i++) {
        unsigned char *sh = hdr + 20 + i * 30;
        unsigned long bl  = (((unsigned short) sh[22]<<8) | sh[23]) * 2UL;
        signed   char ft  = (signed char)(sh[24] & 0x0F);
        unsigned long lsw = ((unsigned short) sh[26]<<8) | sh[27];
        unsigned long llw = ((unsigned short) sh[28]<<8) | sh[29];
        unsigned long ls  = lsw * 2UL;
        unsigned long ll  = llw * 2UL;
        unsigned long avail = ((unsigned long)size > fpos) ? ((unsigned long)size - fpos) : 0UL;
        unsigned long dl  = (bl < avail) ? bl : avail;       // what the file really contains
        unsigned int  lps = 0, lpe = 0;

        if (ft >  7)  ft = (signed char)(ft - 16);

        smpl_ft [i] = ft;
        smpl_lps[i] = 0;
        smpl_lpe[i] = 0;
        smpl_len[i] = 0;
        smpl_seg[i] = 0;
        smpl_off[i] = 0;
        smpl_fpos[i] = fpos;
        smpl_fdl[i] = 0;

        // Looping sample: the data after the loop end is never played, drop it
        if (dl > 0 && llw > 1UL && ls < dl) {
            unsigned long le = ls + ll;
            if (le > dl) le = dl;
            if (le > ls) { lps = (unsigned int)ls; lpe = (unsigned int)le; dl = le; }
        }

        if (dl > 0) {
            smpl_off[i] = total;
            smpl_fdl[i] = (unsigned int)dl;
            smpl_len[i] = (unsigned int)dl;
            smpl_lps[i] = lps;
            smpl_lpe[i] = lpe;
            total += (dl + PAD + 15UL) & 0xFFFFFFF0UL;
        }
        fpos += bl;
    }

    if ( (unsigned long)(total + 32UL + 16) > 65535) LT_Error("Too Big $\n",fn);
    // One block for the whole module
    //mod_raw = (unsigned char far *)LT_calloc0(((total + 32UL)>>4)+1);
    //if (!mod_raw) LT_Error("RAM Error $\n",fn);
    mod_seg  = (unsigned int)((((unsigned long)FP_SEG(LT_music_sdata) << 4) + FP_OFF(LT_music_sdata) + 15UL) >> 4);
    mod_data = (unsigned char far *)MK_FP(mod_seg, 0);

    // Pass 2: read the patterns, each one in its own segment
    LT_fseek(f, 1084L, SEEK_SET);
    for (i = 0; i < 128; i++) pat_ptr[i] = 0;
    for (i = 0; i < num_patterns; i++) {
        pat_ptr[i] = (unsigned char far *)MK_FP(mod_seg + i * 64, 0);
        LT_memset(pat_ptr[i], 0, 1024);
        LT_fread(f, 1024, pat_ptr[i]);
    }

    // Pass 3: read the samples, each into its own paragraph-aligned place, then add the padding
    for (i = 0; i < 31; i++) {
        unsigned int dl, lps, lpe;
        if (smpl_len[i] == 0) continue;

        dl  = smpl_len[i];
        lps = smpl_lps[i];
        lpe = smpl_lpe[i];
        smpl_seg[i] = mod_seg + (unsigned int)(smpl_off[i] >> 4);
        p = (unsigned char far *)MK_FP(smpl_seg[i], 0);

        LT_fseek(f, (long)smpl_fpos[i], SEEK_SET);
        LT_memset(p, 0, dl);
        LT_fread(f, dl, p);

        // Attenuate: signed 8 bit -> signed 6 bit, so four channels can simply be added
        for (j = 0; j < dl; j++) p[j] = (unsigned char)((signed char)p[j] >> 2);

        // Padding after the data: the loop continues (looping), or silence (one-shot)
        if (lpe) {
            unsigned int llen = lpe - lps;
            for (j = 0; j < PAD; j++) p[dl + j] = p[lps + (j % llen)];
        } else {
            for (j = 0; j < PAD; j++) p[dl + j] = 0;
        }
    }
    LT_fclose(f);

    for (i = 0; i < NUM_CHANNELS; i++) {
        ch_active[i]=0; ch_smp_seg[i]=0;
        ch_smp_len[i]=0; ch_lps[i]=0; ch_lpe[i]=0;
        ch_end[i]=0xFFFF; ch_looplen[i]=0;
        ch_pos_i[i]=0; ch_pos_f[i]=0;
        ch_inc_i[i]=0; ch_inc_f[i]=0;
        ch_loop_type[i]=0; ch_age_ticks[i]=0; ch_priority[i]=0;
        ch_smp_cur[i]=0; ch_per_cur[i]=0; ch_smp_start[i]=0;
    }
    cur_order = 0; cur_row = 0; tick_cnt = 0;
    tick_left = 0;
    pos_jump = 0; pat_break = 0;
    return 1;
}

void mod_free(void){
    if (mix_raw) LT_free(mix_raw);
}

// ==================== Tracker ====================

void trigger_note(int ch, unsigned int period, int sample){
    unsigned long inc32;
    signed char   ft;
    signed long   adj;
    unsigned char same;
    unsigned int  start_bytes;

    if (sample < 1 || sample > 31) return;
    if (period < MIN_PERIOD || period > MAX_PERIOD) return;
    if (smpl_len[sample-1] == 0) { ch_active[ch] = 0; return; }

    same = (sample == (int)ch_smp_cur[ch]) && (period == ch_per_cur[ch]);

    if (!same) {
        ch_smp_seg[ch] = smpl_seg[sample-1];
        ch_smp_len[ch] = smpl_len[sample-1];
        ch_lps    [ch] = smpl_lps[sample-1];
        ch_lpe    [ch] = smpl_lpe[sample-1];

        // loop test of the mixer: looping -> wrap at the loop end,
        // one-shot -> a value the position never reaches (the C code stops one-shots)
        if (ch_lpe[ch]) { ch_end[ch] = ch_lpe[ch]; ch_looplen[ch] = ch_lpe[ch] - ch_lps[ch]; }
        else            { ch_end[ch] = 0xFFFF;     ch_looplen[ch] = 0; }

        inc32 = ((unsigned long)inc_i_tbl[period] << 16) | inc_f_tbl[period];
        ft = smpl_ft[sample-1];
        if (ft != 0) {
            adj = ((signed long)inc32 * (signed long)ft_delta[ft + 8]);
            // Replaces: adj = ... / 32768L;
            // Shift-based equivalent with truncation toward zero.
            if (adj < 0) adj = -(( -adj) >> 15);
            else         adj = adj >> 15;

            inc32 = (unsigned long)((signed long)inc32 + adj);
            if (inc32 > 0x7FFFFUL) inc32 = 0x7FFFFUL;       // integer increment below 8 (see PAD)
        }
        ch_inc_i[ch] = (unsigned int)(inc32 >> 16);
        ch_inc_f[ch] = (unsigned int)(inc32 & 0xFFFF);

        ch_smp_cur[ch] = (unsigned char)sample;
        ch_per_cur[ch] = period;
    }

    start_bytes = (unsigned int)ch_smp_start[ch] << 8;
    if (start_bytes >= ch_smp_len[ch]) start_bytes = 0;
    ch_pos_i[ch]     = start_bytes;
    ch_pos_f[ch]     = 0;
    ch_smp_start[ch] = 0;

    ch_loop_type[ch] = (ch_lpe[ch] != 0) ? 1 : 0;
    ch_age_ticks[ch] = 0;
    ch_active[ch]    = 1;
    trig_count++;
}


void process_row(void){
    unsigned char far *pat;
    int ch;
    unsigned int row_off;

    pat = pat_ptr[order[cur_order]];
    row_off = cur_row << 4;
    row_count++;

    for (ch = 0; ch < 4; ch++) {
        int c = ch<<2;
        unsigned char b0 = pat[row_off + c + 0];
        unsigned char b1 = pat[row_off + c + 1];
        unsigned char b2 = pat[row_off + c + 2];
        unsigned char b3 = pat[row_off + c + 3];
        unsigned int  period;
        int           sample;
        unsigned char effect;

        // Ignore completely empty cells.
        if (b0 == 0 && b1 == 0 && b2 == 0 && b3 == 0) continue;
        period = ((unsigned int)(b0 & 0x0F) << 8) | b1;
        sample = (b0 & 0xF0) | (b2 >> 4);
        effect = b2 & 0x0F;

        //EFFECTS:
        // Volume 0 (C00) -> cut note; Note cut (ECx) -> cut note;
        // pattern jump/break; BPM;sample start
        switch (effect){
            case 0x09: ch_smp_start[ch] = b3; break;
            case 0x0B: pos_jump = 1; jump_order = b3; break;
            case 0x0D: pat_break = 1; break_row = (unsigned int)(b3 >> 4) * 10 + (b3 & 0x0F); break;
            case 0x0F:
                if (b3 >= 0x20) bpm = b3;
                else if (b3 != 0) tick_speed = b3;
            break;
            case 0x0C: if (b3 == 0) ch_active[ch] = 0; break;
            case 0x0E: if (b3&0xF0 == 0xC0) ch_active[ch] = 0; break;
        }
        if (period != 0 && sample != 0) trigger_note(ch, period, sample);
    }
}

void tick(void){
    int i;
    if (tick_cnt == 0) process_row();

    for (i = 0; i < NUM_CHANNELS; i++)
        if (ch_active[i] && ch_age_ticks[i] < 255) ch_age_ticks[i]++;

    tick_cnt++;
    if (tick_cnt >= tick_speed) {
        tick_cnt = 0;
        if (pos_jump || pat_break) {
            if (pos_jump) cur_order = jump_order;
            else          cur_order++;
            cur_row = pat_break ? break_row : 0;
            if (cur_row >= 64) cur_row = 0;
            pos_jump  = 0;
            pat_break = 0;
        } else {
            cur_row++;
            if (cur_row >= 64) {
                cur_row = 0;
                cur_order++;
            }
        }
        if (cur_order >= num_orders) {
            cur_order = restart_pos;
            if (cur_order >= num_orders) cur_order = 0;
        }
    }
}


// ==================== Ring service ====================

// Buffer number k (counted from the start) lives in slot k % NBUF.  It is free as soon as
// buffer k-NBUF has been played, that is when k - dma_blocks < NBUF.
void fill_ring(void){
    while ((unsigned int)(ring_filled - dma_blocks) < 8) {
        mix_buf_off = BUF_BASE + (ring_filled % NBUF) * BUF_SAMPLES;
        mix_and_advance();
        ring_filled++;
    }
}

// Call this regularly (once per frame).  Mixes the buffers that have been played.
void LT_mod_update(void){
    unsigned int pending;
    if (!LT_MUSIC_ENABLE) return;
    if (!LT_MUSIC_LOADED) return;

    // The buffer that is playing now was never mixed: we were too late.  Skip it.
    if ((int)(dma_blocks - ring_filled) >= 0) {
        dma_underruns++;
        ring_filled = dma_blocks + 1;
    }

    pending = NBUF - (ring_filled - dma_blocks);       // buffers to mix now
    if (pending == 0) return;

    // The CPU is too slow when the ring is nearly empty: only the playing buffer is left
    adjust_channel_limit(pending);
    fill_ring();
}


// ==================== Main ====================

void LT_MODMaster_Init(int rate){
    unsigned int tc;
    if (rate == 0) MIX_RATE = 8192;
    else if (rate == 1) MIX_RATE = 11025;
    else if (rate == 2) MIX_RATE = 22050;
    else rate = 1;
    tc = (unsigned int)(256L - 1000000L / MIX_RATE);
    MIX_RATE = (int)(1000000L / (256 - tc));
    if (!Set_DMA_buffers()) LT_Error("DMA buffer error\n$"," ");
    init_tables();
    if (!sb_reset()) LT_Error("SB not found \n$"," ");
    sb_get_version();
    if (dsp_major < 2) LT_Error("DSP has no auto-init DMA (Sound Blaster 2.0 or later needed) \n$",0);
    dma_init_ports();
    // IRQ hook, speaker and time constant now happen in LT_Start_MOD
}

void LT_MOD_Load(char *file, char *dat_string){
    LT_MUSIC_LOADED = 0;
    mod_load(file);
    LT_MUSIC_LOADED = 1;
}

void LT_Start_MOD(){
    LT_MUSIC_ENABLE = 0;                 // keep LT_mod_update out while we set up
    sb_reset();                          // known DSP state
    sb_set_irq();                        // hook IRQ, unmask PIC, speaker on, time constant
    inportb(SB_BASE + 0x0E);             // clear any pending DSP IRQ

    dma_blocks = 0;                      // BEFORE fill_ring
    ring_filled = 0;
    dma_underruns = 0;
    mix_slow_frames = 0;
    mix_ch_limit = 0;
    mix_ch_limit_max = 0;
    mix_list_n = 0;
    silent_list_n = 0;
    bpm = 125;
    tick_speed = 6;

    fill_ring();
    sb_start_autoinit();
    LT_MUSIC_ENABLE = 1;
}

void LT_Stop_MOD(){
    LT_MUSIC_ENABLE = 0;
    sb_stop_irq();
}

