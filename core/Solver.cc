#include <math.h>


#include "mtl/Sort.h"
#include "core/Solver.h"

using namespace CDCL;



//=================================================================================================
// Search and solve
//=================================================================================================

/**
 * Search for a model the specified number of conflicts.
 * @param nof_conflicts
 * @return l_True id a solution is found. l_False if the formula is UNSAT, l_Undef otherwise.
 */

lbool Solver::search(int nof_conflicts) {
    assert(ok);
    int backtrack_level, lbd;
    int nbConflictsInCurrentRun = 0;
    vec<Lit> learnt_clause;

    for(;;) {
        CRef confl = propagate();                                // BCP (propagate all unit clauses until a fix point or a conflict is reached

        if(confl != CRef_Undef) {  // CONFLICT
            conflicts++;
            nbConflictsInCurrentRun++;

            if(decisionLevel() == 0) return l_False;             // Formula is UNSAT


            trailQueue.push(trail.size());
            // BLOCK RESTART (CP 2012 paper)
            if(conflicts > 10000 && lbdQueue.isvalid() && trail.size() > 1.4 * trailQueue.getavg())
                lbdQueue.fastclear();



            analyze(confl, learnt_clause, backtrack_level, lbd); // Analyze

            // Glucose restarts
            lbdQueue.push(lbd);
            sumLBD += lbd;

            cancelUntil(backtrack_level);                        // Backjump

            if(learnt_clause.size() == 1)
                uncheckedEnqueue(learnt_clause[0]);              // Unary clause is learnt, assign the literal at decision level 0
            else {
                CRef cr = ca.alloc(learnt_clause, true);         // Create a new clause
                learnts.push(cr);                                // Add it in the learnt clauses database
                attachClause(cr);                                // Attach it
                claBumpActivity(ca[cr]);                         // Bump its activity
                uncheckedEnqueue(learnt_clause[0], cr);          // Assign the asserting literal, its reason is the asserting clause
                ca[cr].lbd(lbd);
            }

            varDecayActivity();                                  // Decay the activity of all variables
            claDecayActivity();                                  // Decay the activity of all clauses

            if(conflicts % 1000 == 0 && verbosity >= 1) printIntermediateStats();

        } else {  // NO CONFLICT
            if(lbdQueue.isvalid() && (lbdQueue.getavg() * 0.8) > (sumLBD / conflicts)) { // Glucose restarts
                lbdQueue.fastclear();  // Clear the queue
                cancelUntil(0);
                return l_Undef;
            }

            if(conflicts >= nextReduceDB) { // It is time to reduce the learnt clauses database
                reduceDB();
                nextReduceDB = conflicts + 2000 + 1000 * nb_reducedb;
            }

            Lit next = pickBranchLit();            // New decision literal

            if(next == lit_Undef) return l_True;   // Model found

            newDecisionLevel();                    // Increase decision level and enqueue 'next'
            uncheckedEnqueue(next);                // A decision literal, it has no reason
        }
    }
}


/**
 * Solve a CNF formula
 * @return l_True, L_False or l_Undef depending if the formula is proved SAT, UNSAT or if the search ends before finding the result.
 */

lbool Solver::solve_() {
    model.clear();
    if(!ok) return l_False;

    if(verbosity >= 1) {
        printf("c ");
        printElement("restarts");
        printElement("conflicts");
        printElement("decisions");
        printElement("avg res");
        printElement("reduceDB");
        printElement("avg |learnt|");
        printElement("removed");
        printElement("Progress");
        std::cout << std::endl;
    }

    lbool status = l_Undef;

    int curr_restarts = 0;
    while(status == l_Undef) {
        starts++;
        double rest_base = luby_restart ? luby(2, curr_restarts) : pow(1.5, curr_restarts);
        status = search(rest_base * 32);  // Search for a limited number of conflict
        if(!withinBudget()) break;
        curr_restarts++;
    }

    if(status == l_True) {
        model.growTo(nVars()); // Extend & copy model:
        for(int i = 0; i < nVars(); i++) model[i] = value(i);
    }

    cancelUntil(0);
    return status;
}


//=================================================================================================
// Heuristic, enqueue, propagation and backtrack
//=================================================================================================


/**
 * Select the next literal not assigned with the highest activity
 * @return lit_Undef if none exist
 */

Lit Solver::pickBranchLit() {
    Var next = var_Undef;

    while(next == var_Undef || value(next) != l_Undef)
        if(order_heap.empty())
            return lit_Undef;
        else
            next = order_heap.removeMin();
    decisions++;
    return mkLit(next, polarity[next]);
}


/**
 *    Propagates all enqueued facts. If a conflict arises, the conflicting clause is returned,
 *    otherwise CRef_Undef.
 *
 *    Post-conditions: the propagation queue is empty, even if there was a conflict.
 * @return CRef_Undef or a clause reference
 */

CRef Solver::propagate() {
    CRef confl = CRef_Undef;
    watches.cleanAll();

    while(qhead < trail.size()) {
        Lit p = trail[qhead++];          // 'p' is enqueued fact to propagate.
        vec<Watcher> &ws = watches[p];   // The clauses watched by p
        Watcher *i, *j, *end;
        propagations++;

        for(i = j = (Watcher *) ws, end = i + ws.size(); i != end;) {

            Lit blocker = i->blocker;
            if(value(blocker) == l_True) { // Try to avoid inspecting the clause
                *j++ = *i++;               // The current clause is always watched by p
                continue;
            }

            // Make sure the false literal is data[1]
            CRef cr = i->cref;
            Clause &c = ca[cr];
            Lit false_lit = ~p;
            if(c[0] == false_lit)
                c[0] = c[1], c[1] = false_lit;
            assert(c[1] == false_lit);
            i++;

            // If 0th watch is true, then clause is already satisfied.
            Lit first = c[0];
            Watcher w = Watcher(cr, first);
            if(first != blocker && value(first) == l_True) {
                *j++ = w;
                continue;
            }

            // Look for new watch
            for(int k = 2; k < c.size(); k++)
                if(value(c[k]) != l_False) {  // A new watcher for this clause: c[k]
                    c[1] = c[k];              // Invert c[k] and c[1] (invariant...)
                    c[k] = false_lit;
                    watches[~c[1]].push(w);
                    goto NextClause;
                }

            // Did not find watch -- clause is unit under assignment:
            *j++ = w;
            if(value(first) == l_False) { // The first watch is false, a conflict occurs
                confl = cr;               // With this clause
                qhead = trail.size();     // Do not forget to put qhead at the end
                // Copy the remaining watches:
                while(i < end)
                    *j++ = *i++;
            } else
                uncheckedEnqueue(first, cr);

            NextClause:;
        }
        ws.shrink(i - j);  // Remove unwatched clauses by ps
    }
    return confl;
}


/**
 * Enqueue a literal, set its value, and store its reason (CRef_Undef if it is a decision literal)
 * @param p the literal to enqueue
 * @param from the reason
 * */

void Solver::uncheckedEnqueue(Lit p, CRef from) {
    assert(value(p) == l_Undef);
    assigns[var(p)] = lbool(!sign(p));                    // The polarity of the variable
    vardata[var(p)] = mkVarData(from, decisionLevel());   // Store the level and the reason
    trail.push_(p);                                       // Add the literal in the trail
}


/**
 * Revert to the state at given level (keeping all assignment at 'level' but not beyond).
 * @param level
 */

void Solver::cancelUntil(int level) {
    if(decisionLevel() > level) {
        for(int c = trail.size() - 1; c >= trail_lim[level]; c--) {  // For all propagated literal
            Var x = var(trail[c]);
            assigns[x] = l_Undef;                                      // Unassign it
            polarity[x] = sign(trail[c]);                              // Save its polarity
            insertVarOrder(x);                                         // Insert it in the heap
        }
        qhead = trail_lim[level];                                      // Set the head of the queue
        trail.shrink(trail.size() - trail_lim[level]);                 // Remove all propagations
        trail_lim.shrink(trail_lim.size() - level);                    // Reduce the trail_lim
        assert(trail_lim.size() == level);

    }
}


/**
 * Analyze conflict and produce a reason clause
 *
 * @param confl the reference of the clause in conflict
 * @param out_learnt the learnt clause. out_learnt[0] is the asserting literal. If out_learnt.size() > 1 then 'out_learnt[1]'
 * has the greatest decision level of the rest of literals.
 * @param out_btlevel the backtrack level
 */

void Solver::analyze(CRef confl, vec<Lit> &out_learnt, int &out_btlevel, int &lbd) {
    int nbResolutionsToPerform = 0;

    out_learnt.clear();
    Lit p = lit_Undef;

    // Generate conflict clause:
    out_learnt.push();      // leave room for the asserting literal
    int index = trail.size() - 1;

    do {
        assert(confl != CRef_Undef);        // (otherwise should be UIP)
        Clause &c = ca[confl];
        nb_resolutions++;
        if(c.learnt()) claBumpActivity(c);  // The clause is useful

        for(int j = (p == lit_Undef) ? 0 : 1; j < c.size(); j++) {
            Lit q = c[j];

            if(!seen[var(q)] && level(var(q)) > 0) {
                varBumpActivity(var(q));               // VSIDS favors variables that appear recently in conflict analysis
                seen[var(q)] = 1;                      // process a variable only once
                if(level(var(q)) >= decisionLevel())   // The literal is assigned at the last level, one need to make a resolution
                    nbResolutionsToPerform++;
                else
                    out_learnt.push(q);                // The literal was assigned before, add it to the asserting clause
            }
        }

        while(!seen[var(trail[index--])]);             // Select next useful literal on the last level
        p = trail[index + 1];                          // It is this one
        confl = reason(var(p));                        // It has a reason
        seen[var(p)] = 0;                              // Ok, we processed it
        nbResolutionsToPerform--;                      // One resolution was made

    } while(nbResolutionsToPerform > 0);
    out_learnt[0] = ~p;                                // This is the asserting literal, add it on position 0.

    // Find correct backtrack level:
    if(out_learnt.size() == 1)
        out_btlevel = 0;
    else {
        int max_i = 1;
        // Find the first literal assigned at the next-highest level:
        for(int i = 2; i < out_learnt.size(); i++)
            if(level(var(out_learnt[i])) > level(var(out_learnt[max_i]))) max_i = i;

        // Swap-in this literal at index 1:
        Lit p = out_learnt[max_i];
        out_learnt[max_i] = out_learnt[1];
        out_learnt[1] = p;
        out_btlevel = level(var(p));
    }

    lbd = computeLBD(out_learnt);
    for(int j = 0; j < out_learnt.size(); j++) seen[var(out_learnt[j])] = 0;    // ('seen[]' is now cleared)
}



//=================================================================================================
// Reduction of the learnt clause database
//=================================================================================================

struct reduceDB_lt {
    ClauseAllocator &ca;


    reduceDB_lt(ClauseAllocator &ca_) : ca(ca_) {}


    bool operator()(CRef x, CRef y) {
        // Main criteria... Like in MiniSat we keep all binary clauses
        if(ca[x].size() > 2 && ca[y].size() == 2) return 1;

        if(ca[y].size() > 2 && ca[x].size() == 2) return 0;
        if(ca[x].size() == 2 && ca[y].size() == 2) return 0;

        // Second one  based on literal block distance
        if(ca[x].lbd() > ca[y].lbd()) return 1;
        if(ca[x].lbd() < ca[y].lbd()) return 0;


        // Finally we can use old activity or size, we choose the first one
        return ca[x].activity() < ca[y].activity();
        //return x->size() < y->size();

    }
};


/**
 * Remove half of the learnt clauses, minus the clauses locked by the current assignment.
 */

void Solver::reduceDB() {
    int i, j;
    nb_reducedb++;
    sort(learnts, reduceDB_lt(ca));

    // Don't delete binary or locked clauses. From the rest, delete clauses from the first half
    // and clauses with activity smaller than 'extra_lim':
    for(i = j = 0; i < learnts.size(); i++) {
        Clause &c = ca[learnts[i]];
        if(c.size() > 2 && !locked(c) && i < learnts.size() / 2)
            removeClause(learnts[i]);
        else
            learnts[j++] = learnts[i];
    }
    learnts.shrink(i - j);
    checkGarbage();
}


//=================================================================================================
// Add variables, clauses...
//=================================================================================================


/**
 * Add a new variable. Set the initial polarity
 * @param sign the initial polarity
 * @return the index of the new variable (starting from 0)
 */

Var Solver::newVar(bool sign) {
    int v = nVars();
    watches.init(mkLit(v, false));             // The watched clauses for v
    watches.init(mkLit(v, true));              // The watched clauses for ~v
    assigns.push(l_Undef);                     // The variable is not assigned
    vardata.push(mkVarData(CRef_Undef, 0));    // varData.cr : store the reason of the literal, varData.l the level (if variable is assigned)
    activity.push(0);                          // The initial activity
    seen.push(0);                              // Useful for conflict analysis
    polarity.push(sign);                       // The progress saving phase
    insertVarOrder(v);                         // Add it to the heap (VSIDS)
    trail.capacity(v + 1);
    levelTagged.push(0);                       // For computing LBD
    return v;
}


/**
 * Add a clause
 * Simplify it, if unary, then propagate, otherwise store and attach it.
 * @param ps the vector of literals
 * @return true if ok, false if a conflict occurs
 */

bool Solver::addClause_(vec<Lit> &ps) {
    assert(decisionLevel() == 0);
    if(!ok) return false;

    // Check if clause is satisfied and remove false/duplicate literals:
    sort(ps);
    Lit p;
    int i, j;
    for(i = j = 0, p = lit_Undef; i < ps.size(); i++)       // Check all literals
        if(value(ps[i]) == l_True || ps[i] == ~p)           // A true literal: the clause is sat
            return true;
        else if(value(ps[i]) != l_False && ps[i] != p)
            ps[j++] = p = ps[i];                           // The literal is not false
    ps.shrink(i - j);                                      // Remove useless literals (false)

    if(ps.size() == 0)                                     // Trivial unsat problem
        return ok = false;
    else if(ps.size() == 1) {                              // Unit clause
        uncheckedEnqueue(ps[0]);                           // propagate the literal
        return ok = (propagate() == CRef_Undef);
    } else {
        CRef cr = ca.alloc(ps, false);                     // Create the clause
        clauses.push(cr);                                  // Add it
        attachClause(cr);                                  // Attach it
    }

    return true;
}


/**
 * Attach a clause reference. Set the first two literals as sentinels.
 * @param cr
 */

void Solver::attachClause(CRef cr) {
    const Clause &c = ca[cr];
    assert(c.size() > 1);
    watches[~c[0]].push(Watcher(cr, c[1]));
    watches[~c[1]].push(Watcher(cr, c[0]));
    if(c.learnt())
        nb_lits_in_learnts += c.size();
}


/**
 * Detach a clause reference. Remove the two sentinels.
 * @param cr
 * @param strict
 */

void Solver::detachClause(CRef cr, bool strict) {
    const Clause &c = ca[cr];
    assert(c.size() > 1);

    if(strict) {
        remove(watches[~c[0]], Watcher(cr, c[1]));
        remove(watches[~c[1]], Watcher(cr, c[0]));
    } else {
        // Lazy detaching: (NOTE! Must clean all watcher lists before garbage collecting this clause)
        watches.smudge(~c[0]);
        watches.smudge(~c[1]);
    }
    if(c.learnt())
        nb_lits_in_learnts -= c.size();

}


/**
 * Remove a clause reference. Detach it and free the memory
 * @param cr
 */

void Solver::removeClause(CRef cr) {
    Clause &c = ca[cr];
    detachClause(cr);
    // Don't leave pointers to free'd memory!
    if(locked(c)) vardata[var(c[0])].reason = CRef_Undef;
    c.mark(1);
    ca.free(cr);
    nb_removed_clauses++;
}


//=================================================================================================
// Minor methods:
//=================================================================================================

double Solver::progressEstimate() const {
    double progress = 0;
    double F = 1.0 / nVars();

    for(int i = 0; i <= decisionLevel(); i++) {
        int beg = i == 0 ? 0 : trail_lim[i - 1];
        int end = i == decisionLevel() ? trail.size() : trail_lim[i];
        progress += pow(F, i) * (end - beg);
    }

    return progress / nVars();
}


void Solver::printIntermediateStats() {
    printf("c ");
    printElement(starts);
    printElement(conflicts);
    printElement(decisions);
    printElement((int) (nb_resolutions / conflicts));
    printElement(nb_reducedb);
    printElement(nb_lits_in_learnts / learnts.size());
    printElement(nb_removed_clauses);
    printElement(progressEstimate() * 100);
    std::cout << std::endl;
}


int Solver::computeLBD(vec<Lit> &lits) {
    int nblevels = 0;
    FLAG++;
    for(int i = 0; i < lits.size(); i++) {
        int l = level(var(lits[i]));
        if(levelTagged[l] != FLAG) {
            levelTagged[l] = FLAG;
            nblevels++;
        }
    }
    return nblevels;
}


//=================================================================================================
// Constructor/Destructor:
//=================================================================================================



// Options:
static const char *_cat = "CORE";

static DoubleOption opt_var_decay(_cat, "var-decay", "The variable activity decay factor", 0.95, DoubleRange(0, false, 1, false));
static DoubleOption opt_clause_decay(_cat, "cla-decay", "The clause activity decay factor", 0.999, DoubleRange(0, false, 1, false));
static BoolOption opt_luby_restart(_cat, "luby", "Use the Luby restart sequence", true);
static DoubleOption opt_garbage_frac(_cat, "gc-frac", "The fraction of wasted memory allowed before a garbage collection is triggered", 0.20,
                                     DoubleRange(0, false, HUGE_VAL, false));


Solver::Solver() :

// Parameters (user settable):
//
        verbosity(0), var_decay(opt_var_decay), clause_decay(opt_clause_decay),
        luby_restart(opt_luby_restart),
        nextReduceDB(2000),
        garbage_frac(opt_garbage_frac),
        // Statistics: (formerly in 'SolverStats')
        //
        starts(0), decisions(0), rnd_decisions(0), propagations(0), conflicts(0), nb_removed_clauses(0), nb_reducedb(0),
        nb_resolutions(0), nb_lits_in_learnts(0),
        ok(true), cla_inc(1), var_inc(1), watches(WatcherDeleted(ca)), qhead(0),
        order_heap(VarOrderLt(activity)), progress_estimate(0), FLAG(0)

        // Resource constraints:
        //
        , conflict_budget(-1), propagation_budget(-1), asynch_interrupt(false) {

    lbdQueue.initSize(50);
    trailQueue.initSize(5000);
    sumLBD = 0;

}


Solver::~Solver() {
}




//=================================================================================================
// Garbage Collection methods:

void Solver::relocAll(ClauseAllocator &to) {
    // All watchers:
    //
    // for (int i = 0; i < watches.size(); i++)
    watches.cleanAll();
    for(int v = 0; v < nVars(); v++)
        for(int s = 0; s < 2; s++) {
            Lit p = mkLit(v, s);
            // printf(" >>> RELOCING: %s%d\n", sign(p)?"-":"", var(p)+1);
            vec<Watcher> &ws = watches[p];
            for(int j = 0; j < ws.size(); j++)
                ca.reloc(ws[j].cref, to);
        }

    // All reasons:
    //
    for(int i = 0; i < trail.size(); i++) {
        Var v = var(trail[i]);

        if(reason(v) != CRef_Undef && (ca[reason(v)].reloced() || locked(ca[reason(v)])))
            ca.reloc(vardata[v].reason, to);
    }

    // All learnt:
    //
    for(int i = 0; i < learnts.size(); i++)
        ca.reloc(learnts[i], to);

    // All original:
    //
    for(int i = 0; i < clauses.size(); i++)
        ca.reloc(clauses[i], to);
}


void Solver::garbageCollect() {
    // Initialize the next region to a size corresponding to the estimated utilization degree. This
    // is not precise but should avoid some unnecessary reallocations for the new region:
    ClauseAllocator to(ca.size() - ca.wasted());

    relocAll(to);
    if(verbosity >= 2)
        printf("|  Garbage collection:   %12d bytes => %12d bytes             |\n",
               ca.size() * ClauseAllocator::Unit_Size, to.size() * ClauseAllocator::Unit_Size);
    to.moveTo(ca);
}
