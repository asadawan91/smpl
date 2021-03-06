#ifndef __dintdrift_h__
#define __dintdrift_h__

#include "dynamical_system.h"
#include <cassert>
using namespace std;

class dintdrift_optimization_data_c : public optimization_data_c
{
  public:
    double T;
    bool is_initialized;

    dintdrift_optimization_data_c() : is_initialized(false) {}
    ~dintdrift_optimization_data_c(){}
};

class dintdrift_c : public dynamical_system_c<state_c<4>, control_c<2>, dintdrift_optimization_data_c>
{
  public:
    typedef dynamical_system_c<state_c<4>, control_c<2>, optimization_data_c> dynamical_system_t;
    typedef typename dynamical_system_t::state_t state_t;
    typedef typename dynamical_system_t::control_t control_t;
    typedef typename dynamical_system_t::trajectory_t trajectory_t;
    typedef dintdrift_optimization_data_c dint_opt_data_t;
    
    dintdrift_c() {}
    
    int get_plotter_state(const state_t& s, double* ps)
    {
      ps[0] = s[0];
      ps[1] = s[1];
      ps[2] = 0;
      return 0;
    }

    int extend_to(const state_t& si, const state_t& sf,
        trajectory_t& traj, dint_opt_data_t& opt_data)
    {
      return 0;
    }
    
    double evaluate_extend_cost(const state_t& si, const state_t& sf,
        dint_opt_data_t& opt_data)
    {
      if(opt_data.is_initialized)
        return opt_data.T;
      
    }
};
#endif
