/* -*- mode: C -*-  */
/* 
   IGraph library.
   Copyright (C) 2010  Gabor Csardi <csardi.gabor@gmail.com>
   Rue de l'Industrie 5, Lausanne 1005, Switzerland
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 
   02110-1301 USA

*/

#include "igraph_flow.h"
#include "igraph_flow_internal.h"
#include "igraph_error.h"
#include "igraph_memory.h"
#include "igraph_constants.h"
#include "igraph_interface.h"
#include "igraph_adjlist.h"
#include "igraph_conversion.h"
#include "igraph_constructors.h"
#include "igraph_structural.h"
#include "igraph_components.h"
#include "igraph_types_internal.h"
#include "config.h"
#include "igraph_math.h"
#include "igraph_dqueue.h"
#include "igraph_visitor.h"
#include "igraph_marked_queue.h"
#include "igraph_stack.h"
#include "igraph_estack.h"

/**
 * \function igraph_even_tarjan_reduction
 * Even-Tarjan reduction of a graph
 */

int igraph_even_tarjan_reduction(const igraph_t *graph, igraph_t *graphbar,
				 igraph_vector_t *capacity) {

  long int no_of_nodes=igraph_vcount(graph);
  long int no_of_edges=igraph_ecount(graph);
  
  long int new_no_of_nodes=no_of_nodes*2;
  long int new_no_of_edges=no_of_nodes + no_of_edges * 2;
  
  igraph_vector_t edges;
  long int edgeptr=0, capptr=0;
  long int i;

  IGRAPH_VECTOR_INIT_FINALLY(&edges, new_no_of_edges * 2);

  if (capacity) { 
    IGRAPH_CHECK(igraph_vector_resize(capacity, new_no_of_edges));
  }

  /* Every vertex 'i' is replaced by two vertices, i' and i'' */
  /* id[i'] := id[i] ; id[i''] := id[i] + no_of_nodes */

  /* One edge for each original vertex, for i, we add (i',i'') */
  for (i=0; i<no_of_nodes; i++) {
    VECTOR(edges)[edgeptr++] = i;
    VECTOR(edges)[edgeptr++] = i + no_of_nodes;
    if (capacity) { 
      VECTOR(*capacity)[capptr++] = 1.0;
    }
  }
  
  /* Two news edges for each original edge 
     (from,to) becomes (from'',to'), (to'',from') */
  for (i=0; i<no_of_edges; i++) {
    long int from=IGRAPH_FROM(graph, i);
    long int to=IGRAPH_TO(graph, i);
    VECTOR(edges)[edgeptr++] = from + no_of_nodes;
    VECTOR(edges)[edgeptr++] = to;
    VECTOR(edges)[edgeptr++] = to + no_of_nodes;
    VECTOR(edges)[edgeptr++] = from;
    if (capacity) { 
      VECTOR(*capacity)[capptr++] = no_of_nodes; /* TODO: should be Inf */
      VECTOR(*capacity)[capptr++] = no_of_nodes; /* TODO: should be Inf */
    }
  }
  
  IGRAPH_CHECK(igraph_create(graphbar, &edges, new_no_of_nodes, 
			     IGRAPH_DIRECTED));

  igraph_vector_destroy(&edges);
  IGRAPH_FINALLY_CLEAN(1);
  
  return 0;
}

int igraph_i_residual_graph(const igraph_t *graph,
			    const igraph_vector_t *capacity,
			    igraph_t *residual,
			    igraph_vector_t *residual_capacity,
			    const igraph_vector_t *flow, 
			    igraph_vector_t *tmp) {
  
  long int no_of_nodes=igraph_vcount(graph);
  long int no_of_edges=igraph_ecount(graph);
  long int i, no_new_edges=0;
  long int edgeptr=0, capptr=0;
  
  for (i=0; i<no_of_edges; i++) {
    if (VECTOR(*flow)[i] < VECTOR(*capacity)[i]) {
      no_new_edges++;
    }
  }
  
  IGRAPH_CHECK(igraph_vector_resize(tmp, no_new_edges*2));
  if (residual_capacity) {
    IGRAPH_CHECK(igraph_vector_resize(residual_capacity, no_new_edges));
  }
  
  for (i=0; i<no_of_edges; i++) {
    if (VECTOR(*capacity)[i] - VECTOR(*flow)[i] > 0) {
      long int from=IGRAPH_FROM(graph, i);
      long int to=IGRAPH_TO(graph, i);
      igraph_real_t c=VECTOR(*capacity)[i];
      VECTOR(*tmp)[edgeptr++] = from;
      VECTOR(*tmp)[edgeptr++] = to;
      if (residual_capacity) {
	VECTOR(*residual_capacity)[capptr++] = c;
      }
    }
  }

  IGRAPH_CHECK(igraph_create(residual, tmp, no_of_nodes, IGRAPH_DIRECTED));  

  return 0;
}

int igraph_residual_graph(const igraph_t *graph,
			  const igraph_vector_t *capacity,
			  igraph_t *residual,
			  igraph_vector_t *residual_capacity,
			  const igraph_vector_t *flow) {
  
  igraph_vector_t tmp;
  long int no_of_edges=igraph_ecount(graph);
  
  if (igraph_vector_size(capacity) != no_of_edges) {
    IGRAPH_ERROR("Invalid `capacity' vector size", IGRAPH_EINVAL);
  }
  if (igraph_vector_size(flow) != no_of_edges) {
    IGRAPH_ERROR("Invalid `flow' vector size", IGRAPH_EINVAL);
  }

  IGRAPH_VECTOR_INIT_FINALLY(&tmp, 0);

  IGRAPH_CHECK(igraph_i_residual_graph(graph, capacity, residual, 
				       residual_capacity, flow, &tmp));
  
  igraph_vector_destroy(&tmp);
  IGRAPH_FINALLY_CLEAN(1);
  
  return 0;
}

int igraph_i_reverse_residual_graph(const igraph_t *graph,
				    const igraph_vector_t *capacity,
				    igraph_t *residual,
				    const igraph_vector_t *flow,
				    igraph_vector_t *tmp) {

  long int no_of_nodes=igraph_vcount(graph);
  long int no_of_edges=igraph_ecount(graph);
  long int i, no_new_edges=0;
  long int edgeptr=0;

  for (i=0; i<no_of_edges; i++) {
    igraph_real_t cap=capacity ? VECTOR(*capacity)[i] : 1.0;
    if (VECTOR(*flow)[i] > 0) {
      no_new_edges++;
    }
    if (VECTOR(*flow)[i] < cap) {
      no_new_edges++;
    }
  }
  
  IGRAPH_CHECK(igraph_vector_resize(tmp, no_new_edges*2));

  for (i=0; i<no_of_edges; i++) {
    long int from=IGRAPH_FROM(graph, i);
    long int to=IGRAPH_TO(graph, i);
    igraph_real_t cap=capacity ? VECTOR(*capacity)[i] : 1.0;
    if (VECTOR(*flow)[i] > 0) {
      VECTOR(*tmp)[edgeptr++] = from;
      VECTOR(*tmp)[edgeptr++] = to;
    }
    if (VECTOR(*flow)[i] < cap) {
      VECTOR(*tmp)[edgeptr++] = to;
      VECTOR(*tmp)[edgeptr++] = from;
    }
  }
  
  IGRAPH_CHECK(igraph_create(residual, tmp, no_of_nodes, IGRAPH_DIRECTED));
  
  return 0;
}
  
int igraph_reverse_residual_graph(const igraph_t *graph,
				  const igraph_vector_t *capacity,
				  igraph_t *residual,
				  const igraph_vector_t *flow) {
  igraph_vector_t tmp;
  long int no_of_edges=igraph_ecount(graph);
  
  if (capacity && igraph_vector_size(capacity) != no_of_edges) {
    IGRAPH_ERROR("Invalid `capacity' vector size", IGRAPH_EINVAL);
  }
  if (igraph_vector_size(flow) != no_of_edges) {
    IGRAPH_ERROR("Invalid `flow' vector size", IGRAPH_EINVAL);
  }
  IGRAPH_VECTOR_INIT_FINALLY(&tmp, 0);
  
  IGRAPH_CHECK(igraph_i_reverse_residual_graph(graph, capacity, residual,
					       flow, &tmp));
  
  igraph_vector_destroy(&tmp);
  IGRAPH_FINALLY_CLEAN(1);
  
  return 0;
}

typedef struct igraph_i_dbucket_t {
  igraph_vector_long_t head;
  igraph_vector_long_t next;
} igraph_i_dbucket_t;

int igraph_i_dbucket_init(igraph_i_dbucket_t *buck, long int size) {
  IGRAPH_CHECK(igraph_vector_long_init(&buck->head, size));
  IGRAPH_FINALLY(igraph_vector_long_destroy, &buck->head);
  IGRAPH_CHECK(igraph_vector_long_init(&buck->next, size));
  IGRAPH_FINALLY_CLEAN(1);
  return 0;
}

void igraph_i_dbucket_destroy(igraph_i_dbucket_t *buck) {
  igraph_vector_long_destroy(&buck->head);
  igraph_vector_long_destroy(&buck->next);
}

int igraph_i_dbucket_insert(igraph_i_dbucket_t *buck, long int bid, 
			    long int elem) {
  /* Note: we can do this, since elem is not in any buckets */
  VECTOR(buck->next)[elem]=VECTOR(buck->head)[bid];
  VECTOR(buck->head)[bid]=elem+1;
  return 0;
}

long int igraph_i_dbucket_empty(const igraph_i_dbucket_t *buck, 
				long int bid) {
  return VECTOR(buck->head)[bid] == 0;
}

long int igraph_i_dbucket_delete(igraph_i_dbucket_t *buck, long int bid) {
  long int elem=VECTOR(buck->head)[bid]-1;
  VECTOR(buck->head)[bid]=VECTOR(buck->next)[elem];
  return elem;
}

int igraph_i_dominator_LINK(long int v, long int w,
			    igraph_vector_long_t *ancestor) {
  VECTOR(*ancestor)[w] = v+1;
  return 0;
}

/* TODO: don't always reallocate path */

int igraph_i_dominator_COMPRESS(long int v,
				igraph_vector_long_t *ancestor,
				igraph_vector_long_t *label,
				igraph_vector_long_t *semi) {
  igraph_stack_long_t path;
  long int w=v;
  long int top, pretop;
    
  IGRAPH_CHECK(igraph_stack_long_init(&path, 10));
  IGRAPH_FINALLY(igraph_stack_long_destroy, &path);
  
  while (VECTOR(*ancestor)[w] != 0) {
    IGRAPH_CHECK(igraph_stack_long_push(&path, w));
    w=VECTOR(*ancestor)[w]-1;
  }
  
  top=igraph_stack_long_pop(&path);
  while (!igraph_stack_long_empty(&path)) {
    pretop=igraph_stack_long_pop(&path);

    if (VECTOR(*semi)[VECTOR(*label)[top]] < 
	VECTOR(*semi)[VECTOR(*label)[pretop]]) {
      VECTOR(*label)[pretop] = VECTOR(*label)[top];
    }
    VECTOR(*ancestor)[pretop]=VECTOR(*ancestor)[top];

    top=pretop;
  }

  igraph_stack_long_destroy(&path);
  IGRAPH_FINALLY_CLEAN(1);

  return 0;
}

long int igraph_i_dominator_EVAL(long int v,
				 igraph_vector_long_t *ancestor,
				 igraph_vector_long_t *label,
				 igraph_vector_long_t *semi) {
  if (VECTOR(*ancestor)[v] == 0) { 
    return v;
  } else {
    igraph_i_dominator_COMPRESS(v, ancestor, label, semi);
    return VECTOR(*label)[v];
  }
}

/* TODO: implement the faster version. */

int igraph_dominator_tree(const igraph_t *graph,
			  igraph_integer_t root,
			  igraph_vector_t *dom,
			  igraph_t *domtree,
			  igraph_vector_t *leftout,
			  igraph_neimode_t mode) {

  long int no_of_nodes=igraph_vcount(graph);

  igraph_adjlist_t succ, pred;
  igraph_vector_t parent;
  igraph_vector_long_t semi;	/* +1 always */
  igraph_vector_t vertex;	/* +1 always */
  igraph_i_dbucket_t bucket;
  igraph_vector_long_t ancestor;
  igraph_vector_long_t label;

  igraph_neimode_t invmode= mode==IGRAPH_IN ? IGRAPH_OUT: IGRAPH_IN;

  long int i;

  igraph_vector_t vdom, *mydom=dom;

  long int component_size=0;

  if (root < 0 || root >= no_of_nodes) {
    IGRAPH_ERROR("Invalid root vertex id for dominator tree", 
		 IGRAPH_EINVAL);
  }

  if (!igraph_is_directed(graph)) {
    IGRAPH_ERROR("Dominator tree of an undirected graph requested",
		 IGRAPH_EINVAL);
  }
  
  if (mode == IGRAPH_ALL) {
    IGRAPH_ERROR("Invalid neighbor mode for dominator tree",
		 IGRAPH_EINVAL);
  }

  if (dom) {
    IGRAPH_CHECK(igraph_vector_resize(dom, no_of_nodes));
  } else {
    mydom=&vdom;
    IGRAPH_VECTOR_INIT_FINALLY(mydom, no_of_nodes);
  }
  igraph_vector_fill(mydom, IGRAPH_NAN);

  IGRAPH_CHECK(igraph_vector_init(&parent, no_of_nodes));
  IGRAPH_FINALLY(igraph_vector_destroy, &parent);
  IGRAPH_CHECK(igraph_vector_long_init(&semi, no_of_nodes));
  IGRAPH_FINALLY(igraph_vector_long_destroy, &semi);
  IGRAPH_CHECK(igraph_vector_init(&vertex, no_of_nodes));
  IGRAPH_FINALLY(igraph_vector_destroy, &vertex);
  IGRAPH_CHECK(igraph_vector_long_init(&ancestor, no_of_nodes));
  IGRAPH_FINALLY(igraph_vector_long_destroy, &ancestor);
  IGRAPH_CHECK(igraph_vector_long_init_seq(&label, 0, no_of_nodes-1));
  IGRAPH_FINALLY(igraph_vector_long_destroy, &label);
  IGRAPH_CHECK(igraph_adjlist_init(graph, &succ, mode));
  IGRAPH_FINALLY(igraph_adjlist_destroy, &succ);
  IGRAPH_CHECK(igraph_adjlist_init(graph, &pred, invmode));
  IGRAPH_FINALLY(igraph_adjlist_destroy, &pred);
  IGRAPH_CHECK(igraph_i_dbucket_init(&bucket, no_of_nodes));
  IGRAPH_FINALLY(igraph_i_dbucket_destroy, &bucket);

  /* DFS first, to set semi, vertex and parent, step 1 */
  
  IGRAPH_CHECK(igraph_dfs(graph, root, mode, /*unreachable=*/ 0,
			  /*order=*/ &vertex,
			  /*order_out=*/ 0, /*father=*/ &parent,
			  /*dist=*/ 0, /*in_callback=*/ 0, 
			  /*out_callback=*/ 0, /*extra=*/ 0));

  for (i=0; i<no_of_nodes; i++) {
    if (IGRAPH_FINITE(VECTOR(vertex)[i])) {
      long int t=VECTOR(vertex)[i];
      VECTOR(semi)[t] = component_size+1;
      VECTOR(vertex)[component_size] = t+1;
      component_size++;
    }
  }
  if (leftout) {
    long int n=no_of_nodes-component_size;
    long int p=0, j;
    IGRAPH_CHECK(igraph_vector_resize(leftout, n));
    for (j=0; j<no_of_nodes && p<n; j++) {
      if (!IGRAPH_FINITE(VECTOR(parent)[j])) {
	VECTOR(*leftout)[p++] = j;
      }
    }
  }

  /* We need to go over 'pred' because it should contain only the
     edges towards the target vertex. */
  for (i=0; i<no_of_nodes; i++) {
    igraph_vector_t *v=igraph_adjlist_get(&pred, i);
    long int j, n=igraph_vector_size(v);
    for (j=0; j<n; ) {
      long int v2=VECTOR(*v)[j];
      if (IGRAPH_FINITE(VECTOR(parent)[v2])) {
	j++; 
      } else {
	VECTOR(*v)[j]=VECTOR(*v)[n-1];
	igraph_vector_pop_back(v);
	n--;
      }
    }
  }

  /* Now comes the main algorithm, steps 2 & 3 */

  for (i=component_size-1; i>0; i--) {
    long int w=VECTOR(vertex)[i]-1;
    igraph_vector_t *predw=igraph_adjlist_get(&pred, w);
    long int j, n=igraph_vector_size(predw);
    for (j=0; j<n; j++) {
      long int v=VECTOR(*predw)[j];
      long int u=igraph_i_dominator_EVAL(v, &ancestor, &label, &semi);
      if (VECTOR(semi)[u] < VECTOR(semi)[w]) {
	VECTOR(semi)[w]=VECTOR(semi)[u];
      }
    }
    igraph_i_dbucket_insert(&bucket, 
			    VECTOR(vertex)[ VECTOR(semi)[w]-1 ]-1, w);
    igraph_i_dominator_LINK(VECTOR(parent)[w], w, &ancestor);
    while (!igraph_i_dbucket_empty(&bucket, VECTOR(parent)[w])) {
      long int v=igraph_i_dbucket_delete(&bucket, VECTOR(parent)[w]);
      long int u=igraph_i_dominator_EVAL(v, &ancestor, &label, &semi);
      VECTOR(*mydom)[v] = VECTOR(semi)[u] < VECTOR(semi)[v] ? u : 
	VECTOR(parent)[w];
    }
  }

  /* Finally, step 4 */

  for (i=1; i<component_size; i++) {
    long int w=VECTOR(vertex)[i]-1;
    if (VECTOR(*mydom)[w] != VECTOR(vertex)[VECTOR(semi)[w]-1]-1) {
      VECTOR(*mydom)[w] = VECTOR(*mydom)[(long int)VECTOR(*mydom)[w]];
    }
  }
  VECTOR(*mydom)[(long int)root]=-1;

  igraph_i_dbucket_destroy(&bucket);
  igraph_adjlist_destroy(&pred);
  igraph_adjlist_destroy(&succ);
  igraph_vector_long_destroy(&label);
  igraph_vector_long_destroy(&ancestor);
  igraph_vector_destroy(&vertex);
  igraph_vector_long_destroy(&semi);
  igraph_vector_destroy(&parent);
  IGRAPH_FINALLY_CLEAN(8);

  if (domtree) {
    igraph_vector_t edges;
    long int ptr=0;
    IGRAPH_VECTOR_INIT_FINALLY(&edges, component_size*2-2);
    for (i=0; i<no_of_nodes; i++) {
      if (i!=root && IGRAPH_FINITE(VECTOR(*mydom)[i])) {
	if (mode==IGRAPH_OUT) {
	  VECTOR(edges)[ptr++] = VECTOR(*mydom)[i];
	  VECTOR(edges)[ptr++] = i;
	} else {
	  VECTOR(edges)[ptr++] = i;
	  VECTOR(edges)[ptr++] = VECTOR(*mydom)[i];
	}
      }
    }
    IGRAPH_CHECK(igraph_create(domtree, &edges, no_of_nodes,
			       IGRAPH_DIRECTED));
    igraph_vector_destroy(&edges);
    IGRAPH_FINALLY_CLEAN(1);
  }

  if (!dom) {
    igraph_vector_destroy(&vdom);
    IGRAPH_FINALLY_CLEAN(1);
  }

  return 0;
}

typedef struct igraph_i_all_st_cuts_minimal_dfs_data_t {
  igraph_stack_t *stack;
  igraph_vector_bool_t *nomark;  
  const igraph_vector_bool_t *GammaX;
  long int root;
  const igraph_vector_t *map;
} igraph_i_all_st_cuts_minimal_dfs_data_t;

igraph_bool_t igraph_i_all_st_cuts_minimal_dfs_incb(const igraph_t *graph,
						    igraph_integer_t vid,
						    igraph_integer_t dist,
						    void *extra) {

  igraph_i_all_st_cuts_minimal_dfs_data_t *data=extra;
  igraph_stack_t *stack=data->stack;
  igraph_vector_bool_t *nomark=data->nomark;
  const igraph_vector_bool_t *GammaX=data->GammaX;
  const igraph_vector_t *map=data->map;
  long int realvid=VECTOR(*map)[(long int)vid];

  if (VECTOR(*GammaX)[(long int)realvid]) {
    if (!igraph_stack_empty(stack)) {
      long int top=igraph_stack_top(stack);
      VECTOR(*nomark)[top]=1;	/* we just found a smaller one */
    }
    igraph_stack_push(stack, realvid); /* TODO: error check */
  }

  return 0;
}

igraph_bool_t igraph_i_all_st_cuts_minimal_dfs_otcb(const igraph_t *graph,
						    igraph_integer_t vid,
						    igraph_integer_t dist,
						    void *extra) {
  igraph_i_all_st_cuts_minimal_dfs_data_t *data=extra;
  igraph_stack_t *stack=data->stack;
  const igraph_vector_t *map=data->map;
  long int realvid=VECTOR(*map)[(long int)vid];

  if (!igraph_stack_empty(stack) && 
      igraph_stack_top(stack) == realvid) {
    igraph_stack_pop(stack);
  }

  return 0;
}

int igraph_i_all_st_cuts_minimal(const igraph_t *graph,
				 const igraph_t *domtree,
				 long int root,
				 const igraph_marked_queue_t *X,
				 const igraph_vector_bool_t *GammaX,
				 const igraph_vector_t *invmap,
				 igraph_vector_t *minimal) {

  long int no_of_nodes=igraph_vcount(graph);
  igraph_stack_t stack;
  igraph_vector_bool_t nomark;
  igraph_i_all_st_cuts_minimal_dfs_data_t data;
  long int i;
  
  IGRAPH_CHECK(igraph_stack_init(&stack, 10));
  IGRAPH_FINALLY(igraph_stack_destroy, &stack);
  IGRAPH_CHECK(igraph_vector_bool_init(&nomark, no_of_nodes));
  IGRAPH_FINALLY(igraph_vector_bool_destroy, &nomark);

  data.stack=&stack;
  data.nomark=&nomark;
  data.GammaX=GammaX;
  data.root=root;
  data.map=invmap;

  /* We mark all GammaX elements as minimal first.
     TODO: actually, we could just use GammaX to return the minimal
     elements. */
  for (i=0; i<no_of_nodes; i++) {
    VECTOR(nomark)[i] = VECTOR(*GammaX)[i] == 0 ? 1 : 0;
  }

  /* We do a reverse DFS from root. If, along a path we find a GammaX
     vertex after (=below) another GammaX vertex, we mark the higher
     one as non-minimal. */

  IGRAPH_CHECK(igraph_dfs(domtree, root, IGRAPH_IN, /*unreachable=*/ 0,
			  /*order=*/ 0,
			  /*order_out=*/ 0, /*father=*/ 0, 
			  /*dist=*/ 0, /*in_callback=*/ 
			  igraph_i_all_st_cuts_minimal_dfs_incb,
			  /*out_callback=*/ 
			  igraph_i_all_st_cuts_minimal_dfs_otcb,
			  /*extra=*/ &data));
  
  igraph_vector_clear(minimal);
  for (i=0; i<no_of_nodes; i++) {
    if (!VECTOR(nomark)[i]) { 
      IGRAPH_CHECK(igraph_vector_push_back(minimal, i));
    }
  }

  igraph_vector_bool_destroy(&nomark);
  igraph_stack_destroy(&stack);
  IGRAPH_FINALLY_CLEAN(2);

  return 0;
}

int igraph_i_all_st_cuts_pivot(const igraph_t *graph,
			       const igraph_marked_queue_t *S,
			       const igraph_estack_t *T,
			       long int source,
			       long int target,
			       long int *v,
			       igraph_vector_t *Isv,
			       void *arg) {

  long int no_of_nodes=igraph_vcount(graph);
  igraph_t Sbar;
  igraph_vector_t Sbar_map, Sbar_invmap;
  igraph_vector_t keep;
  igraph_t domtree;
  igraph_vector_t leftout;
  long int i, nomin;
  long int root;
  igraph_vector_t M;
  igraph_vector_bool_t GammaS;
  long int GammaS_len;
  igraph_vector_t Nuv;
  igraph_vector_t Isv_min;
  igraph_vector_t GammaS_vec;

  /* We need to create the graph induced by Sbar */
  IGRAPH_VECTOR_INIT_FINALLY(&Sbar_map, 0);
  IGRAPH_VECTOR_INIT_FINALLY(&Sbar_invmap, 0);

  IGRAPH_VECTOR_INIT_FINALLY(&keep, 0);
  for (i=0; i<no_of_nodes; i++) {
    if (!igraph_marked_queue_iselement(S, i)) {
      IGRAPH_CHECK(igraph_vector_push_back(&keep, i));
    }
  }

  IGRAPH_CHECK(igraph_induced_subgraph_map(graph, &Sbar,
					   igraph_vss_vector(&keep),
					   IGRAPH_SUBGRAPH_AUTO,
					   /* map= */ &Sbar_map, 
					   /* invmap= */ &Sbar_invmap));  
  igraph_vector_destroy(&keep);
  IGRAPH_FINALLY_CLEAN(1);
  IGRAPH_FINALLY(igraph_destroy, &Sbar);

  root=VECTOR(Sbar_map)[target]-1;

  /* -------------------------------------------------------------*/
  /* Construct the dominator tree of Sbar */

  IGRAPH_VECTOR_INIT_FINALLY(&leftout, 0);
  IGRAPH_CHECK(igraph_dominator_tree(&Sbar, root, /*dom=*/ 0, &domtree,
				     &leftout, IGRAPH_IN));
  IGRAPH_FINALLY(igraph_destroy, &domtree);

  /* -------------------------------------------------------------*/
  /* Identify the set M of minimal elements of Gamma(S) with respect
     to the dominator relation. */
  
  /* First we create GammaS */
  /* TODO: use the adjacency list, instead of neighbors() */
  IGRAPH_CHECK(igraph_vector_bool_init(&GammaS, no_of_nodes));
  IGRAPH_FINALLY(igraph_vector_bool_destroy, &GammaS);
  if (igraph_marked_queue_size(S)==0) {
    VECTOR(GammaS)[(long int) VECTOR(Sbar_map)[source]-1]=1;
  } else {
    for (i=0; i<no_of_nodes; i++) {
      if (igraph_marked_queue_iselement(S, i)) {
	igraph_vector_t neis;
	long int j, n;
	IGRAPH_VECTOR_INIT_FINALLY(&neis, 0);
	IGRAPH_CHECK(igraph_neighbors(graph, &neis, i, IGRAPH_OUT));
	n=igraph_vector_size(&neis);
	for (j=0; j<n; j++) {
	  long int nei=VECTOR(neis)[j];
	  if (!igraph_marked_queue_iselement(S, nei)) {
	    VECTOR(GammaS)[nei]=1;
	  }
	}
	igraph_vector_destroy(&neis);
	IGRAPH_FINALLY_CLEAN(1);
      }
    }
  }

  IGRAPH_VECTOR_INIT_FINALLY(&M, 0);
  if (igraph_ecount(&domtree)>0) {
    IGRAPH_CHECK(igraph_i_all_st_cuts_minimal(graph, &domtree, root, S,
					      &GammaS, &Sbar_invmap, &M));
  }

  igraph_vector_clear(Isv);
  IGRAPH_VECTOR_INIT_FINALLY(&Nuv, 0);
  IGRAPH_VECTOR_INIT_FINALLY(&Isv_min, 0);
  IGRAPH_VECTOR_INIT_FINALLY(&GammaS_vec, 0);
  for (i=0; i<no_of_nodes; i++) {
    if (VECTOR(GammaS)[i]) {
      IGRAPH_CHECK(igraph_vector_push_back(&GammaS_vec, i));
    }
  }
  GammaS_len=igraph_vector_size(&GammaS_vec);

  nomin=igraph_vector_size(&M);
  for (i=0; i<nomin; i++) {
    /* -------------------------------------------------------------*/
    /* For each v in M find the set Nu(v)=dom(Sbar, v)-K
       Nu(v) contains all vertices that are dominated by v, for every
       v, this is a subtree of the dominator tree, rooted at v. The
       different subtrees are disjoint. */
    long int min=VECTOR(Sbar_map)[(long int) VECTOR(M)[i] ]-1;
    long int nuvsize, isvlen, j;
    IGRAPH_CHECK(igraph_dfs(&domtree, min, IGRAPH_IN, /*unreachable=*/ 0, 
			    /*order=*/ &Nuv, 
			    /*order_out=*/ 0, /*father=*/ 0, /*dist=*/ 0,
			    /*in_callback=*/ 0, /*out_callback=*/ 0, 
			    /*extra=*/ 0));
    /* Remove the NAN values from the end of the vector */
    for (nuvsize=0; nuvsize<no_of_nodes; nuvsize++) {
      igraph_real_t t=VECTOR(Nuv)[nuvsize];
      if (IGRAPH_FINITE(t)) {
	VECTOR(Nuv)[nuvsize]=VECTOR(Sbar_invmap)[(long int) t];
      } else {
	break;
      }
    }
    igraph_vector_resize(&Nuv, nuvsize);
    
    /* -------------------------------------------------------------*/
    /* By a BFS search of <Nu(v)> determine I(S,v)-K.
       I(S,v) contains all vertices that are in Nu(v) and that are
       reachable from Gamma(S) via a path in Nu(v). */
    IGRAPH_CHECK(igraph_bfs(graph, /*root=*/ -1, /*roots=*/ &GammaS_vec,
			    /*mode=*/ IGRAPH_OUT, /*unreachable=*/ 0,
			    /*restricted=*/ &Nuv, 
			    /*order=*/ &Isv_min, /*rank=*/ 0,
			    /*father=*/ 0, /*pred=*/ 0, /*succ=*/ 0,
			    /*dist=*/ 0, /*callback=*/ 0, /*extra=*/ 0));
    for (isvlen=0; isvlen<no_of_nodes; isvlen++) {
      if (!IGRAPH_FINITE(VECTOR(Isv_min)[isvlen])) { break; }
    }
    igraph_vector_resize(&Isv_min, isvlen);
    
    /* -------------------------------------------------------------*/
    /* For each c in M check whether Isv-K is included in Tbar. If 
       such a v is found, compute Isv={x|v[Nu(v) U K]x} and return v and 
       Isv; otherwise return Isv={}. */
    for (j=0; j<isvlen; j++) {
      long int v=VECTOR(Isv_min)[j];
      if (igraph_estack_iselement(T, v) || v==target) { break; }
    }
    /* We might have found one */
    if (j==isvlen) {
      *v=VECTOR(M)[i];
      /* Calculate real Isv */
      IGRAPH_CHECK(igraph_vector_append(&Nuv, &leftout));
      IGRAPH_CHECK(igraph_bfs(graph, /*root=*/ *v, /*roots=*/ 0,
			      /*mode=*/ IGRAPH_OUT, /*unreachable=*/ 0,
			      /*restricted=*/ &Nuv, 
			      /*order=*/ &Isv_min, /*rank=*/ 0,
			      /*father=*/ 0, /*pred=*/ 0, /*succ=*/ 0,
			      /*dist=*/ 0, /*callback=*/ 0, /*extra=*/ 0));
      for (isvlen=0; isvlen<no_of_nodes; isvlen++) {
	if (!IGRAPH_FINITE(VECTOR(Isv_min)[isvlen])) { break; }
      }
      igraph_vector_resize(&Isv_min, isvlen);
      igraph_vector_update(Isv, &Isv_min);

      break;
    }
  }

  igraph_vector_destroy(&GammaS_vec);
  igraph_vector_destroy(&Isv_min);
  igraph_vector_destroy(&Nuv);
  IGRAPH_FINALLY_CLEAN(3);

  igraph_vector_destroy(&M);
  igraph_vector_bool_destroy(&GammaS);
  igraph_destroy(&domtree);
  igraph_vector_destroy(&leftout);  
  igraph_destroy(&Sbar);
  igraph_vector_destroy(&Sbar_map);
  igraph_vector_destroy(&Sbar_invmap);
  IGRAPH_FINALLY_CLEAN(7);

  return 0;
}

/* TODO: This is a temporary resursive version, without proper error
   handling */

int igraph_provan_shier_list(const igraph_t *graph,
			     igraph_marked_queue_t *S,
			     igraph_estack_t *T,
			     long int source,
			     long int target,
			     igraph_vector_ptr_t *result,
			     igraph_provan_shier_pivot_t *pivot,
			     void *pivot_arg) {

  long int no_of_nodes=igraph_vcount(graph);
  igraph_vector_t Isv;
  long int v=0;
  long int i, n;

  igraph_vector_init(&Isv, 0);
  
  pivot(graph, S, T, source, target, &v, &Isv, pivot_arg);
  if (igraph_vector_size(&Isv)==0) {
    if (igraph_marked_queue_size(S) != 0 && 
	igraph_marked_queue_size(S) != no_of_nodes) {
      igraph_vector_t *vec=igraph_Calloc(1, igraph_vector_t);      
      igraph_vector_init(vec, igraph_marked_queue_size(S));
      igraph_marked_queue_as_vector(S, vec);
      IGRAPH_CHECK(igraph_vector_ptr_push_back(result, vec));
    }
  } else {
    /* Put v into T */
    igraph_estack_push(T, v);

    /* Go down left in the search tree */
    igraph_provan_shier_list(graph, S, T, source, target, 
			     result, pivot, pivot_arg);

    /* Take out v from T */
    igraph_estack_pop(T);
    
    /* Add Isv to S */
    igraph_marked_queue_start_batch(S);
    n=igraph_vector_size(&Isv);
    for (i=0; i<n; i++) {
      if (!igraph_marked_queue_iselement(S, VECTOR(Isv)[i])) {
	igraph_marked_queue_push(S, VECTOR(Isv)[i]);
      }
    }

    /* Go down right in the search tree */
    
    igraph_provan_shier_list(graph, S, T, source, target, 
			     result, pivot, pivot_arg);

    /* Take out Isv from S */
    igraph_marked_queue_pop_back_batch(S);
  }
    
  igraph_vector_destroy(&Isv);

  return 0;
}

int igraph_all_st_cuts(const igraph_t *graph,
		       igraph_vector_ptr_t *cuts,
		       igraph_vector_ptr_t *partition1s,
		       igraph_integer_t source,
		       igraph_integer_t target) {

  /* S is a special stack, in which elements are pushed in batches. 
     It is then possible to remove the whole batch in one step.

     T is a stack with an is-element operation.
     Every element is included at most once.
  */

  long int no_of_nodes=igraph_vcount(graph);
  long int no_of_edges=igraph_ecount(graph);
  igraph_marked_queue_t S;
  igraph_estack_t T;

  if (!igraph_is_directed(graph)) {
    IGRAPH_ERROR("Listing all s-t cuts only implemented for "
		 "directed graphs", IGRAPH_UNIMPLEMENTED);
  }
  
  if (!partition1s) { 
    IGRAPH_ERROR("`partition1s' must not be a null pointer", 
		 IGRAPH_UNIMPLEMENTED);
  }

  IGRAPH_CHECK(igraph_marked_queue_init(&S, no_of_nodes));
  IGRAPH_FINALLY(igraph_marked_queue_destroy, &S);
  IGRAPH_CHECK(igraph_estack_init(&T, no_of_nodes, 0));
  IGRAPH_FINALLY(igraph_estack_destroy, &T);

  if (cuts)        { igraph_vector_ptr_clear(cuts);        }
  if (partition1s) { igraph_vector_ptr_clear(partition1s); }    
  
  /* We call it with S={}, T={} */
  IGRAPH_CHECK(igraph_provan_shier_list(graph, &S, &T,
					source, target, partition1s,
					igraph_i_all_st_cuts_pivot, 
					/*pivot_arg=*/ 0));
  
  if (cuts) {
    igraph_vector_long_t inS;
    long int i, nocuts=igraph_vector_ptr_size(partition1s);
    IGRAPH_CHECK(igraph_vector_long_init(&inS, no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_long_destroy, &inS);
    IGRAPH_CHECK(igraph_vector_ptr_resize(cuts, nocuts));
    for (i=0; i<nocuts; i++) {
      igraph_vector_t *cut;
      igraph_vector_t *part=VECTOR(*partition1s)[i];
      long int cutsize=0;
      long int j, partlen=igraph_vector_size(part);
      /* Mark elements */
      for (j=0; j<partlen; j++) {
	long int v=VECTOR(*part)[j];
	VECTOR(inS)[v] = i+1;
      }
      /* Check how many edges */
      for (j=0; j<no_of_edges; j++) {
	long int from=IGRAPH_FROM(graph, j);
	long int to=IGRAPH_TO(graph, j);
	long int pfrom=VECTOR(inS)[from];
	long int pto=VECTOR(inS)[to];
	if (pfrom == i+1 && pto != i+1) { 
	  cutsize++;
	}
      }
      /* Add the edges */
      cut=igraph_Calloc(1, igraph_vector_t);
      if (!cut) {
	IGRAPH_ERROR("Cannot calculate s-t cuts", IGRAPH_ENOMEM);
      }
      IGRAPH_VECTOR_INIT_FINALLY(cut, cutsize);
      cutsize=0;
      for (j=0; j<no_of_edges; j++) {
	long int from=IGRAPH_FROM(graph, j);
	long int to=IGRAPH_TO(graph, j);
	long int pfrom=VECTOR(inS)[from];
	long int pto=VECTOR(inS)[to];
	if ((pfrom == i+1 && pto != i+1) ||
	    (pfrom != i+1 && pto == i+1)) {
	  VECTOR(*cut)[cutsize++]=j;
	}
      }
      VECTOR(*cuts)[i]=cut;
      IGRAPH_FINALLY_CLEAN(1);
    }
    
    igraph_vector_long_destroy(&inS);
    IGRAPH_FINALLY_CLEAN(1);
  }

  igraph_estack_destroy(&T);
  igraph_marked_queue_destroy(&S);
  IGRAPH_FINALLY_CLEAN(2);

  return 0;
}

typedef struct igraph_i_all_st_mincuts_minimal_dfs_data_t {
  igraph_stack_t *stack;
  igraph_vector_bool_t *nomark;
  long int root;
  const igraph_vector_bool_t *active;
  const igraph_vector_t *map;
  const igraph_marked_queue_t *X;
} igraph_i_all_st_mincuts_minimal_dfs_data_t;

igraph_bool_t igraph_i_all_st_mincuts_minimal_dfs_incb(const igraph_t *graph,
						       igraph_integer_t vid,
						       igraph_integer_t dist,
						       void *extra) {

  igraph_i_all_st_mincuts_minimal_dfs_data_t *data=extra;
  igraph_stack_t *stack=data->stack;
  igraph_vector_bool_t *nomark=data->nomark;
  const igraph_vector_bool_t *active=data->active;
  const igraph_vector_t *map=data->map;
  long int realvid=VECTOR(*map)[(long int)vid];
  const igraph_marked_queue_t *X=data->X;
  
  if (VECTOR(*active)[realvid] && 
      !igraph_marked_queue_iselement(X, realvid)) {
    if (!igraph_stack_empty(stack)) {
      long int top=igraph_stack_top(stack);
      VECTOR(*nomark)[top]=1; 	/* we just found a smaller one */
    }
    igraph_stack_push(stack, realvid); /* TODO: error check */
  }

  return 0;
}

igraph_bool_t igraph_i_all_st_mincuts_minimal_dfs_otcb(const igraph_t *graph,
						       igraph_integer_t vid,
						       igraph_integer_t dist,
						       void *extra) {

  igraph_i_all_st_mincuts_minimal_dfs_data_t *data=extra;
  igraph_stack_t *stack=data->stack;
  const igraph_vector_t *map=data->map;
  long int realvid=VECTOR(*map)[(long int)vid];
  
  if (!igraph_stack_empty(stack) && 
      igraph_stack_top(stack) == realvid) {
    igraph_stack_pop(stack);
  }

  return 0;
}

int igraph_i_all_st_mincuts_minimal(const igraph_t *graph, 
				    const igraph_t *domtree, 
				    long int root, 
				    const igraph_marked_queue_t *X,
				    const igraph_vector_bool_t *active,
				    const igraph_vector_t *invmap,
				    igraph_vector_t *minimal) {

  long int no_of_nodes=igraph_vcount(graph);
  igraph_stack_t stack;
  igraph_vector_bool_t nomark;
  igraph_i_all_st_mincuts_minimal_dfs_data_t data;
  long int i;
  
  IGRAPH_CHECK(igraph_stack_init(&stack, 10));
  IGRAPH_FINALLY(igraph_stack_destroy, &stack);
  IGRAPH_CHECK(igraph_vector_bool_init(&nomark, no_of_nodes));
  IGRAPH_FINALLY(igraph_vector_bool_destroy, &nomark);
  
  data.stack=&stack;
  data.nomark=&nomark;
  data.root=root;
  data.active=active;
  data.map=invmap;
  data.X=X;
  
  /* We mark all active elements as minimal first  */
  for (i=0; i<no_of_nodes; i++) {
    if (!VECTOR(*active)[i] || igraph_marked_queue_iselement(X, i)) {
      VECTOR(nomark)[i] = 1;
    }
  }

  /* We do a reverse DFS from root. If, along a path we find an active
     vertex after (=below) another active vertex, we mark the higher one
     as non-minimal */
  IGRAPH_CHECK(igraph_dfs(domtree, root, IGRAPH_IN, /*unreachable=*/ 0,
			  /*order=*/ 0,
			  /*order_out=*/ 0, /*father=*/ 0, 
			  /*dist=*/ 0, /*in_callback=*/ 
			  igraph_i_all_st_mincuts_minimal_dfs_incb,
			  /*out_callback=*/ 
			  igraph_i_all_st_mincuts_minimal_dfs_otcb,
			  /*extra=*/ &data));

  igraph_vector_clear(minimal);
  for (i=0; i<no_of_nodes; i++) {
    if (!VECTOR(nomark)[i]) { 
      IGRAPH_CHECK(igraph_vector_push_back(minimal, i));
    }
  }

  igraph_vector_bool_destroy(&nomark);
  igraph_stack_destroy(&stack);
  IGRAPH_FINALLY_CLEAN(2);

  return 0;
}

typedef struct igraph_i_all_st_mincuts_data_t {
  const igraph_vector_bool_t *active;
} igraph_i_all_st_mincuts_data_t;

int igraph_i_all_st_mincuts_pivot(const igraph_t *graph,
				  const igraph_marked_queue_t *S,
				  const igraph_estack_t *T,
				  long int source,
				  long int target,
				  long int *v,
				  igraph_vector_t *Isv,
				  void *arg) {

  igraph_i_all_st_mincuts_data_t *data=arg;
  const igraph_vector_bool_t *active=data->active;

  long int no_of_nodes=igraph_vcount(graph);
  long int i;
  igraph_vector_t Sbar_map, Sbar_invmap;
  igraph_vector_t keep;
  igraph_t Sbar;
  igraph_vector_t leftout;
  long int root;
  igraph_t domtree;
  igraph_vector_t M;
  long int nomin;

  if (igraph_marked_queue_size(S) == no_of_nodes) {
    igraph_vector_clear(Isv);
    return 0;
  }

  /* Create the graph induced by Sbar */
  IGRAPH_VECTOR_INIT_FINALLY(&Sbar_map, 0);
  IGRAPH_VECTOR_INIT_FINALLY(&Sbar_invmap, 0);

  IGRAPH_VECTOR_INIT_FINALLY(&keep, 0);
  for (i=0; i<no_of_nodes; i++) {
    if (!igraph_marked_queue_iselement(S, i)) {
      IGRAPH_CHECK(igraph_vector_push_back(&keep, i));
    }
  }
  
  IGRAPH_CHECK(igraph_induced_subgraph_map(graph, &Sbar,
					   igraph_vss_vector(&keep),
					   IGRAPH_SUBGRAPH_AUTO,
					   /* map= */ &Sbar_map, 
					   /* invmap= */ &Sbar_invmap));  
  igraph_vector_destroy(&keep);
  IGRAPH_FINALLY_CLEAN(1);
  IGRAPH_FINALLY(igraph_destroy, &Sbar);

  root=VECTOR(Sbar_map)[target]-1;

  /* ------------------------------------------------------------- */
  /* Construct the dominator tree of Sbar */ 
  IGRAPH_VECTOR_INIT_FINALLY(&leftout, 0);
  IGRAPH_CHECK(igraph_dominator_tree(&Sbar, root, /*dom=*/ 0, &domtree,
				     &leftout, IGRAPH_IN));
  IGRAPH_FINALLY(igraph_destroy, &domtree);
  
  /* ------------------------------------------------------------- */
  /* Identify the set M of minimal elements that are active */
  IGRAPH_VECTOR_INIT_FINALLY(&M, 0);
  if (igraph_ecount(&domtree)>0) {
    IGRAPH_CHECK(igraph_i_all_st_mincuts_minimal(graph, &domtree, root, S, 
						 active, &Sbar_invmap, &M));
  }

  /* ------------------------------------------------------------- */
  /* Now find a minimal element that is not in T */  
  igraph_vector_clear(Isv);
  nomin=igraph_vector_size(&M);
  for (i=0; i<nomin; i++) {
    long int min=VECTOR(M)[i];
    if (!igraph_estack_iselement(T, min)) { break; }
  }
  if (i!=nomin) {
    /* OK, we found a pivot element. I(S,v) contains all elements
       that can reach the pivot element */
    igraph_vector_t Isv_min;
    long int isvlen;
    IGRAPH_VECTOR_INIT_FINALLY(&Isv_min, 0);
    *v=VECTOR(M)[i];
    IGRAPH_CHECK(igraph_bfs(graph, /*root=*/ *v, /*roots=*/ 0,
			    /*mode=*/ IGRAPH_IN, /*unreachable=*/ 0,
			    /*restricted=*/ 0, /*order=*/ &Isv_min,
			    /*rank=*/ 0, /*father=*/ 0, /*pred=*/ 0,
			    /*succ=*/ 0, /*dist=*/ 0, /*callback=*/ 0,
			    /*extra=*/ 0));
    for (isvlen=0; isvlen<no_of_nodes; isvlen++) {
      if (!IGRAPH_FINITE(VECTOR(Isv_min)[isvlen])) { break; }
    }
    igraph_vector_resize(&Isv_min, isvlen);
    igraph_vector_update(Isv, &Isv_min);
    igraph_vector_destroy(&Isv_min);
    IGRAPH_FINALLY_CLEAN(1);
  }

  igraph_vector_destroy(&M);
  igraph_destroy(&domtree);
  igraph_vector_destroy(&leftout);
  igraph_destroy(&Sbar);
  igraph_vector_destroy(&Sbar_invmap);
  igraph_vector_destroy(&Sbar_map);
  IGRAPH_FINALLY_CLEAN(6);

  return 0;
}

int igraph_all_st_mincuts(const igraph_t *graph, igraph_real_t *value,
			  igraph_vector_ptr_t *cuts,
			  igraph_vector_ptr_t *partition1s,
			  igraph_integer_t source,
			  igraph_integer_t target,
			  const igraph_vector_t *capacity) {

  long int no_of_nodes=igraph_vcount(graph);
  long int no_of_edges=igraph_ecount(graph);
  igraph_vector_t flow;
  igraph_t residual;
  igraph_vector_t NtoL;
  long int newsource, newtarget;
  igraph_marked_queue_t S;
  igraph_estack_t T;
  igraph_i_all_st_mincuts_data_t pivot_data;
  igraph_vector_bool_t VE1bool;
  igraph_vector_t VE1;
  long int VE1size=0;
  long int i, nocuts;
  igraph_integer_t proj_nodes;
  igraph_vector_t revmap_ptr, revmap_next;
  igraph_vector_ptr_t closedsets;
  igraph_vector_ptr_t *mypartition1s=partition1s, vpartition1s;

  /* -------------------------------------------------------------------- */
  /* Error checks */
  if (source < 0 || source >= no_of_nodes) {
    IGRAPH_ERROR("Invalid `source' vertex", IGRAPH_EINVAL);
  }
  if (target < 0 || target >= no_of_nodes) {
    IGRAPH_ERROR("Invalid `target' vertex", IGRAPH_EINVAL);
  }
  if (source==target) {
    IGRAPH_ERROR("`source' and 'target' are the same vertex", IGRAPH_EINVAL);
  }
  
  if (!partition1s) {
    mypartition1s=&vpartition1s;
    IGRAPH_CHECK(igraph_vector_ptr_init(mypartition1s, 0));
    IGRAPH_FINALLY(igraph_vector_ptr_destroy, mypartition1s);
  }

  /* -------------------------------------------------------------------- */  
  /* We need to calculate the maximum flow first */
  IGRAPH_VECTOR_INIT_FINALLY(&flow, 0);
  IGRAPH_CHECK(igraph_maxflow(graph, value, &flow, /*cut=*/ 0, 
			      /*partition1=*/ 0, /*partition2=*/ 0, 
			      /*source=*/ source, /*target=*/ target, 
			      capacity));

  /* -------------------------------------------------------------------- */
  /* Then we need the reverse residual graph */
  IGRAPH_CHECK(igraph_reverse_residual_graph(graph, capacity, &residual,
					     &flow));
  IGRAPH_FINALLY(igraph_destroy, &residual);
  
  /* -------------------------------------------------------------------- */
  /* We shrink it to its strongly connected components */
  IGRAPH_VECTOR_INIT_FINALLY(&NtoL, 0);
  IGRAPH_CHECK(igraph_clusters(&residual, /*membership=*/ &NtoL, 
			       /*csize=*/ 0, /*no=*/ &proj_nodes, 
			       IGRAPH_STRONG));
  IGRAPH_CHECK(igraph_contract_vertices(&residual, /*mapping=*/ &NtoL, 
					/*vertex_comb=*/ 0));
  IGRAPH_CHECK(igraph_simplify(&residual, /*multiple=*/ 1, /*loops=*/ 1,
			       /*edge_comb=*/ 0));

  newsource=VECTOR(NtoL)[(long int)source];
  newtarget=VECTOR(NtoL)[(long int)target];

  /* TODO: handle the newsource == newtarget case */

  /* -------------------------------------------------------------------- */
  /* Determine the active vertices in the projection */
  IGRAPH_VECTOR_INIT_FINALLY(&VE1, 0);
  IGRAPH_CHECK(igraph_vector_bool_init(&VE1bool, proj_nodes));
  IGRAPH_FINALLY(igraph_vector_bool_destroy, &VE1bool);
  for (i=0; i<no_of_edges; i++) {
    if (VECTOR(flow)[i] > 0) {
      long int from=IGRAPH_FROM(graph, i);
      long int to=IGRAPH_TO(graph, i);
      long int pfrom=VECTOR(NtoL)[from];
      long int pto=VECTOR(NtoL)[to];
      if (!VECTOR(VE1bool)[pfrom]) { 
	VECTOR(VE1bool)[pfrom] = 1;
	VE1size++;
      }
      if (!VECTOR(VE1bool)[pto]) {
	VECTOR(VE1bool)[pto] = 1;
	VE1size++;
      }
    }
  }
  IGRAPH_CHECK(igraph_vector_reserve(&VE1, VE1size));
  for (i=0; i<proj_nodes; i++) {
    if (VECTOR(VE1bool)[i]) {
      igraph_vector_push_back(&VE1, i);
    }
  }

  if (cuts)        { igraph_vector_ptr_clear(cuts);        }
  if (partition1s) { igraph_vector_ptr_clear(partition1s); }    

  /* -------------------------------------------------------------------- */
  /* Everything is ready, list the cuts, using the right PIVOT
     function  */
  IGRAPH_CHECK(igraph_marked_queue_init(&S, no_of_nodes));
  IGRAPH_FINALLY(igraph_marked_queue_destroy, &S);
  IGRAPH_CHECK(igraph_estack_init(&T, no_of_nodes, 0));
  IGRAPH_FINALLY(igraph_estack_destroy, &T);

  pivot_data.active=&VE1bool;

  IGRAPH_CHECK(igraph_vector_ptr_init(&closedsets, 0));
  IGRAPH_FINALLY(igraph_vector_ptr_destroy, &closedsets); /* TODO */
  IGRAPH_CHECK(igraph_provan_shier_list(&residual, &S, &T, 
					newsource, newtarget, &closedsets,
					igraph_i_all_st_mincuts_pivot,
					&pivot_data));

  /* Convert the closed sets in the contracted graphs to cutsets in the
     original graph */
  IGRAPH_VECTOR_INIT_FINALLY(&revmap_ptr, igraph_vcount(&residual));
  IGRAPH_VECTOR_INIT_FINALLY(&revmap_next, no_of_nodes);
  for (i=0; i<no_of_nodes; i++) {
    long int id=VECTOR(NtoL)[i];
    VECTOR(revmap_next)[i]=VECTOR(revmap_ptr)[id];
    VECTOR(revmap_ptr)[id]=i+1;
  }
  
  nocuts=igraph_vector_ptr_size(&closedsets);
  igraph_vector_ptr_clear(mypartition1s);
  IGRAPH_CHECK(igraph_vector_ptr_reserve(mypartition1s, nocuts));
  for (i=0; i<nocuts; i++) {
    igraph_vector_t *supercut=VECTOR(closedsets)[i];
    long int j, supercutsize=igraph_vector_size(supercut);
    igraph_vector_t *cut=igraph_Calloc(1, igraph_vector_t);
    IGRAPH_VECTOR_INIT_FINALLY(cut, 0); /* TODO: better allocation */
    for (j=0; j<supercutsize; j++) {
      long int vtx=VECTOR(*supercut)[j];
      long int ovtx=VECTOR(revmap_ptr)[vtx];
      while (ovtx != 0) {
	ovtx--;
	IGRAPH_CHECK(igraph_vector_push_back(cut, ovtx));
	ovtx=VECTOR(revmap_next)[ovtx];
      }
    }
    igraph_vector_ptr_push_back(mypartition1s, cut);
    IGRAPH_FINALLY_CLEAN(1);
  }    

  igraph_vector_destroy(&revmap_next);
  igraph_vector_destroy(&revmap_ptr);
  igraph_vector_ptr_destroy(&closedsets);
  IGRAPH_FINALLY_CLEAN(3);

  if (cuts) {
    igraph_vector_long_t memb;
    IGRAPH_CHECK(igraph_vector_long_init(&memb, no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_long_destroy, &memb);
    IGRAPH_CHECK(igraph_vector_ptr_resize(cuts, nocuts));
    for (i=0; i<nocuts; i++) {
      igraph_vector_t *part=VECTOR(*mypartition1s)[i];
      long int j, n=igraph_vector_size(part);
      igraph_vector_t *v;
      v=igraph_Calloc(1, igraph_vector_t);
      if (!v) { 
	IGRAPH_ERROR("Cannot list minimum s-t cuts", IGRAPH_ENOMEM);
      }
      IGRAPH_VECTOR_INIT_FINALLY(v, 0);
      for (j=0; j<n; j++) {
	long int vtx=VECTOR(*part)[j];
	VECTOR(memb)[vtx]=i+1;
      }
      for (j=0; j<no_of_edges; j++) {
	long int from=IGRAPH_FROM(graph, j);
	long int to=IGRAPH_TO(graph, j);
	if (VECTOR(memb)[from] == i+1 && VECTOR(memb)[to] != i+1) {
	  IGRAPH_CHECK(igraph_vector_push_back(v, j)); /* TODO: allocation */
	}
      }
      VECTOR(*cuts)[i] = v;
      IGRAPH_FINALLY_CLEAN(1);
    }
    igraph_vector_long_destroy(&memb);
    IGRAPH_FINALLY_CLEAN(1);
  }
  
  igraph_estack_destroy(&T);
  igraph_marked_queue_destroy(&S);
  igraph_vector_bool_destroy(&VE1bool);
  igraph_vector_destroy(&NtoL);
  igraph_destroy(&residual);
  igraph_vector_destroy(&flow);
  IGRAPH_FINALLY_CLEAN(6);

  if (!partition1s) {
    igraph_vector_ptr_destroy(mypartition1s);
    IGRAPH_FINALLY_CLEAN(1);
  }

  return 0;
}

