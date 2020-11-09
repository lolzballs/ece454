#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#include "defs.h"
#include "hash.h"

#define SAMPLES_TO_COLLECT   10000000
#define RAND_NUM_UPPER_BOUND   100000
#define NUM_SEED_STREAMS            4

/* 
 * ECE454 Students: 
 * Please fill in the following team struct 
 */
team_t team = {
    "fork()",                  /* Team name */

    "Benjamin Cheng",                    /* Member full name */
    "1004838045",                 /* Member student number */
    "benjamin.cheng@mail.utoronto.ca",                 /* Member email address */
};

unsigned num_threads;
unsigned samples_to_skip;

class sample;

class sample {
    unsigned my_key;
    public:
    sample *next;
    unsigned count;

    sample(unsigned the_key){my_key = the_key; count = 0;};
    unsigned key(){return my_key;}
    void print(FILE *f){printf("%d %d\n",my_key,count);}
};

// This instantiates an empty hash table
// it is a C++ template, which means we define the types for
// the element and key value here: element is "class sample" and
// key value is "unsigned".  
hash<sample,unsigned> h;

void *stream_seed(void *arg) {
    uint32_t rnum = (uint64_t) arg & 0xFFFFFFFF;
    uint32_t samples_to_skip = (uint64_t) arg >> 32;
    sample *s;

    // collect a number of samples
    for (int j = 0; j < SAMPLES_TO_COLLECT; j++){

        // skip a number of samples
        for (int k = 0; k < samples_to_skip; k++){
            rnum = rand_r((unsigned int*)&rnum);
        }

        // force the sample to be within the range of 0..RAND_NUM_UPPER_BOUND-1
        unsigned key = rnum % RAND_NUM_UPPER_BOUND;

        // if this sample has not been counted before
        h.lock_list(key);
        if (!(s = h.lookup(key))){
            // insert a new element for it into the hash table
            s = new sample(key);
            h.insert(s);
        }

        // increment the count for the sample
        s->count++;
        h.unlock_list(key);
    }

    return 0;
}

int main (int argc, char* argv[]){
    int i,j,k;
    int rnum;
    unsigned key;
    sample *s;

    // Print out team information
    printf( "Team Name: %s\n", team.team );
    printf( "\n" );
    printf( "Student 1 Name: %s\n", team.name1 );
    printf( "Student 1 Student Number: %s\n", team.number1 );
    printf( "Student 1 Email: %s\n", team.email1 );
    printf( "\n" );

    // Parse program arguments
    if (argc != 3){
        printf("Usage: %s <num_threads> <samples_to_skip>\n", argv[0]);
        exit(1);  
    }
    sscanf(argv[1], " %d", &num_threads); // not used in this single-threaded version
    sscanf(argv[2], " %d", &samples_to_skip);

    pthread_t threads[4];

    // initialize a 16K-entry (2**14) hash of empty lists
    h.setup(14);

    // process streams starting with different initial numbers
    while(i < NUM_SEED_STREAMS) {
        j = 0;
        while (j < num_threads && i < NUM_SEED_STREAMS) {
            uint64_t arg = ((uint64_t) samples_to_skip << 32) | i;
            pthread_create(&threads[j], NULL, stream_seed, (void*) arg);
            i++;
            j++;
        }

        for (k = 0; k < j; k++) {
            pthread_join(threads[k], NULL);
        }
    }

    // print a list of the frequency of all samples
    h.print();
}

