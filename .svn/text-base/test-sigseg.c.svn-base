
#define _GNU_SOURCE // this will open __USE_GNU, needed for ucontext_t
//#define __USE_GNU


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>



#include <signal.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <assert.h>

#include <sys/ucontext.h>

static void sigsegv_handler(int sig, siginfo_t *si, void *unused)
{
    void *va = si->si_addr;
  
    ucontext_t *u = (ucontext_t *)unused;
 
    unsigned char *pc = (unsigned char *)u->uc_mcontext.gregs[REG_RIP];

    int rwerror = u->uc_mcontext.gregs[REG_ERR]&2; 

    // u->uc_mcontext.gregs[REG_ERR]&2 == 0::  read-protect error
    // u->uc_mcontext.gregs[REG_ERR]&2 == 2::  write-protect error
     
    printf("Got SIGSEGV at address: 0x%lx, pc=%p, rw=%d\n",
        (long) si->si_addr, pc, rwerror );
    
    if( !va ){    // invalide address, shall exit now ...
        //signal(SIGSEGV, SIG_DFL);
        printf("sigsegv:  invalid address=%p\n", va);
        assert(0);
        return;
    }
    va = (unsigned long)va & ~((1UL<<12)-1);
    
    if( rwerror == 0 ) // read-err
    {
        printf("see a read-err...\n");
        if( mprotect( va, 4096, PROT_READ)!=0 ){
            printf("in sigsegv: read mprotect %p failed...\n", va);
            perror("mprotect error::  ");
            exit(0);
        }
    }
    else if( rwerror==2 ) // write-err, give write-prot, which implies read-prot 
    {
        printf("see a write-err...\n");
        if( mprotect( va, 4096, PROT_WRITE)!=0 ){
            printf("in sigsegv: write mprotect %p failed...\n", va);
            perror("mprotect error::  ");
            exit(0);
        }
    }
    //exit(EXIT_FAILURE);
}



void    init_sigsegv_sigaction(struct sigaction* action)
{
  /* Block most signals while SIGSEGV is being handled.  */
  /* Signals SIGKILL, SIGSTOP cannot be blocked.  */
  /* Signals SIGCONT, SIGTSTP, SIGTTIN, SIGTTOU are not blocked because
     dealing with these signals seems dangerous.  */
  /* Signals SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGTRAP, SIGIOT, SIGEMT, SIGBUS,
     SIGSYS, SIGSTKFLT are not blocked because these are synchronous signals,
     which may require immediate intervention, otherwise the process may
     starve.  */

    // sigs in the sa_mask will be blocked

    sigemptyset (&action->sa_mask);

#ifdef SIGHUP
    sigaddset (&action->sa_mask,SIGHUP);
#endif
#ifdef SIGINT
    sigaddset (&action->sa_mask,SIGINT);
#endif
#ifdef SIGQUIT
    sigaddset (&action->sa_mask,SIGQUIT);
#endif
#ifdef SIGPIPE
    sigaddset (&action->sa_mask,SIGPIPE);
#endif
#ifdef SIGALRM
    sigaddset (&action->sa_mask,SIGALRM);
#endif
#ifdef SIGTERM
    sigaddset (&action->sa_mask,SIGTERM);
#endif
#ifdef SIGUSR1
    sigaddset (&action->sa_mask,SIGUSR1);
#endif
#ifdef SIGUSR2
    sigaddset (&action->sa_mask,SIGUSR2);
#endif
#ifdef SIGCHLD
    sigaddset (&action->sa_mask,SIGCHLD);
#endif
#ifdef SIGCLD
    sigaddset (&action->sa_mask,SIGCLD);
#endif
#ifdef SIGURG
    sigaddset (&action->sa_mask,SIGURG);
#endif
#ifdef SIGIO
    sigaddset (&action->sa_mask,SIGIO);
#endif
#ifdef SIGPOLL
    sigaddset (&action->sa_mask,SIGPOLL);
#endif
#ifdef SIGXCPU
    sigaddset (&action->sa_mask,SIGXCPU);
#endif
#ifdef SIGXFSZ
    sigaddset (&action->sa_mask,SIGXFSZ);
#endif
#ifdef SIGVTALRM
    sigaddset (&action->sa_mask,SIGVTALRM);
#endif
#ifdef SIGPROF
    sigaddset (&action->sa_mask,SIGPROF);
#endif
#ifdef SIGPWR
    sigaddset (&action->sa_mask,SIGPWR);
#endif
#ifdef SIGLOST
    sigaddset (&action->sa_mask,SIGLOST);
#endif
#ifdef SIGWINCH
    sigaddset (&action->sa_mask,SIGWINCH);
#endif

}

int try_sigsegv()
{
    unsigned char *p1;
    unsigned long i;
    /////////////// install sigsegv handler
    //// term handler
    struct sigaction sa; 

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    init_sigsegv_sigaction(&sa);

    sa.sa_sigaction = sigsegv_handler;
    //sa.sa_handler = term_handler;
    sa.sa_flags = SA_SIGINFO; // need a siginfo_t in callback func

    if( sigaction(SIGSEGV, &sa, NULL)!= 0 )
    {
        printf("fail to reg sig SIGSEGV...\n");
        return -1;
    }

    ///////////////////// alloc mem, set read/write protect

    //// NOTE::  in order to malloc huge size, shall let linux kernel use:  sysctl -w vm.overcommit_memory=1

    size_t size = 1024UL*1024*1024*1024;// *1024*200; //sizeof(int);
    i = size/2; //4096; //size/2; //1024*1024*50;

    p1 = memalign(4096, size);
    if( !p1 ){
        printf("mem alloc failed..\n");
        return -1;
    }
    
    printf("now mprotect: %p, : size=%ld ...\n", p1, size);

    //madvise(p1, size, MADV_DONTNEED);
    if(mprotect ((void *)(p1), size, PROT_NONE) != 0)
    {
        printf("mprotect 1 failed..\n");
        return -1;
    }
    printf("begin test...\n");

    //////////////////////////  trigger sigsegv
    printf("1::  p[%ld] = %d\n\n\n", i+2, p1[i+2]);  // read
    p1[i+2] = 101;  // write   
    printf("1.5::  p[%ld] = %d\n\n\n", i+2, p1[i+2]);  // read


    ////////////////
    madvise(p1+i, 4096, MADV_DONTNEED);
    if(mprotect ((void *)(p1+i), 4096, PROT_NONE) < 0)
    {
        printf("mprotect 2 failed..\n");
        return -1;
    }
    //sleep(2);
    p1[i+3] = 112;  // write   
    printf("2:: p[%ld] = %d\n\n\n", i+3, p1[i+3]);  // if not write in the previous line,
        // read p1[i]= 0

    //////

    i = size-1;    
    p1[i] = 35;  // write   
    printf("p[%ld] = %d\n", i, p1[i]);  // read

    /////////////////////////////////
    free(p1);



}


int main(int argc, char** argv)
{

    int i;
    
    void *p;

    try_sigsegv();
 

    return 0;

}
