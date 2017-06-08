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
    // idle_cycles = the initial cycles to be sampled and returned which still need to be sampled
    // idle_samples = the number of samples needed to catch up with an interrupted frame

    int buf_pos = 0;

    if (nr_instr)
    {
        idle_cycles += (instrs[0] & (0x1FFF << 16)) >> 16;
    }

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

    // catch up on initial idle_cycles
    while ((idle_cycles > 0) && (nr_samples > 0))
    {
        int sampled = sid.clock(idle_cycles, samples + buf_pos, MAX(idle_samples, nr_samples));
        nr_samples -= sampled;
        idle_samples -= sampled;
        buf_pos += sampled;
        fprintf(stderr, "ICL instr_to_go %d, samples_to_go %d, idle_cycles %d, idle_samples %d\n", nr_instr, nr_samples, idle_cycles, idle_samples);
    }

    // Couldn't empty idle time, return
    if ((idle_samples > 0) || (idle_cycles > 0)) return 0;
    //idle_samples = 0;
    //idle_cycles = 0;

    // fill up the rest
    int samples_in_frame = -idle_samples;
    int instr_pos = 0;
    
    while ((nr_instr > 0) && (nr_samples > 0))
    {

        unsigned int cmd = instrs[instr_pos++];
        nr_instr--;

        // check upcoming cycles to assume to process them under the current command
        if (nr_instr)
        {
            idle_cycles = (instrs[instr_pos] & (0x1FFF << 16)) >> 16;
            fprintf(stderr, "CMD DEL NOW %d, CMD DEL WORKING ON %d \n", (instrs[instr_pos-1] & (0x1FFF << 16)) >> 16, (instrs[instr_pos] & (0x1FFF << 16)) >> 16);
        }
        else idle_cycles = 0; // return? --> no, we have to execute the commands still

        if ((cmd & (1 << 31)) == 0)
        {

            reSID::reg8 reg = (cmd & 0xFF00) >> 8;
            reSID::reg8 val = cmd & 0xFF;

            //Cycle exact sampling in between frames
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
            while ((nr_samples > 0) & (idle_cycles > 0))
            {
                reSID::cycle_count cycles_before = idle_cycles;
                int sampled = sid.clock(idle_cycles, samples + buf_pos, nr_samples);
                nr_samples -= sampled;
                buf_pos += sampled;
                samples_in_frame += sampled;

                fprintf(stderr, "CMD instr_to_go %d, samples_to_go %d, idle_cycles %d, idle_samples %d\n", nr_instr, nr_samples, idle_cycles, idle_samples);
                /*
                if (verbose)
                    fprintf(stderr, "INSTR %02x%02x, sampled %3d (total %5d) for %-3d cycles (now %3d)\n", reg, val, sampled, samples_in_frame, cycles_before, idle_cycles); */

                /*sid.clock(idle_cycles);
                cycles --;*/
            }
            // if nr_samples == 0 return ?
        }

        // FRAME ... pull from SID until samples_in_frame is ok
        else
        {

            unsigned int f_nr = cmd & 0xFFFF;

            fprintf(stderr, "IDLE SAMPLES %d\n", idle_samples);
            idle_samples += SAMPLE_FREQUENCY / SCREEN_REFRESH - samples_in_frame;
            //Sample until the whole frame is filled up
            fprintf(stderr, "IDLE SAMPLES %d\n", idle_samples);

            while ((nr_samples > 0) & ( (idle_cycles > 0) | (idle_samples > 0)))
            {
                reSID::cycle_count cn = 10000;

                int sampled = sid.clock(cn, samples + buf_pos, MIN(idle_samples, nr_samples));
                idle_cycles -= (10000 - cn);
                nr_samples -= sampled;
                idle_samples -= sampled;
                buf_pos += sampled;
                samples_in_frame += sampled;
                fprintf(stderr, "FRM %d, %d sampled, instr_to_go %d, samples_to_go %d, idle_cycles %d, idle_samples %d\n", f_nr, sampled, nr_instr, nr_samples, idle_cycles, idle_samples);
                /*
                if (verbose)
                    fprintf(stderr, "FRAME %04x, sampled %3d (total %5d) for %-3d cycles --> samples needed: %-5d\n", f_nr, sampled, samples_in_frame, 10000 - cn, samples_needed); */
        
            }
        
            //reset
            if (idle_samples <= 0) 
            {
                samples_in_frame = 0;
            }
            //sid.reset();
        }

    }
}