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

    sample(unsigned the_key){
        my_key = the_key;
        count = 0;
    };
    unsigned key(){return my_key;}
    void print(FILE *f){printf("%d %d\n",my_key,count);}
};

struct thread_args {
    hash<sample, unsigned> table;
    unsigned rnum_start;
    unsigned rnum_end;
};

void *stream_seed(void *arg) {
    thread_args *args = (thread_args*) arg;
    hash<sample, unsigned> h = args->table;
    for (int i = args->rnum_start; i < args->rnum_end; i++) {
        unsigned rnum = i;

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
            if (!(s = h.lookup(key))){
                // insert a new element for it into the hash table
                s = new sample(key);
                h.insert(s);
            }

            // increment the count for the sample
            s->count++;
        }
    }

    return NULL;
}

int main (int argc, char* argv[]){
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
    thread_args args[4];

    // initialize a 16K-entry (2**14) hash of empty lists
    hash<sample, unsigned> gh;
    gh.setup(14);

    // process streams starting with different initial numbers
    for (int i = 0; i < num_threads; i++) {
        args[i].table.setup(14);
        args[i].rnum_start = NUM_SEED_STREAMS / num_threads * i;
        args[i].rnum_end = NUM_SEED_STREAMS / num_threads * (i + 1);

        pthread_create(&threads[i], NULL, stream_seed, (void*) &args[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);

        hash<sample, unsigned> th = args[i].table;
        for (int j = 0; j < th.my_size; j++) {
            auto list = th.get_list(j);
            while ((s = list->pop()) != NULL) {
                auto gs = gh.lookup(s->key());
                if (gs == NULL) {
                    gh.insert(s);
                } else {
                    gs->count += s->count;
                }
            }
        }
    }

    // print a list of the frequency of all samples
    gh.print();
}

