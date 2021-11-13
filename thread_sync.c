/*****************************************************************************
/ PROGRAM:  thread_sync.c
/ AUTHOR:   Cody Register
/ COURSE:   CS431 - Operating Systems
/ SYNOPSIS: Creates 10,000 threads that write to a fixed size chunk of a file
/           using pwrite.  Of those concurrent threads, a semaphore will allow 
/           five concurrent threads to contend for a single mutex lock.  Once 
/           acquiring the lock, a thread can write its chunk of data to the 
/           file. Similarly, 10,000 threads are created to read a fixed size 
/           chunk of data from the same file.  The reader threads similarly
/           wait on a semaphore and contend for a mutex lock before reading.
/
/           To build, you must manually link the pthread library:
/
/           gcc -o thread_sync thread_sync.c -lpthread -lrt
/
/           (c) Regis University
/*****************************************************************************/
#define _POSIX_C_SOURCE 200809L 
/** Defining this extension corrects a warning that pread and pwrite are being
 * implicitly declared, even though they are included in the unistd.h declaration.
 * https://stackoverflow.com/questions/48332332/what-does-define-posix-source-mean
 * Added to cleanup compile messages.
 */

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <semaphore.h>

#define CONCURRENT_THREADS 1000
#define DATA_SIZE          5

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t semaphore;
static int fd;
static int file_offset = 0;

// This is the function called by each writer thread.  Each thread will take
// the following steps:
//
// 1. Construct a char array containing the thread's ID number. The content must
// be justified to exactly five characters regardless of the number of digits.
// Hint: consider using sprintf.
// 2. Call wait on the semaphore
// 3. Acquire the mutex lock
// 4. Do a pwrite of the char array on the file at the current file offset
// 5. Increment the file offset value by DATA_SIZE number of bytes
// 6. Release the mutex lock
// 7. Call signal on the semaphore

void* writer_thread(void* arg) {
    int thread_id = *(int*) arg;
    char buffer[DATA_SIZE + 1];
    // Step 1. Create a char array containing only the thread's ID
    sprintf(buffer, "%5i", thread_id);

    // Step 2. Call wait on the semaphore
    if (sem_wait(&semaphore) < 0) {
        fprintf(stderr, "Could not call wait on the semaphore in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }

    // Step 3. Acquire mutex lock
    if (pthread_mutex_lock(&mutex) < 0) {
        fprintf(stderr, "Could not acquire mutex lock in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }

    // Steps 4 & 5. Execute critical section
    if (pwrite(fd, buffer, DATA_SIZE, file_offset) < 0) {
        fprintf(stderr, "Could not write file in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }
    
    //Don't increment file_offset on last item. 
    //Could also be resolved by decrementing between the pthread_creates
    if (thread_id != (CONCURRENT_THREADS - 1)) {
        file_offset += DATA_SIZE;
    }

    // Step 6. Release mutex lock
    if (pthread_mutex_unlock(&mutex) < 0) {
        fprintf(stderr, "Could not unlock mutex in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }

    // Step 7. Call signal on the semaphore
    if (sem_post(&semaphore) < 0) {
        fprintf(stderr, "Could not call signal on semaphore in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }
}

// This is the function called by each reader thread.  Each thread will take
// the following steps:
//
// 1. Call wait on the semaphore
// 2. Acquire the mutex lock
// 3. Do a pread of the char array on the file at the current file offset
// 4. Decrement the file offset value by DATA_SIZE number of bytes
// 5. Release the mutex lock
// 6. Call signal on the semaphore
// 7. Output the result of the pread

void* reader_thread(void* arg) {
    int thread_id = *(int*) arg;
    char buffer[DATA_SIZE];

    // Step 1. Call wait on the semaphore
    if (sem_wait(&semaphore) < 0) {
        fprintf(stderr, "Could not call wait on the semaphore in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }

    // Step 2. Acquire mutex lock
    if (pthread_mutex_lock(&mutex) < 0) {
        fprintf(stderr, "Could not acquire mutex lock in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }

    // Steps 3 & 4. Critical section code 
    if (pread(fd, buffer, DATA_SIZE, file_offset) < 0) {
        fprintf(stderr, "Could not read file in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }
    file_offset -= DATA_SIZE;

    // Step 5. Release mutex lock
    if (pthread_mutex_unlock(&mutex) < 0) {
        fprintf(stderr, "Could not unlock mutex in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }

    // Step 6. Call signal on the semaphore
    if (sem_post(&semaphore) < 0) {
        fprintf(stderr, "Could not call signal on semaphore in thread: %i: %s\n", 
                thread_id, strerror(errno));
        exit(-1);
    }

    // Step 7. Output the results of the read

    if ((thread_id) % 20 == 0) {
        printf("\n"); //Cleans up the output
    }
        printf("%s", buffer);


}

int main() {
    // Initialize semaphore with a value of 5
    sem_init(&semaphore, 0, 5);

    // Create an array large enough to contain 20,000 threads
    pthread_t threads[CONCURRENT_THREADS * 2];

    // Open the file that constitutes the shared buffer for reading and writing
    fd = open("sharedfile.txt", O_RDWR | O_CREAT, (mode_t) 0644);
    if (fd < 0) {
        fprintf(stderr, "Could not open file sharedfile.txt: %s\n", 
                    strerror(errno));
        return EXIT_FAILURE;
    }

    // Create 10,000 writer threads
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        pthread_create(&threads[i], NULL, writer_thread, (void*) &i);
        pthread_join(threads[i], NULL);
    }

    // Create 10,000 reader threads. Note: No thread ID numbers are reused
    for (int i = CONCURRENT_THREADS; i < 2 * CONCURRENT_THREADS; i++) {
        pthread_create(&threads[i], NULL, reader_thread, (void*) &i);
        pthread_join(threads[i], NULL);
    }

    // Tear down the semaphore and close the file
    sem_destroy(&semaphore);
    close(fd);
    printf("\n");
    return EXIT_SUCCESS;
}
