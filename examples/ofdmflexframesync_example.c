//
// ofdmflexframesync_example.c
//
// Example demonstrating the OFDM flexible frame synchronizer.
//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#include "liquid.h"

void usage()
{
    printf("ofdmflexframesync_example [options]\n");
    printf("  u/h   : print usage\n");
    printf("  s     : signal-to-noise ratio [dB], default: 30\n");
    printf("  f     : frame length [bytes], default: 120\n");
    printf("  p     : modulation depth (default 2 bits/symbol)\n");
    printf("  m     : modulation scheme (qpsk default)\n");
    liquid_print_modulation_schemes();
    printf("  v     : data integrity check: crc32 default\n");
    liquid_print_crc_schemes();
    printf("  c     : coding scheme (inner): h74 default\n");
    printf("  k     : coding scheme (outer): none default\n");
    liquid_print_fec_schemes();
}

// callback function
int callback(unsigned char *  _header,
             int              _header_valid,
             unsigned char *  _payload,
             unsigned int     _payload_len,
             int              _payload_valid,
             framesyncstats_s _stats,
             void *           _userdata);

int main(int argc, char*argv[])
{
    srand(time(NULL));

    // options
    unsigned int M = 64;                // number of subcarriers
    unsigned int cp_len = 16;           // cyclic prefix length
    unsigned int payload_len = 120;     // length of payload (bytes)
    unsigned int num_symbols_S0 = 3;    // number of S0 training symbols
    modulation_scheme ms = LIQUID_MODEM_QPSK;
    unsigned int bps = 2;
    fec_scheme fec0  = LIQUID_FEC_NONE;
    fec_scheme fec1  = LIQUID_FEC_HAMMING128;
    crc_scheme check = LIQUID_CRC_32;
    float noise_floor = -30.0f;         // noise floor [dB]
    float SNRdB = 20.0f;                // signal-to-noise ratio [dB]

    // get options
    int dopt;
    while((dopt = getopt(argc,argv,"uhs:f:p:m:v:c:k:")) != EOF){
        switch (dopt) {
        case 'u':
        case 'h': usage();                      return 0;
        case 's': SNRdB = atof(optarg);         break;
        case 'f': payload_len = atol(optarg);   break;
        case 'p': bps = atoi(optarg);           break;
        case 'm':
            ms = liquid_getopt_str2mod(optarg);
            if (ms == LIQUID_MODEM_UNKNOWN) {
                fprintf(stderr,"error: %s, unknown/unsupported mod. scheme: %s\n", argv[0], optarg);
                exit(-1);
            }
            break;
        case 'v':
            // data integrity check
            check = liquid_getopt_str2crc(optarg);
            if (check == LIQUID_CRC_UNKNOWN) {
                fprintf(stderr,"error: unknown/unsupported CRC scheme \"%s\"\n\n",optarg);
                exit(1);
            }
            break;
        case 'c':
            // inner FEC scheme
            fec0 = liquid_getopt_str2fec(optarg);
            if (fec0 == LIQUID_FEC_UNKNOWN) {
                fprintf(stderr,"error: unknown/unsupported inner FEC scheme \"%s\"\n\n",optarg);
                exit(1);
            }
            break;
        case 'k':
            // outer FEC scheme
            fec1 = liquid_getopt_str2fec(optarg);
            if (fec1 == LIQUID_FEC_UNKNOWN) {
                fprintf(stderr,"error: unknown/unsupported outer FEC scheme \"%s\"\n\n",optarg);
                exit(1);
            }
            break;
        default:
            fprintf(stderr,"error: %s, unknown option '%s'\n", argv[0], optarg);
            exit(-1);
        }
    }

    // TODO : validate options

    // derived values
    unsigned int frame_len = M + cp_len;
    float complex buffer[frame_len]; // time-domain buffer
    float nstd = powf(10.0f, noise_floor/10.0f);
    float gamma = powf(10.0f, (SNRdB + noise_floor)/10.0f);

    // allocate memory for header, payload
    unsigned char header[8];
    unsigned char payload[payload_len];

    // initialize subcarrier allocation
    unsigned int p[M];
    ofdmframe_init_default_sctype(M, p);

    // create frame generator
    ofdmflexframegenprops_s fgprops;
    ofdmflexframegenprops_init_default(&fgprops);
    fgprops.num_symbols_S0  = num_symbols_S0;
    fgprops.payload_len     = payload_len;
    fgprops.check           = check;
    fgprops.fec0            = fec0;
    fgprops.fec1            = fec1;
    fgprops.mod_scheme      = ms;
    fgprops.mod_bps         = bps;
    ofdmflexframegen fg = ofdmflexframegen_create(M, cp_len, p, &fgprops);
    ofdmflexframegen_print(fg);

    // create frame synchronizer
    ofdmflexframesync fs = ofdmflexframesync_create(M, cp_len, p, callback, (void*)payload);
    ofdmflexframesync_print(fs);

    unsigned int i;

    // initialize header/payload and assemble frame
    for (i=0; i<8; i++)
        header[i] = i & 0xff;
    for (i=0; i<payload_len; i++)
        payload[i] = rand() & 0xff;
    ofdmflexframegen_assemble(fg, header, payload);

    // initialize frame synchronizer with noise
    for (i=0; i<1000; i++) {
        float complex noise = nstd * randnf() * cexpf(_Complex_I*2*M_PI*randf());
        ofdmflexframesync_execute(fs, &noise, 1);
    }

    // generate frame
    int last_symbol=0;
    unsigned int num_written;
    while (!last_symbol) {
        // generate symbol
        last_symbol = ofdmflexframegen_writesymbol(fg, buffer, &num_written);

        // apply channel
        for (i=0; i<num_written; i++) {
            float complex noise = nstd * randnf() * cexpf(_Complex_I*2*M_PI*randf());
            buffer[i] *= gamma;
            buffer[i] += noise;
        }

        // receive symbol
        ofdmflexframesync_execute(fs, buffer, num_written);
    }

    // destroy objects
    ofdmflexframegen_destroy(fg);
    ofdmflexframesync_destroy(fs);

    printf("done.\n");
    return 0;
}

// callback function
int callback(unsigned char *  _header,
             int              _header_valid,
             unsigned char *  _payload,
             unsigned int     _payload_len,
             int              _payload_valid,
             framesyncstats_s _stats,
             void *           _userdata)
{
    printf("**** callback invoked : rssi = %8.3f dB\n", _stats.rssi);

    unsigned int i;

    // print header data to standard output
    printf("  header rx  :");
    for (i=0; i<8; i++)
        printf(" %.2X", _header[i]);
    printf("\n");

    // print payload data to standard output
    printf("  payload rx :");
    for (i=0; i<_payload_len; i++) {
        printf(" %.2X", _payload[i]);
        if ( ((i+1)%26)==0 && i !=_payload_len-1 )
            printf("\n              ");
    }
    printf("\n");

    // count errors in received payload and print to standard output
    unsigned char * payload_tx = (unsigned char*) _userdata;
    unsigned int num_errors = count_bit_errors_array(_payload, payload_tx, _payload_len);
    printf("  bit errors : %u / %u\n", num_errors, 8*_payload_len);

    return 0;
}
