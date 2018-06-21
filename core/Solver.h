#ifndef Minisat_Solver_h
#define Minisat_Solver_h

#include "mtl/Vec.h"
#include "mtl/Heap.h"
#include "mtl/Alg.h"
#include "utils/Options.h"
#include "core/SolverTypes.h"
#include<iostream>
#include <iomanip>


namespace CDCL {

//=================================================================================================
// Solver -- the main class:

    class Solver {
    public:

        // Constructor/Destructor:
        //
        Solver();
        virtual ~Solver();

        // Problem specification:
        //
        Var newVar(bool polarity = true); // Add a new variable with parameters specifying variable mode.
        bool addClause_(vec<Lit> &ps);   // Add a clause to the solver without making superflous internal copy. Will change the passed vector 'ps'.

        // Solving:
        //
        lbool solve();                  // Search without assumptions.
        bool okay() const;              // FALSE means solver is in a conflicting state



        // Variable mode:
        //

        // Read state:
        //
        lbool value(Var x) const;       // The current value of a variable.
        lbool value(Lit p) const;       // The current value of a literal.
        int nAssigns() const;           // The current number of assigned literals.
        int nClauses() const;           // The current number of original clauses.
        int nLearnts() const;           // The current number of learnt clauses.
        int nVars() const;              // The current number of variables.

        // Resource contraints:
        //
        void setConfBudget(int64_t x);
        void setPropBudget(int64_t x);
        void budgetOff();
        void interrupt();               // Trigger a (potentially asynchronous) interruption of the solver.
        void clearInterrupt();          // Clear interrupt indicator flag.

        // Memory managment:
        //
        virtual void garbageCollect();
        void checkGarbage(double gf);
        void checkGarbage();

        // Extra results: (read-only member variable)
        //
        vec<lbool> model;               // If problem is satisfiable, this vector contains the model (if any).

        // Mode of operation:
        //
        int verbosity;
        double var_decay;
        double clause_decay;
        bool luby_restart;
        uint64_t nextReduceDB;
        double garbage_frac;           // The fraction of wasted memory allowed before a garbage collection is triggered.

        // Statistics
        uint64_t starts, decisions, rnd_decisions, propagations, conflicts, nb_removed_clauses, nb_reducedb;
        uint64_t nb_resolutions, nb_lits_in_learnts;

    protected:

        // Helper structures:
        //
        struct VarData {
            CRef reason;
            int level;
        };


        static inline VarData mkVarData(CRef cr, int l) {
            VarData d = {cr, l};
            return d;
        }


        struct Watcher {
            CRef cref;
            Lit blocker;


            Watcher(CRef cr, Lit p) : cref(cr), blocker(p) {}


            bool operator==(const Watcher &w) const { return cref == w.cref; }


            bool operator!=(const Watcher &w) const { return cref != w.cref; }
        };

        struct WatcherDeleted {
            const ClauseAllocator &ca;


            WatcherDeleted(const ClauseAllocator &_ca) : ca(_ca) {}


            bool operator()(const Watcher &w) const { return ca[w.cref].mark() == 1; }
        };

        struct VarOrderLt {
            const vec<double> &activity;
            bool operator()(Var x, Var y) const { return activity[x] > activity[y]; }
            VarOrderLt(const vec<double> &act) : activity(act) {}
        };

        // Solver state:
        //
        bool ok;                     // If FALSE, the constraints are already unsatisfiable. No part of the solver state may be used!
        vec<CRef> clauses;           // List of problem clauses.
        vec<CRef> learnts;           // List of learnt clauses.
        double cla_inc;              // Amount to bump next clause with.
        vec<double> activity;        // A heuristic measurement of the activity of a variable.
        double var_inc;              // Amount to bump next variable with.
        OccLists<Lit, vec<Watcher>, WatcherDeleted>
                watches;             // 'watches[lit]' is a list of constraints watching 'lit' (will go there if literal becomes true).
        vec<lbool> assigns;          // The current assignments.
        vec<char> polarity;          // The preferred polarity of each variable.
        vec<Lit> trail;              // Assignment stack; stores all assigments made in the order they were made.
        vec<int> trail_lim;          // Separator indices for different decision levels in 'trail'.
        vec<VarData> vardata;        // Stores reason and level for each variable.
        int qhead;                   // Head of queue (as index into the trail -- no more explicit propagation queue in MiniSat).
        Heap<VarOrderLt> order_heap; // A priority queue of variables ordered with respect to the variable activity.
        double progress_estimate;    // Set by 'search()'.

        ClauseAllocator ca;

        // Temporaries (to reduce allocation overhead). Each variable is prefixed by the method in which it is
        // used, exept 'seen' wich is used in several places.
        //
        vec<char> seen;
        vec<unsigned int>   levelTagged;
        unsigned int FLAG;
        vec<Lit> analyze_stack;
        vec<Lit> analyze_toclear;
        vec<Lit> add_tmp;

        // Resource contraints:
        //
        int64_t conflict_budget;    // -1 means no budget.
        int64_t propagation_budget; // -1 means no budget.
        bool asynch_interrupt;

        // Main internal methods:
        //
        void insertVarOrder(Var x);                                          // Insert a variable in the decision order priority queue.
        Lit pickBranchLit();                                                 // Return the next decision variable.
        void newDecisionLevel();                                             // Begins a new decision level.
        void uncheckedEnqueue(Lit p, CRef from = CRef_Undef);                // Enqueue a literal. Assumes value of literal is undefined.
        CRef propagate();                                                    // Perform unit propagation. Returns possibly conflicting clause.
        void cancelUntil(int level);                                         // Backtrack until a certain level.
        void analyze(CRef confl, vec<Lit> &out_learnt, int &out_btlevel, int & lbd);    // (bt = backtrack)
        lbool search(int nof_conflicts);                                     // Search for a given number of conflicts.
        lbool solve_();                                                      // Main solve method (assumptions given in 'assumptions').
        void reduceDB();                                                     // Reduce the set of learnt clauses.
        int computeLBD(vec<Lit> &lits);                                      // compute the LBD of a clause
        // Maintaining Variable/Clause activity:
        //
        void varDecayActivity();                     // Decay all variables with the specified factor. Implemented by increasing the 'bump' value instead.
        void varBumpActivity(Var v, double inc);     // Increase a variable with the current 'bump' value.
        void varBumpActivity(Var v);                 // Increase a variable with the current 'bump' value.
        void claDecayActivity();                     // Decay all clauses with the specified factor. Implemented by increasing the 'bump' value instead.
        void claBumpActivity(Clause &c);             // Increase a clause with the current 'bump' value.

        // Operations on clauses:
        //
        void attachClause(CRef cr);                      // Attach a clause to watcher lists.
        void detachClause(CRef cr, bool strict = false); // Detach a clause to watcher lists.
        void removeClause(CRef cr);                      // Detach and free a clause.
        bool locked(const Clause &c) const;              // Returns TRUE if a clause is a reason for some implication in the current state.

        void relocAll(ClauseAllocator &to);

        // Misc:
        //
        int decisionLevel() const; // Gives the current decisionlevel.
        uint32_t abstractLevel(Var x) const; // Used to represent an abstraction of sets of decision levels.
        CRef reason(Var x) const;
        int level(Var x) const;
        double progressEstimate() const; // DELETE THIS ?? IT'S NOT VERY USEFUL ...
        bool withinBudget() const;
        void printIntermediateStats();

    };


//=================================================================================================
// Implementation of inline methods:

    inline CRef Solver::reason(Var x) const { return vardata[x].reason; }


    inline int Solver::level(Var x) const { return vardata[x].level; }


    inline void Solver::insertVarOrder(Var x) {
        if(!order_heap.inHeap(x)) order_heap.insert(x);
    }


    inline void Solver::varDecayActivity() { var_inc *= (1 / var_decay); }


    inline void Solver::varBumpActivity(Var v) { varBumpActivity(v, var_inc); }


    inline void Solver::varBumpActivity(Var v, double inc) {
        if((activity[v] += inc) > 1e100) {
            // Rescale:
            for(int i = 0 ; i < nVars() ; i++)
                activity[i] *= 1e-100;
            var_inc *= 1e-100;
        }

        // Update order_heap with respect to new activity:
        if(order_heap.inHeap(v))
            order_heap.decrease(v);
    }


    inline void Solver::claDecayActivity() { cla_inc *= (1 / clause_decay); }


    inline void Solver::claBumpActivity(Clause &c) {
        if((c.activity() += cla_inc) > 1e20) {
            // Rescale:
            for(int i = 0 ; i < learnts.size() ; i++)
                ca[learnts[i]].activity() *= 1e-20;
            cla_inc *= 1e-20;
        }
    }


    inline void Solver::checkGarbage(void) { return checkGarbage(garbage_frac); }


    inline void Solver::checkGarbage(double gf) {
        if(ca.wasted() > ca.size() * gf)
            garbageCollect();
    }


    inline bool Solver::locked(const Clause &c) const { return value(c[0]) == l_True && reason(var(c[0])) != CRef_Undef && ca.lea(reason(var(c[0]))) == &c; }


    inline void Solver::newDecisionLevel() { trail_lim.push(trail.size()); }


    inline int Solver::decisionLevel() const { return trail_lim.size(); }


    inline uint32_t Solver::abstractLevel(Var x) const { return 1 << (level(x) & 31); }


    inline lbool Solver::value(Var x) const { return assigns[x]; }


    inline lbool Solver::value(Lit p) const { return assigns[var(p)] ^ sign(p); }


    inline int Solver::nAssigns() const { return trail.size(); }


    inline int Solver::nClauses() const { return clauses.size(); }


    inline int Solver::nLearnts() const { return learnts.size(); }


    inline int Solver::nVars() const { return vardata.size(); }


    inline void Solver::setConfBudget(int64_t x) { conflict_budget = conflicts + x; }


    inline void Solver::setPropBudget(int64_t x) { propagation_budget = propagations + x; }


    inline void Solver::interrupt() { asynch_interrupt = true; }


    inline void Solver::clearInterrupt() { asynch_interrupt = false; }


    inline void Solver::budgetOff() { conflict_budget = propagation_budget = -1; }


    inline bool Solver::withinBudget() const {
        return !asynch_interrupt &&
               (conflict_budget < 0 || conflicts < (uint64_t) conflict_budget) &&
               (propagation_budget < 0 || propagations < (uint64_t) propagation_budget);
    }


// FIXME: after the introduction of asynchronous interrruptions the solve-versions that return a
// pure bool do not give a safe interface. Either interrupts must be possible to turn off here, or
// all calls to solve must return an 'lbool'. I'm not yet sure which I prefer.
    inline lbool Solver::solve() {
        budgetOff();
        return solve_();
    }


    inline bool Solver::okay() const { return ok; }


    // Display

    template<typename T>
    void printElement(T t) {
        std::cout << std::left << std::setw(15) << std::setfill(' ') << t;
    }



    /*
  Finite subsequences of the Luby-sequence:

  0: 1
  1: 1 1 2
  2: 1 1 2 1 1 2 4
  3: 1 1 2 1 1 2 4 1 1 2 1 1 2 4 8
  ...
 */
    inline double luby(double y, int x) {

        // Find the finite subsequence that contains index 'x', and the
        // size of that subsequence:
        int size, seq;
        for(size = 1, seq = 0 ; size < x + 1 ; seq++, size = 2 * size + 1);

        while(size - 1 != x) {
            size = (size - 1) >> 1;
            seq--;
            x = x % size;
        }

        return pow(y, seq);
    }




//=================================================================================================
// Debug etc:


//=================================================================================================
}

#endif
