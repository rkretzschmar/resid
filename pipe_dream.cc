#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#include "sid.h"
#include "siddefs.h"

//PAL and 50FPS and buffer size to have around 100fps sound flushing 
#define CPU_FREQ 985248 //1023444.642857142857143 //NTSC: 1022730Hz
#define SAMPLE_FREQUENCY 44100
#define OUTPUTBUFFERSIZE (SAMPLE_FREQUENCY / 100)

//To scale aribtrary should cycles be quanitized
#define INSTR_TO_CYCLE 1


void flush_buf(short* buf, int &buf_pos)
{
    fprintf(stderr, "Flushing %d max but with %d\n", OUTPUTBUFFERSIZE, buf_pos);
    if (buf_pos)
    {
        write(1, (short *)buf, buf_pos * 2);
        buf_pos = 0;
    }
}

int main(int argc, char **argv)
{   
    //Setup SID
    reSID::SID sid;
    sid.set_chip_model(reSID::MOS6581);
    const int halfFreq = 5000 * ((static_cast<int>(SAMPLE_FREQUENCY) + 5000) / 10000);
    sid.set_sampling_parameters((double)CPU_FREQ, reSID::SAMPLE_FAST, (double)SAMPLE_FREQUENCY, MIN(halfFreq, 20000));
    sid.enable_filter(true);

    unsigned int cmd;
    unsigned int instr_buf = 0;
    int c_read = read(0, &cmd, 4);
    if (c_read == 4) instr_buf++;

    int samples_in_frame = 0;
    short *m_buffer = new short[OUTPUTBUFFERSIZE];
    int buf_pos = 0;

    while (instr_buf)
    {
        unsigned int cmd_next;
        int c_read = read(0, &cmd_next, 4);
        if (c_read == 4) instr_buf++;

        {
            //How long this state will be sampled
            int dist_next = (cmd_next & (0x1FFF << 16)) >> 16;
            reSID::cycle_count cycles = dist_next * INSTR_TO_CYCLE;
            //cycles = 5000;

            // FRAME ... pull from SID until full
            if (cmd & (1 << 31))
            {
                unsigned int f_nr = cmd & 0xFFFF;
                //fprintf(stderr, "FRAME %d | next instr in %04x\n", f_nr, dist_next);
                //fprintf(stderr, " -- cmd: %08x\n     cmd_next: %08x    \n", cmd, cmd_next);
                
                //Sample until the whole frame is filled up
                if (true)
                {
                    int samples_needed = SAMPLE_FREQUENCY / 50 - samples_in_frame; //50 hardcoded!
                    while (samples_needed > 0)
                    {
                        reSID::cycle_count cn = 10000; 
                        int sampled = sid.clock(cn, (short *)m_buffer + buf_pos, MIN(OUTPUTBUFFERSIZE - buf_pos, samples_needed));
                        buf_pos += sampled;
                        samples_in_frame += sampled;
                        samples_needed -= sampled;

                        if (buf_pos >= OUTPUTBUFFERSIZE) flush_buf(m_buffer, buf_pos);

                        fprintf(stderr, "FRAME %04x received %00d samples (total %d), %00d still needed\n", f_nr, sampled, samples_in_frame, samples_needed);   
                    }
                }
                //reset
                flush_buf(m_buffer, buf_pos);
                samples_in_frame = 0;
                //sid.reset();
            }

            // INSTRUCTION ... push to sound buffer
            else
            {
                reSID::reg8 reg = (cmd & 0xFF00) >> 8;
                reSID::reg8 val = cmd & 0xFF;
                
                //Cycle exact sampling in between frames
                if (reg <= 24){

                    //Poke SID
                    sid.write(reg, val);

                    while (cycles)
                    {
                        reSID::cycle_count cycles_before = cycles;
                        int sampled = sid.clock(cycles, (short *)m_buffer + buf_pos, OUTPUTBUFFERSIZE - buf_pos);
                        buf_pos += sampled;
                        samples_in_frame += sampled;

                        if (buf_pos >= OUTPUTBUFFERSIZE) flush_buf(m_buffer, buf_pos);

                        fprintf(stderr, "INST %02x%02x: received %00d samples (total %d) for %00d cycles (now %00d)\n", reg, val, sampled, samples_in_frame, cycles_before, cycles);
                        
                        /*sid.clock(cycles);
                        cycles --;*/
                    }
                }
                //Handle special instructions
                else {
                }
            }

        }
        
        instr_buf--;
        cmd = cmd_next;
    }

    flush_buf(m_buffer, buf_pos);
    delete[] m_buffer;
}