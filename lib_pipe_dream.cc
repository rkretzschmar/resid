#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
//#include <sys/types.h>
//#include <sys/stat.h>

#include <string.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#include "sid.h"
#include "siddefs.h"

//PAL and 50FPS and buffer size to have around 100fps sound flushing
#define CPU_FREQ 985248 //1023444.642857142857143 //NTSC: 1022730Hz
#define SAMPLE_FREQUENCY 44100
#define OUTPUTBUFFERSIZE (SAMPLE_FREQUENCY / 100)
#define SCREEN_REFRESH 50

//To scale should cycles be quanitized
#define INSTR_TO_CYCLE 1

int decrement(int &in)
{
    in--;
    return in;
}

reSID::SID* create_sid(void)
{
    reSID::SID* sid = new reSID::SID();
    sid->set_chip_model(reSID::MOS6581);
    const int halfFreq = 5000 * ((static_cast<int>(SAMPLE_FREQUENCY) + 5000) / 10000);
    sid->set_sampling_parameters((double)CPU_FREQ, reSID::SAMPLE_FAST, (double)SAMPLE_FREQUENCY, MIN(halfFreq, 20000));
    sid->enable_filter(true);

    return sid;
}

void destroy_sid(reSID::SID* sid)
{
    delete sid;
}

// cycles_per_sample = cycle_count(clock_frequency / sample_freq * (1 << FIXP_SHIFT) + 0.5);
// ^-- is in SID protected..

//sample and cycle exact rendering of instructions
int render_instrs(reSID::SID &sid, 
                unsigned int *instrs, int &nr_instr, 
                short *samples, int &nr_samples, 
                int &idle_cycles, int &idle_samples)
{
    // idle_cycles = the cycles to be sampled when interrupted in executing instructions
    // idle_samples = the number of samples needed to catch up with an interrupted frame

    int buf_pos = 0;

    // Couldn't empty idle time, return
    //if ((idle_samples > 0) || (idle_cycles > 0)) return 0;
    //idle_samples = 0;
    //idle_cycles = 0;

    // fill up the rest
    int instr_pos = 0;
    
    while ((nr_instr > 0) && (nr_samples > 0))
    {

        // if i remove idle_cycle accumulation it doesn't crackle :/
        unsigned int cmd = instrs[instr_pos++];
        idle_cycles += (cmd & (0x1FFF << 16)) >> 16;

        while ((nr_samples > 0) && (idle_samples > 0))
        {
            reSID::cycle_count cn = 10000;

            int sampled = sid.clock(cn, samples + buf_pos, MIN(idle_samples, nr_samples));
            idle_cycles -= (10000 - cn);
            nr_samples -= sampled;
            idle_samples -= sampled;
            buf_pos += sampled;

            fprintf(stderr, "ISL instr_to_go %d, samples_to_go %d, idle_cycles %d, idle_samples %d\n", nr_instr, nr_samples, idle_cycles, idle_samples);
        }

        if (nr_samples == 0) 
        {
            idle_cycles = 0;
            return 0;
        }


        // catch up on initial idle_cycles
        while ((idle_cycles > 0) && (nr_samples > 0))
        {
            int sampled = sid.clock(idle_cycles, samples + buf_pos, MAX(idle_samples, nr_samples));
            nr_samples -= sampled;
            idle_samples -= sampled;
            buf_pos += sampled;
            fprintf(stderr, "ICL instr_to_go %d, samples_to_go %d, idle_cycles %d, idle_samples %d\n", nr_instr, nr_samples, idle_cycles, idle_samples);
        }

        if (nr_samples == 0) return 0;

        if ((cmd & (1 << 31)) == 0)
        {

            reSID::reg8 reg = (cmd & 0xFF00) >> 8;
            reSID::reg8 val = cmd & 0xFF;

            if (reg <= 24)
            {
                //Poke SID
                sid.write(reg, val);
            }
            //Handle special instructions
            else
            {
            };
            // Also sample for NOP == 25 and every other instruction
            /*while ((nr_samples > 0) & (idle_cycles > 0))
            {
                reSID::cycle_count cycles_before = idle_cycles;
                int sampled = sid.clock(idle_cycles, samples + buf_pos, nr_samples);
                nr_samples -= sampled;
                idle_samples -= sampled;
                buf_pos += sampled;

                fprintf(stderr, "CMD instr_to_go %d, samples_to_go %d, idle_cycles %d, idle_samples %d\n", nr_instr, nr_samples, idle_cycles, idle_samples);

                //sid.clock(idle_cycles);
                //cycles --;
            }*/
        }

        // FRAME ... pull from SID until samples_in_frame is ok
        else
        {

            unsigned int f_nr = cmd & 0xFFFF;

            fprintf(stderr, "IDLE SAMPLES %d\n", idle_samples);
            idle_samples += SAMPLE_FREQUENCY / SCREEN_REFRESH;
            //Sample until the whole frame is filled up
            fprintf(stderr, "IDLE SAMPLES %d\n", idle_samples);

            /*while ((nr_samples > 0) && ((idle_cycles > 0) || (idle_samples > 0)))
            {
                reSID::cycle_count cn = CPU_FREQ;

                int sampled = sid.clock(cn, samples + buf_pos, MIN(idle_samples, nr_samples));
                idle_cycles -= (CPU_FREQ - cn);
                nr_samples -= sampled;
                idle_samples -= sampled;
                buf_pos += sampled;
                fprintf(stderr, "FRM %d, %d sampled, instr_to_go %d, samples_to_go %d, idle_cycles %d, idle_samples %d\n", f_nr, sampled, nr_instr, nr_samples, idle_cycles, idle_samples);
        
            } */
        
            //sid.reset();
        }

        nr_instr--;
    }
}