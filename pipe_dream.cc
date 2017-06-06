#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

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


void flush_buf(short* buf, int &buf_pos, int fd)
{
    //fprintf(stderr, "Flushing %d max but with %d\n", OUTPUTBUFFERSIZE, buf_pos);
    if (buf_pos)
    {
        write(fd, (short *)buf, buf_pos * 2);
        buf_pos = 0;
    }
}

int main(int argc, char **argv)
{   


    int verbose = 0;
    char *fn = 0;
    int fd_in = 0;

    int fd_out = 1;
    int use_sox = 0;
    FILE *f_out = 0;

    // Scan arguments
    for (int c = 0; c < argc; c++)
    {
        if (argv[c][0] == '-')
        {
            switch(argv[c][1])
            {
            case 'v':
            verbose = 1;
            break;
            case 's':
            use_sox = 1;
            break;
            }
            
        }
        else
        {
            if (!fn) fn = argv[c];
        }
    }

    //Setup SID
    reSID::SID sid;
    sid.set_chip_model(reSID::MOS6581);
    const int halfFreq = 5000 * ((static_cast<int>(SAMPLE_FREQUENCY) + 5000) / 10000);
    sid.set_sampling_parameters((double)CPU_FREQ, reSID::SAMPLE_FAST, (double)SAMPLE_FREQUENCY, MIN(halfFreq, 20000));
    sid.enable_filter(true);

    //Setup IO
    if (fn)
    {
        fd_in = open(fn, O_RDONLY);
        if (fd_in < 0) {
            fprintf(stderr, "Cannot open %s", fn);
            return 1;
        }
    }
    if (use_sox) {
        f_out = popen("sox -traw -r44100 -b16 -c 1 -e signed-integer - -tcoreaudio", "w");
        fd_out = fileno(f_out);
    }

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

                int samples_needed = SAMPLE_FREQUENCY / SCREEN_REFRESH - samples_in_frame;
                //Sample until the whole frame is filled up
                if (samples_needed > 0)
                {
                    while (samples_needed > 0)
                    {
                        reSID::cycle_count cn = 10000; 
                        int sampled = sid.clock(cn, (short *)m_buffer + buf_pos, MIN(OUTPUTBUFFERSIZE - buf_pos, samples_needed));
                        buf_pos += sampled;
                        samples_in_frame += sampled;
                        samples_needed -= sampled;

                        if (buf_pos >= OUTPUTBUFFERSIZE) flush_buf(m_buffer, buf_pos, fd_out);

                        if (verbose) fprintf(stderr, "FRAME %04x, sampled %3d (total %5d) for %-3d cycles --> samples needed: %-5d\n", f_nr, sampled, samples_in_frame, 10000-cn, samples_needed);
                    }
                }
                else
                {
                    fprintf(stderr, "FRAME %04x, produced %00d samples too much (total %5d)\n", f_nr, -samples_needed, samples_in_frame);
                }
                //reset
                flush_buf(m_buffer, buf_pos, fd_out);
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
                }
                //Handle special instructions
                else
                {
                }

                // Also sample for NOP = 25 and every other instruction
                while (cycles)
                {
                    reSID::cycle_count cycles_before = cycles;
                    int sampled = sid.clock(cycles, (short *)m_buffer + buf_pos, OUTPUTBUFFERSIZE - buf_pos);
                    buf_pos += sampled;
                    samples_in_frame += sampled;

                    if (buf_pos >= OUTPUTBUFFERSIZE) flush_buf(m_buffer, buf_pos, fd_out);

                    if (verbose) fprintf(stderr, "INSTR %02x%02x, sampled %3d (total %5d) for %-3d cycles (now %3d)\n", reg, val, sampled, samples_in_frame, cycles_before, cycles);
                    
                    /*sid.clock(cycles);
                    cycles --;*/
                }
                
            }

        }
        
        instr_buf--;
        cmd = cmd_next;
    }

    flush_buf(m_buffer, buf_pos, fd_out);
    delete[] m_buffer;

    if (use_sox)
    {
        pclose(f_out);
    }
}