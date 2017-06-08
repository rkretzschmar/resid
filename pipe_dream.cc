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
#include "lib_pipe_dream.h"

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
        //reduce/match pipe buffer
        //fnctl(fd_out, F_SETPIPE_SZ, OUTPUTBUFFERSIZE);
    }


    unsigned int instr_buf[10];
    short *m_buffer = new short[OUTPUTBUFFERSIZE];

    int idle_cycles = 0;
    int idle_samples = 0;
    int nr_instr = 0;

    int _read = 1;
    while (_read)
    {

        int nr_samples = OUTPUTBUFFERSIZE;
        while (nr_samples > 0)
        {

            if (nr_instr == 0)
            {
                _read = read(0, instr_buf, 40);
                nr_instr = _read / 4;
                fprintf(stderr, "read %d\n", _read);
            }

            fprintf(stderr, "EXE samples_to_go %d, instr_to_go %d, idle_cycles %d, idle_samples %d\n", nr_samples, nr_instr, idle_cycles, idle_samples);
            
            render_instrs(sid, instr_buf + (10-nr_instr), nr_instr, m_buffer, nr_samples, idle_cycles, idle_samples);

            fprintf(stderr, "RET samples_to_go %d, instr_to_go %d, idle_cycles %d, idle_samples %d\n", nr_samples, nr_instr, idle_cycles, idle_samples);
        }
        fprintf(stderr, "WRITE\n");
        write(fd_out, m_buffer, (OUTPUTBUFFERSIZE - nr_samples)*2);
    }


    delete[] m_buffer;

    if (use_sox)
    {
        pclose(f_out);
    }
}