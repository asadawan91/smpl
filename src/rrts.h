#ifndef __rrts_h__
#define __rrts_h__

#include <list>
#include <set>
#include <vector>
#include <set>
#include <cfloat>
#include "kdtree.h"
#include <algorithm>
#include <tuple>
#include <functional>
#include <unordered_map>

#include "system.h"
using namespace std;

template<class system_tt> class vertex_c;
template<class system_tt> class edge_c;

template<class system_tt>
class vertex_c
{
  public:
    typedef vertex_c<system_tt> vertex;
    typedef edge_c<system_tt> edge;

    typedef typename system_tt::state state_t;
    typedef typename system_tt::control control_t;
    typedef typename system_tt::opt_data_t opt_data_t;
    typedef typename system_tt::cost_t cost_t;
    typedef typename system_tt::trajectory trajectory_t;

    vertex* parent;
    state_t state;
    set<vertex*> children;

    int mark;

    cost_t cost_from_root;
    cost_t cost_from_parent;
    edge* edge_from_parent;

    vertex_c()
    {
      parent = NULL;
      edge_from_parent = NULL;
      mark = 0;
    }
    ~vertex_c()
    {
      if(edge_from_parent)
        delete edge_from_parent;
    }
    vertex_c(const state_t& si)
    {
      parent = NULL;
      edge_from_parent = NULL;
      state = si;
      mark = 0;
    }
    string tostring() const
    {
      return state->tostring();
    }
    void print_branch(string prefix)
    {
      cout << prefix << tostring() << " - cost: "; cost_from_root->print();
      for(auto& pc : children)
        pc->print_branch(prefix+"\t");
    }
    state_t& get_state() const { return state;};
    vertex& get_parent() const {return *parent;};
    cost_t& get_cost() const {return cost_from_root;};
};

template<class system_tt>
class edge_c
{
  public:
    typedef edge_c<system_tt> edge;

    typedef typename system_tt::state state;
    typedef typename system_tt::control control;
    typedef typename system_tt::opt_data_t opt_data_t;
    typedef typename system_tt::trajectory trajectory;
    typedef typename system_tt::cost_t cost_t;  

    const state* start_state;
    const state* end_state;

    cost_t cost;
    opt_data_t opt_data;

    edge_c()
    {
      end_state = NULL;
      start_state = NULL;
    };

    edge_c(const state* si, const state* se, cost_t& c, opt_data_t& opt_data_in)
    {
      start_state = si;
      end_state = se;
      opt_data = opt_data_in;
      cost = c;
    }
};

template<class system_tt>
class rrts_c
{
  public:
    typedef typename system_tt::state state;
    typedef typename system_tt::control control;
    typedef typename system_tt::opt_data_t opt_data_t;
    typedef typename system_tt::trajectory trajectory_t;
    typedef typename system_tt::cost_t cost_t;  
    const static size_t num_dim = system_tt::N;

    typedef vertex_c<system_tt> vertex;
    typedef edge_c<system_tt> edge;
    typedef system_tt system_t;

    typedef struct kdtree kdtree_t;
    typedef struct kdres kdres_t;

    system_t system;

    int num_vertices;
    list<vertex*> list_vertices;
    float gamma;
    float goal_sample_freq;
    bool do_branch_and_bound;

    vertex* root;
    cost_t lower_bound_cost;
    vertex* lower_bound_vertex;
    kdtree_t* kdtree;

    static int debug_counter;

    rrts_c()
    {
      gamma = 2.5;
      goal_sample_freq = 0.1;
      do_branch_and_bound = true;

      root = NULL;
      lower_bound_vertex = NULL;

      kdtree = NULL;
      num_vertices = 0;
    }
    ~rrts_c()
    {
      if(kdtree)
        kd_free(kdtree);
      clear_list_vertices();
    }

    void clear_list_vertices()
    {
      for(auto& i : list_vertices)
        delete i;
      list_vertices.clear();
      num_vertices = 0;
    }
    int set_root(const state& rs)
    {
      root = new vertex(rs);
      root->cost_from_root = system.get_zero_cost();
      root->cost_from_parent = system.get_zero_cost();
      root->edge_from_parent = NULL;
      root->parent = NULL;
      root->children.clear();

      insert_into_kdtree(*root);
      //update_best_vertex(*root);
      return 0; 
    }
    int initialize(const state& rs, bool do_branch_and_bound_in=true)
    {
      clear_list_vertices();  
      lower_bound_cost = system.get_inf_cost();
      lower_bound_vertex = NULL;
      do_branch_and_bound = do_branch_and_bound_in;

      if(kdtree)
        kd_free(kdtree);
      kdtree = kd_create(num_dim);

      set_root(rs);
      root->state.print(cout,"set root to:", "\n");

      return 0;
    }

    int iteration()
    {
      // 1. sample
      state sr;
      int ret = 0;
      float p = RANDF;
      if(p < goal_sample_freq)
        ret = system.sample_in_goal(sr);
      else
        ret = system.sample_state(sr);
      if(ret)
        return 1;

      // 2. compute nearest vertices
      vector<vertex*> near_vertices;
      if(get_near_vertices(sr, near_vertices))
        return 2;

      // 3. best parent
      vertex* best_parent = NULL;
      edge* edge_from_parent = NULL;
      if(find_best_parent(sr, near_vertices, best_parent, edge_from_parent))
        return 3;

      // 4. draw edge to parent from new vertex
      vertex* new_vertex = insert_edge(*best_parent, *edge_from_parent);
      if(!new_vertex)
        return 4;

      // 5. rewire
      if(near_vertices.size())
        rewire_vertices(*new_vertex, near_vertices);

      return 0;
    }

    int insert_into_kdtree(vertex& v)
    {
      float* key = new float[num_dim];
      system.get_key(v.state, key);
      kd_insertf(kdtree, key, &v);
      delete[] key;

      list_vertices.push_back(&v);
      num_vertices++;
      return 0;
    }

    vertex& get_root_vertex() {return *root;};
    cost_t get_best_cost()    {return lower_bound_cost;};
    vertex& get_best_vertex() {return *lower_bound_vertex;}

    int get_best_trajectory(trajectory_t& best_traj)
    {
      if(!lower_bound_vertex)
        return 1;

      best_traj.clear();
      bool check_obstacles = false;
      vertex* vc = lower_bound_vertex;
      while(vc)
      {
        vertex* vparent = vc->parent;
        if(vparent)
        {
          trajectory_t traj_from_parent;
          system.extend_to(vparent->state, vc->state, check_obstacles,
              traj_from_parent, vc->edge_from_parent->opt_data);
          traj_from_parent.reverse();
          best_traj.append(traj_from_parent);
        }
        vc = vparent;
      }
      best_traj.reverse();
      return 0;
    }

    int get_near_vertices(const state& s, vector<vertex*>& near_vertices)
    {
      int toret = 0;
      float* key = new float[num_dim];
      system.get_key(s, key);

      float rn = gamma*pow(log(num_vertices + 1.0)/(num_vertices+1.0), 1.0/(float)num_dim);
      kdres_t* kdres = kd_nearest_rangef(kdtree, key, rn);

      int num_near_vertices = kd_res_size(kdres);
      if(!num_near_vertices)
      {
        kd_res_free(kdres);

        // get nearest vertex
        kdres = kd_nearestf(kdtree, key);
        if(kd_res_end(kdres))
          toret = 1;
        else
        {
          vertex* vc = (vertex*) kd_res_item_data(kdres);
          near_vertices.push_back(vc);
        }
      }
      else
      {
        while(! kd_res_end(kdres))
        {
          vertex* vc = (vertex*)kd_res_item_data(kdres);
          near_vertices.push_back(vc);
          kd_res_next(kdres);
        }
      }

      delete[] key;
      kd_res_free(kdres);
      return toret;
    }

    int update_best_vertex(vertex& v)
    {
      if(system.is_in_goal(v.state))
      {
        // implement goal cost here
        if( (!lower_bound_vertex) || (v.cost_from_root < lower_bound_cost))
        {
          lower_bound_cost = v.cost_from_root;
          lower_bound_vertex = &v;
        }
      }
      return 0;
    }

    vertex* insert_edge(vertex& vs, edge& e)
    {
      // branch and bound
      if(do_branch_and_bound)
      {
        cost_t new_cost = vs.cost_from_root + e.cost; 
        if(new_cost > lower_bound_cost)
          return NULL;
      }

      // create new vertex
      vertex* new_vertex = new vertex(*(e.end_state));
      insert_into_kdtree(*new_vertex);

      insert_edge(vs, e, *new_vertex);
      return new_vertex;
    }

    int insert_edge(vertex& vs, edge& e, vertex& ve)
    {
      ve.cost_from_parent = e.cost;
      ve.cost_from_root = vs.cost_from_root + ve.cost_from_parent;
      update_best_vertex(ve);

      if(ve.edge_from_parent)
        delete ve.edge_from_parent;
      ve.edge_from_parent = &e;

      if(ve.parent)
        ve.parent->children.erase(&ve);
      ve.parent = &vs;
      vs.children.insert(&ve);
      return 0;
    }

    static bool compare_vertex_cost_pairs(const pair<vertex*, cost_t>& p1,
        const pair<vertex*, cost_t>& p2)
    {
      return (p1.second < p2.second);
    }

    int find_best_parent(const state& si, const vector<vertex*>& near_vertices,
        vertex*& best_parent, edge*& best_edge)
    {
      // 1. create vertex_cost_pairs
      vector<pair<vertex*, cost_t> > vertex_cost_pairs;
      unordered_map<vertex*, tuple<cost_t, cost_t, opt_data_t> > vertex_map;
      for(auto& pv : near_vertices)
      {
        vertex& v = *pv;
        opt_data_t opt_data;
        cost_t edge_cost;

        if(system.evaluate_extend_cost(v.state, si, opt_data, edge_cost))
          continue;
        cost_t v_cost = v.cost_from_root + edge_cost;

        vertex_cost_pairs.push_back(make_pair(pv, v_cost));
        vertex_map.insert(make_pair(pv, make_tuple(edge_cost, v_cost, opt_data)));
      }

      // 2. sort using compare function of cost_t
      sort(vertex_cost_pairs.begin(), vertex_cost_pairs.end(), compare_vertex_cost_pairs);

      // 3. check obstacles in order of increasing cost
      bool check_obstacles = true;
      trajectory_t traj;
      for(auto& p : vertex_cost_pairs)
      {
        vertex& v = *(p.first);
        opt_data_t& opt_data = get<2>(vertex_map[p.first]);
        cost_t& edge_cost = get<0>(vertex_map[p.first]);
        if(!system.extend_to(v.state, si, check_obstacles,traj, opt_data))
        {
          best_parent = &v;
          best_edge = new edge(&(v.state), &si, edge_cost, opt_data);
          //cout<<"best_edge.cost: "<< best_edge.cost.val << endl;
          return 0;
        }
      }
      return 1;
    }

    int update_all_costs()
    {
      lower_bound_cost = system.get_inf_cost();
      lower_bound_vertex = NULL;
      update_branch_cost(*root,0); 
      return 0;
    }

    int update_branch_cost(const vertex& v, int depth)
    {
      for(auto& pc : v.children)
      {
        vertex& child = *pc;
        child.cost_from_root = v.cost_from_root + child.cost_from_parent;
        update_best_vertex(child); 
        update_branch_cost(child, depth+1);
      }
      return 0;
    }

    int rewire_vertices(vertex& v, const vector<vertex*>& near_vertices)
    {
      bool check_obstacles = true;
      for(auto& pvn : near_vertices)
      {
        vertex& vn = *pvn;
        opt_data_t opt_data;
        trajectory_t traj;
        cost_t cost_edge;
        if(system.evaluate_extend_cost(v.state, vn.state, opt_data, cost_edge))
          continue;

        cost_t cvn = v.cost_from_root + cost_edge;
        if(cvn < vn.cost_from_root)
        {
          if(system.extend_to(v.state, vn.state, check_obstacles, traj, opt_data))
            continue;
          edge* en = new edge(&(v.state), &(vn.state), cost_edge, opt_data);
          insert_edge(v, *en, vn);

          update_branch_cost(vn,0);
        }
      }
      return 0;
    }

    int recompute_cost(vertex& v)
    {
      update_branch_cost(v,0);  
      return 0;
    }

    bool is_safe_trajectory(const trajectory_t& traj)
    {
      return system.is_safe_trajectory(traj);
    }

    int mark_descendent_vertices(vertex& v)
    {
      v.mark = 1;
      for(auto& pc : v.children)
        mark_descendent_vertices(*pc);
      return 0;
    }
    
    int delete_unmarked_vertices(vector<vertex*>& surviving_vertices)
    {
      surviving_vertices.clear();
      for(auto& pv : list_vertices)
      {
        if(pv->mark == 1)
        {
          pv->mark = 0;
          surviving_vertices.push_back(pv);
        }
        else
          delete pv;
      }
      return 0;
    }
    
    int mark_vertex_and_remove_from_parent(vertex& v)
    {
      v.mark = 1;
      v.parent->children.erase(&v);
      for(auto& pc : v.children)
        mark_vertex_and_remove_from_parent(*pc);
      return 0;
    }

    int check_and_mark_children(vertex& v)
    {
      trajectory_t traj;
      bool check_obstacles = true;

      if(system.extend_to(v.parent->state, v.state, check_obstacles,
            traj, v.edge_from_parent->opt_data))
      { 
        mark_vertex_and_remove_from_parent(v);
      }
      else
      {
        for(auto& pc : v.children)
          check_and_mark_children(*pc);
      }
      return 0;
    }

    int check_tree()
    {
      if(system.is_in_collision(root->state))
      {
        cout<<"root in collision"<<endl;
        return 1;
      }
      else if(!root->children.empty())
      {
        root->mark = 0;

        for(auto& prc : root->children)
          check_and_mark_children(*prc);

        list<vertex*> surviving_vertices;
        for(auto& pv : list_vertices)
        {
          if(!pv->mark)
            surviving_vertices.push_back(pv);
          else
            delete pv;
        }

        if(kdtree)
          kd_free(kdtree);
        kdtree = kd_create(num_dim);

        list_vertices.clear();
        num_vertices = 0;
        for(auto& pv : surviving_vertices)
          insert_into_kdtree(*pv); 

        update_all_costs();
      }
      return 0;
    }

    int lazy_check_tree(const trajectory_t& committed_trajectory)
    {
      int ret = 0;
      if(!system.is_safe_trajectory(committed_trajectory))
        ret = check_tree();
      return ret;
    }

    int get_best_trajectory_vertices(list<vertex*>& best_trajectory_vertices)
    {
      if(!lower_bound_vertex)
        return 1;
      best_trajectory_vertices.clear();
      vertex* pvc = lower_bound_vertex;
      while(pvc)
      {
        best_trajectory_vertices.push_front(pvc);
        pvc = pvc->parent;
      }
      return 0;
    }

    int print_marks()
    {
      cout<<"marks: ";
      for(auto& pv : list_vertices)
        cout<<pv->mark<<" ";
      cout<<endl;
      return 0;
    }
    
    int switch_root(const float& distance, trajectory_t& committed_trajectory)
    {
      if(!lower_bound_vertex)
        return 1;

      // 0. if root is inside goal
      if(system.is_in_goal(root->state))
        return 0;

      // 1. find new root
      list<vertex*> best_trajectory_vertices;
      get_best_trajectory_vertices(best_trajectory_vertices);

      bool check_obstacles = false;
      float length = 0;

      state new_root_state;
      vertex* child_of_new_root_vertex = NULL;
      bool new_root_found = false;
      for(auto& pv : best_trajectory_vertices)
      {
        vertex& vc = *pv;
        if(new_root_found)
          break;
        if(vc.parent)
        {
          vertex& parent = *(vc.parent);
          trajectory_t traj;
          // traj connects parent with vc!
          if(!system.extend_to(parent.state, vc.state, check_obstacles,
                traj, vc.edge_from_parent->opt_data))
          {
            // 1.a go ahead until reach the edge with the root
            if( (length + traj.total_variation) < distance)
            {
              length += traj.total_variation;
              committed_trajectory.append(traj);
            }
            // 1.b ----length----distance(new_root)----length+total_variation(new_child)
            else
            {
              state sc = state(traj.states.front()), sp = sc; 
              auto cc = traj.controls.begin();
              bool only_xy = true;
              float t1 = 0;
              for(auto& ps : traj.states)
              {
                sc = state(ps);
                t1 = sp.dist(sc, only_xy);
                sp = sc;
                if((t1+length)<distance)
                {
                  length = length + t1;
                  committed_trajectory.states.push_back(sc);
                  committed_trajectory.controls.push_back(*cc);
                  committed_trajectory.total_variation += t1;
                }
                else
                {
                  new_root_state = sc;
                  child_of_new_root_vertex = pv;
                  new_root_found = true;
                  break;
                }
                cc++;
              }
            }
          }
          else
            return 2;
        }
      }
      // i.e., lower_bound_vertex = new root
      // no new child
      if(!new_root_found)
      {
        new_root_state = lower_bound_vertex->state;
        new_root_found = true;
        child_of_new_root_vertex = NULL;
      }

      if(new_root_found)
      {
        // new_root is inside goal
        if(!child_of_new_root_vertex)
        {
          clear_list_vertices();
          if(kdtree)
            kd_free(kdtree);
          kdtree = kd_create(num_dim);

          set_root(new_root_state);
          update_all_costs();
          return 0;
        }
        else
        {
          mark_descendent_vertices(*child_of_new_root_vertex);

          vector<vertex*> surviving_vertices;
          delete_unmarked_vertices(surviving_vertices);

          list_vertices.clear();
          num_vertices = 0;
          if(kdtree)
            kd_free(kdtree);
          kdtree = kd_create(num_dim);

          set_root(new_root_state);

          if(child_of_new_root_vertex->edge_from_parent)
            delete child_of_new_root_vertex->edge_from_parent;

          trajectory_t new_root_traj;
          opt_data_t opt_data;
          if(system.extend_to(new_root_state, child_of_new_root_vertex->state,
                check_obstacles, new_root_traj, opt_data))
            return 5;

          cost_t child_of_new_root_edge_cost;
          if(system.evaluate_extend_cost(new_root_state, child_of_new_root_vertex->state,
                opt_data, child_of_new_root_edge_cost))
            return 6;
          child_of_new_root_vertex->edge_from_parent = new edge(&new_root_state,
              &child_of_new_root_vertex->state,
              child_of_new_root_edge_cost, opt_data);
          child_of_new_root_vertex->parent = root;
          child_of_new_root_vertex->cost_from_parent = 
            child_of_new_root_vertex->edge_from_parent->cost;
          root->children.insert(child_of_new_root_vertex);

          // root was already inserted in set_root
          for(auto& pv : surviving_vertices)
            insert_into_kdtree(*pv);

          update_all_costs();
          return 0;
        }
      }
      else
        return 3;
    }
};
#endif
