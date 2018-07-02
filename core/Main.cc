#include <errno.h>

#include <signal.h>
#include <zlib.h>

#include "utils/System.h"
#include "utils/ParseUtils.h"
#include "utils/Options.h"
#include "core/Dimacs.h"
#include "core/Solver.h"

using namespace CDCL;

//=================================================================================================


void printStats(Solver &solver) {
    double cpu_time = cpuTime();
    printf("c\nc\nc restarts              : %"PRIu64"\n", solver.starts);
    printf("c conflicts             : %-12"PRIu64"   (%.0f /sec)\n", solver.conflicts, solver.conflicts / cpu_time);
    printf("c decisions             : %-12"PRIu64"   (%.0f /sec)\n", solver.decisions, solver.decisions / cpu_time);
    printf("c propagations          : %-12"PRIu64"   (%.0f /sec)\n", solver.propagations, solver.propagations / cpu_time);
    printf("c\n");
    printf("c nb reduce DB          : %-12"PRIu64" \n", solver.nb_reducedb);
    printf("c removed clauses       : %-12"PRIu64"   (%"PRIu64" %% of total)\n", solver.nb_removed_clauses, (solver.conflicts==0 ? 0 : (solver.nb_removed_clauses*100) / solver.conflicts));
    printf("c\n");
    printf("c CPU time              : %g s\n", cpu_time);
}


static Solver *solver;


// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
static void SIGINT_interrupt(int signum) { solver->interrupt(); }


// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int signum) {
    printf("\n");
    printf("*** INTERRUPTED ***\n");
    if(solver->verbosity > 0) {
        printStats(*solver);
        printf("\n");
        printf("*** INTERRUPTED ***\n");
    }
    _exit(1);
}


//=================================================================================================
// Main:


int main(int argc, char **argv) {
    try {
        setUsageHelp("USAGE: %s [options] <input-file> <result-output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n");

#if defined(__linux__)
        fpu_control_t oldcw, newcw;
        _FPU_GETCW(oldcw); newcw = (oldcw & ~_FPU_EXTENDED) | _FPU_DOUBLE; _FPU_SETCW(newcw);
        printf("WARNING: for repeatability, setting FPU to use double precision\n");
#endif
        // Extra options:
        //
        IntOption verb("MAIN", "verb", "Verbosity level (0=silent, 1=some, 2=more).", 1, IntRange(0, 2));
        IntOption cpu_lim("MAIN", "cpu-lim", "Limit on CPU time allowed in seconds.\n", INT32_MAX, IntRange(0, INT32_MAX));
        IntOption mem_lim("MAIN", "mem-lim", "Limit on memory usage in megabytes.\n", INT32_MAX, IntRange(0, INT32_MAX));

        printf("c\nc minicdcl - Heavily based on Minisat with only essentials components. SAT Summer School 2018\n");
        parseOptions(argc, argv, true);

        Solver S;
        double initial_time = cpuTime();

        S.verbosity = verb;

        solver = &S;
        // Use signal handlers that forcibly quit until the solver will be able to respond to
        // interrupts:
        signal(SIGINT, SIGINT_exit);
        signal(SIGXCPU, SIGINT_exit);

        // Set limit on CPU-time:
        if(cpu_lim != INT32_MAX) {
            rlimit rl;
            getrlimit(RLIMIT_CPU, &rl);
            if(rl.rlim_max == RLIM_INFINITY || (rlim_t) cpu_lim < rl.rlim_max) {
                rl.rlim_cur = cpu_lim;
                if(setrlimit(RLIMIT_CPU, &rl) == -1)
                    printf("c WARNING! Could not set resource limit: CPU-time.\n");
            }
        }

        // Set limit on virtual memory:
        if(mem_lim != INT32_MAX) {
            rlim_t new_mem_lim = (rlim_t) mem_lim * 1024 * 1024;
            rlimit rl;
            getrlimit(RLIMIT_AS, &rl);
            if(rl.rlim_max == RLIM_INFINITY || new_mem_lim < rl.rlim_max) {
                rl.rlim_cur = new_mem_lim;
                if(setrlimit(RLIMIT_AS, &rl) == -1)
                    printf("WARNING! Could not set resource limit: Virtual memory.\n");
            }
        }


        if(argc == 1)
            printf("c Reading from standard input... Use '--help' for help.\n");

        gzFile in = (argc == 1) ? gzdopen(0, "rb") : gzopen(argv[1], "rb");
        if(in == NULL)
            printf("c ERROR! Could not open file: %s\n", argc == 1 ? "<stdin>" : argv[1]), exit(1);

        if(S.verbosity > 0) {
            printf("c \n");
            printf("c \n");
        }
        parse_DIMACS(in, S);
        gzclose(in);

        if(S.verbosity > 0) {
            printf("c Number of variables:  %12d                                         \n", S.nVars());
            printf("c Number of clauses:    %12d                                         \n", S.nClauses());
        }

        double parsed_time = cpuTime();
        if(S.verbosity > 0) {
            printf("c Parse time:           %12.2f s                                       \n", parsed_time - initial_time);
            printf("c                                                                             \n");
        }

        // Change to signal-handlers that will only notify the solver and allow it to terminate
        // voluntarily:
        signal(SIGINT, SIGINT_interrupt);
        signal(SIGXCPU, SIGINT_interrupt);

        // Working with stupid assumptions:
        vec<Lit> assumptions;
        assumptions.push(mkLit(0, true));
        assumptions.push(mkLit(100, true));
        lbool ret = S.solve(assumptions);
        if(S.verbosity > 0) {

            printStats(S);
            printf("\n");
        }
        printf(ret == l_True ? "s SATISFIABLE\n" : ret == l_False ? "s UNSATISFIABLE\n" : "s INDETERMINATE\n");


        exit(ret == l_True ? 10 : ret == l_False ? 20 : 0);     // (faster than "return", which will invoke the destructor for 'Solver')
    } catch(OutOfMemoryException &) {
        printf("c \n\n");
        printf("s INDETERMINATE\n");
        exit(0);
    }
}
