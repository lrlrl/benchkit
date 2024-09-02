#include <vector>
#include <math.h>
#include <iostream>
#include <thread>
#include <algorithm>
#include <tuple>
#include <map>
#include <functional>
#include <unistd.h>
#include "./ThreadPool.cc"
using namespace std;


enum ClauseState {
        UNSAT,
        SAT,
        UNDEF
    };

class Solver {

    
    public:
    Solver(int num_threads) {
        this->num_threads = num_threads; 

    } 
    std::vector<std::vector<int>> clauses;
    int num_clauses;
    int nvars;
    int num_propagations; 
    vector<int> var_activities;
    map<int, vector<vector<int>>> variable_to_clauses;


    vector<thread> worker_threads;
    volatile int num_threads = 1;
    bool is_sat = false; 
    ThreadPool tpool;

    



    
    void backtrack(std::vector<int> &sol, int &currentVar,std::vector<int> &decisionsLeft, std::vector<tuple<int,int>> &trail) {
        tuple<int,int> finalDecision = trail.back();
        int finalDecisionVar = get<0>(finalDecision);
        int finalDecisionType = get<1>(finalDecision);

        while(sol.at(finalDecisionVar-1) == UNSAT || finalDecisionType == 1) {
            


            sol[finalDecisionVar-1] = UNDEF;
            trail.pop_back();
            decisionsLeft.push_back(finalDecisionVar);
            if (trail.empty())
                return;
            else
            {
                finalDecision = trail.back();
                finalDecisionVar = get<0>(finalDecision);
                finalDecisionType = get<1>(finalDecision);
            }
        }
        finalDecisionVar = get<0>(finalDecision);
        finalDecisionType = get<1>(finalDecision);


        if(sol.at(finalDecisionVar-1) == SAT) {
            sol[finalDecisionVar-1] = UNSAT;
            return;
        }

    }





    
    typedef std::tuple<std::vector<int>,
            std::vector<tuple<int,int>>,
            std::vector<int>,
            bool> result_tuple;
    void remove_lit(std::vector<int> &lits, int lit)
    {
       lits.erase(std::remove(lits.begin(), lits.end(), lit), lits.end()); 
    }



    int lit_already_propagated(vector<int> &propagated, int l) {
        for(int _l : propagated) {
            if (l == _l) {
                return 1;
            }
            else if (l == abs(_l)) {
                return -1;
            }

        }

        return 0;
    }
    template <typename T>
    void insert_all(vector<T> &dest, vector<T> &src) {
        for(auto x : src) {
            dest.push_back(x);
        }
    }

    result_tuple parallel_propagations(std::vector<int> prop_lits,
                                std::vector<int> solution,
                                std::vector<tuple<int,int>> trail,
                                std::vector<int> decisionsLeft)
    {
        vector<thread*> thrss;
        result_tuple final_result = {{},{},{},true};
        for( int p : prop_lits) {
            thread thr([&](){
                vector<int> prop_lits;
                prop_lits.push_back(p);
                result_tuple result_tup = do_propagations(prop_lits,solution,trail,decisionsLeft);
                insert_all(get<0>(final_result),get<0>(result_tup));
                insert_all(get<1>(final_result),get<1>(result_tup));
                insert_all(get<2>(final_result),get<2>(result_tup));
                get<3>(final_result) = get<3>(final_result) && get<3>(result_tup);
            } );
            thrss.push_back(&thr);
        }
        for(int i = 0; i<thrss.size();i++){
            thrss[i]->join();
        }
        return final_result;


    }

    result_tuple do_propagations(std::vector<int> &prop_lits,
                                std::vector<int> &solution,
                                std::vector<tuple<int,int>> &trail,
                                std::vector<int> &decisionsLeft
                                 )
    {   
        vector<int> already_propagated;
        while(!prop_lits.empty() ) {
            int l = prop_lits[0];
            prop_lits.erase(prop_lits.begin());
            int lit_propagated = lit_already_propagated(already_propagated,l);
            if (lit_propagated) 
                continue;
            else if (lit_propagated == -1) {
                return {{},{},{},false};
            }
            remove_lit(decisionsLeft,abs(l));
            trail.push_back({abs(l),1});
            if (l > 0 && (solution.at(l - 1) != UNSAT || solution.at(l - 1) == UNDEF))
                solution.at(l - 1) = SAT;
            else if (l < 0 && (solution.at(abs(l) - 1) != SAT || solution.at(abs(l) - 1) == UNDEF))
                solution.at(abs(l) - 1) = UNSAT;
            else return {{},{},{},false};
            already_propagated.push_back(l);

            find_prop_lits(solution,prop_lits,{l});

        }
        return {solution,trail,decisionsLeft,true};
    }

    bool propagate(std::vector<int> &prop_lits,
                                std::vector<int> &solution,
                                std::vector<tuple<int,int>>&trail,
                                std::vector<int> &decisionsLeft,
                                std::vector<int> &potential_propagations )
        {   

            result_tuple res_tuple = do_propagations(prop_lits,solution,trail,decisionsLeft);
            if (std::get<3>(res_tuple)) {
                solution = std::get<0>(res_tuple);
                trail = std::get<1>(res_tuple);
                decisionsLeft = std::get<2>(res_tuple);
                return true;
            } else return false;
        }



    void search(std::vector<int> solution,
                vector<int> decisionsLeft,
                vector<tuple<int,int>> trail,
                vector<int> decisionsDelegated,
                vector<int> possible_propagators)
    {

        
        
        int currentVar = 0;
        int i = 1;
        std::vector<int> prop_lits;
        find_prop_lits(solution,prop_lits,possible_propagators);
        
        if(!propagate(prop_lits,solution,trail,decisionsLeft, possible_propagators))
        {

            return;
        }
        else if (decisionsLeft.empty())
        {
            is_sat = is_sat || check(solution);
            return;
        }

        do {
            prop_lits.clear();
            find_prop_lits(solution,prop_lits,possible_propagators);
            possible_propagators.clear(); 
            ClauseState isSat = check(solution);
            if (!prop_lits.empty()) {
                if(propagate(prop_lits,solution,trail,decisionsLeft,possible_propagators))  {
                    prop_lits.clear();
                    isSat = check(solution);
                    if(isSat == UNDEF) {
                    prop_lits.clear();
                        possible_propagators.clear();
                        continue;
                    }
                    else if (isSat == SAT)
                    {
                        is_sat = true;
                        return;
                    }
                } else{

                 prop_lits.clear();
                 }
                };


            if (isSat == SAT) {
                is_sat = true;
                return; 
            }


            else if (isSat == UNSAT)
            {
                backtrack(solution, currentVar, decisionsLeft, trail);
                possible_propagators.push_back(get<0>(trail.back()));
                if (trail.empty() || find(decisionsDelegated.begin(),decisionsDelegated.end(),get<0>(trail.back())) != decisionsDelegated.end()) {
                    return;
                }
            }
            else
            {
                sort(decisionsLeft.begin(), decisionsLeft.end(), [this](int a, int b)
                     {
                    if (var_activities[a-1]==var_activities[b-1])
                      return a < b;                    
                    else return var_activities[a-1] < var_activities[b-1]; });

                int decision = decisionsLeft.back();
                decisionsLeft.pop_back();
                trail.push_back({decision,0});
                possible_propagators.push_back(decision); 
                if (tpool.queue_size() < 100)
                {
                    tpool.enqueueJob([this, trail, solution, decisionsLeft, decision, decisionsDelegated]()
                                     {
                vector<int> sol2;
                sol2.resize(nvars);
                sol2 = solution;
                sol2.at(decision-1) = UNSAT; 
                search(sol2,decisionsLeft,trail,decisionsDelegated,{-decision}); });

                    decisionsDelegated.push_back(decision);
                }

                solution.at(decision - 1) = SAT;
            }
           
        } while(!is_sat);


    }

    




    
    ClauseState literal_status(int &l,std::vector<int> &solution){
        if(solution.at(abs(l)-1) == UNDEF) {
                return UNDEF;
            } else if (solution.at(abs(l)-1) == SAT && l > 0 || solution.at(abs(l)-1) == UNSAT && l < 0) {
                return SAT;
            } else return UNSAT;
    }

    int is_unit(std::vector<int> &clause,std::vector<int> &solution) {
        int num_sat = 0;
        int num_undef = 0;
        int undef_lit = 2;
        for(int &l : clause) {
            ClauseState lit_status = literal_status(l,solution);
            if (lit_status == SAT) {
                num_sat++;
            } else if (lit_status == UNDEF) {
                undef_lit = l;
                num_undef++;
            } 
        }
        if (num_sat >= 1) {
            return false;
        } else if (num_undef == 1)
        {
            return undef_lit;
        } else return false;
        
    }

    

    std::vector<int> find_prop_lits(std::vector<int> &solution,vector<int> &prop_lits ,vector<int> possible_propagators)
    {
        for (int  &possible_prop : possible_propagators) {
           vector<vector<int>>* possible_clauses = &variable_to_clauses[abs(possible_prop)];
        for(std::vector<int> &clause : *possible_clauses){
            int lit = is_unit(clause,solution);
            if (lit != 0) {
                bool included = false;
                for(int &l : prop_lits) {
                    if (abs(l) == abs(lit)) {
                        included = true;
                    }
                
                
                }
                if (!included) prop_lits.push_back(lit);
            }
            
        } 
    }

        return prop_lits;
    }
   ClauseState check(std::vector<int> &solution) {
    bool sat = true;
    for(std::vector<int> &clause : clauses) {
        ClauseState cstate = check_partial_clause(clause,solution);
        if(cstate == UNDEF) {
        return UNDEF;
        } else if(cstate == UNSAT) {
        return UNSAT;
        
        } else {
        }
        }
    return SAT;
    }


   ClauseState check_partial_clause(std::vector<int> &clause,std::vector<int> &solution) {
     bool sat = false;
     bool undef = true;
     for(int &l : clause) { 
       int lit_state = solution.at(abs(l)-1);
       if(lit_state == UNDEF) return (ClauseState) lit_state;
       else if((lit_state == SAT && l > 0) || (lit_state == UNSAT && l < 0)) return SAT;
     } 
     return UNSAT;

   }

    void print_Trail(vector<tuple<int, int>> trail)
    {
    for (tuple<int,int> x : trail){
        cout << "{" << get<0>(x) << " " << get<1>(x) << "}, " ;

    }
    cout << endl;
    }

   void print_vector(std::vector<int> v) {
        for(int x : v) {
            std::cout << x << " ";
        }
        std::cout << std::endl;
   }

   bool check_clause(std::vector<int> solution,std::vector<int> clause) {
    bool sat = false;
    for(int l : clause) {
        int sol = (solution.at(abs(l)-1));
        if (sol ==0 && l < 0) {
            sat = sat || true;
        } else if(sol == 1 && l >0 ) {
            sat = sat || true;
        }
    }
    return sat;
   }

   void calc_variable_to_clauses()
   {
    var_activities.resize(nvars,0);
    for(vector<int> clause : clauses) {
        for(int l : clause) {
            variable_to_clauses[abs(l)].push_back(clause);
            var_activities[abs(l)-1]++;
        }
    }

   }


    vector<int> find_unit_clauses() {
        vector<int> unit_clauses;
        for(vector<int> c : clauses) {
            if(c.size()==1) {
                unit_clauses.push_back(c[0]);
            }
        }
        return unit_clauses;
    }



std::vector<int> initSol(int nvars) {
    std::vector<int> initSolution;
    initSolution.resize(nvars,(int)UNDEF); 
    return initSolution;
}


   bool solve() {
    calc_variable_to_clauses();

    std::vector<int> decisionsLeft;
    decisionsLeft.reserve(nvars);
    for (int i = nvars; i > 0; i--)
    {
        decisionsLeft.push_back(i);
    }

    std::vector<tuple<int, int>> trail;
    trail.reserve(nvars);

    std::vector<int> sol = initSol(nvars);


    tpool.start(num_threads); 
    tpool.enqueueJob([&]() {

        std::vector<int> possible_propagators = find_unit_clauses();
        search(sol,decisionsLeft,trail,{},possible_propagators);


    });
    std::this_thread::sleep_for (std::chrono::milliseconds(100));
    while(tpool.busy()) {
        if (is_sat) {
            break;
        }
    }


    tpool.stop();
    bool sat = false;
    return is_sat;
   }
};