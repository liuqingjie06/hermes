// This file is part of Hermes2D.
//
// Hermes2D is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// Hermes2D is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Hermes2D.  If not, see <http://www.gnu.org/licenses/>.

#include "discrete_problem/discrete_problem_selective_assembler.h"
#include "neighbor.h"

namespace Hermes
{
  namespace Hermes2D
  {

    template<typename Scalar>
    DiscreteProblemSelectiveAssembler<Scalar>::DiscreteProblemSelectiveAssembler()
    : sp_seq(NULL), 
      spaces_size(0), 
      mfvol_forms_size(0), 
      vfvol_forms_size(0), 
      mfsurf_forms_size(0), 
      vfsurf_forms_size(0), 
      surface_markers_size(0), 
      volume_markers_size(0), 
      matrix_structure_reusable(false), 
      vector_structure_reusable(false)
    {
        this->matrix_surface_recalculation = NULL;
        this->vector_surface_recalculation = NULL;
        this->matrix_surface_forms_recalculation = NULL;
        this->vector_surface_forms_recalculation = NULL;

        this->matrix_volume_recalculation = NULL;
        this->vector_volume_recalculation = NULL;
        this->matrix_volume_forms_recalculation = NULL;
        this->vector_volume_forms_recalculation = NULL;
    }

    template<typename Scalar>
    DiscreteProblemSelectiveAssembler<Scalar>::~DiscreteProblemSelectiveAssembler()
    {
      if(sp_seq)
        delete [] sp_seq;
    }

    template<typename Scalar>
    void DiscreteProblemSelectiveAssembler<Scalar>::prepare_sparse_structure(SparseMatrix<Scalar>* mat, Vector<Scalar>* rhs, Hermes::vector<SpaceSharedPtr<Scalar> >& spaces, Traverse::State**& states, int& num_states)
    {
      int ndof = Space<Scalar>::get_num_dofs(spaces);
      
      if(matrix_structure_reusable && mat)
      {
        mat->zero();
      }

      if(vector_structure_reusable && rhs)
      {
        if(rhs->length() == 0)
          rhs->alloc(ndof);
        else
          rhs->zero();
      }

      if(!matrix_structure_reusable && mat)
      {
        // Spaces have changed: create the matrix from scratch.
        matrix_structure_reusable = true;
        mat->free();
        mat->prealloc(ndof);

        AsmList<Scalar>* al = new AsmList<Scalar>[spaces_size];
        bool **blocks = wf->get_blocks(this->force_diagonal_blocks);
        
        // Loop through all elements.
        for(int state_i = 0; state_i < num_states; state_i++)
        {
          Traverse::State* current_state = states[state_i];

          // Obtain assembly lists for the element at all spaces.
          /// \todo do not get the assembly list again if the element was not changed.
          for (unsigned int i = 0; i < spaces_size; i++)
            if(current_state->e[i])
              spaces[i]->get_element_assembly_list(current_state->e[i], &(al[i]));

          if(this->wf->is_DG())
          {
            // Number of edges ( =  number of vertices).
            int num_edges = current_state->e[0]->nvert;

            // Allocation an array of arrays of neighboring elements for every mesh x edge.
            Element **** neighbor_elems_arrays = new Element ***[spaces_size];
            for(unsigned int i = 0; i < spaces_size; i++)
              neighbor_elems_arrays[i] = new Element **[num_edges];

            // The same, only for number of elements
            int ** neighbor_elems_counts = new int *[spaces_size];
            for(unsigned int i = 0; i < spaces_size; i++)
              neighbor_elems_counts[i] = new int[num_edges];

            // Get the neighbors.
            for(unsigned int el = 0; el < spaces_size; el++)
            {
              NeighborSearch<Scalar> ns(current_state->e[el], spaces[el]->get_mesh());

              // Ignoring errors (and doing nothing) in case the edge is a boundary one.
              ns.set_ignore_errors(true);

              for(int ed = 0; ed < num_edges; ed++)
              {
                ns.set_active_edge(ed);
                const Hermes::vector<Element *> *neighbors = ns.get_neighbors();

                neighbor_elems_counts[el][ed] = ns.get_num_neighbors();
                neighbor_elems_arrays[el][ed] = new Element *[neighbor_elems_counts[el][ed]];
                for(int neigh = 0; neigh < neighbor_elems_counts[el][ed]; neigh++)
                  neighbor_elems_arrays[el][ed][neigh] = (*neighbors)[neigh];
              }
            }

            // Pre-add into the stiffness matrix.
            for (unsigned int m = 0; m < spaces_size; m++)
            {
              for(unsigned int el = 0; el < spaces_size; el++)
              {
                for(int ed = 0; ed < num_edges; ed++)
                {
                  for(int neigh = 0; neigh < neighbor_elems_counts[el][ed]; neigh++)
                  {
                    if((blocks[m][el] || blocks[el][m]) && current_state->e[m])
                    {
                      AsmList<Scalar>*am = &(al[m]);
                      AsmList<Scalar>*an = new AsmList<Scalar>;
                      spaces[el]->get_element_assembly_list(neighbor_elems_arrays[el][ed][neigh], an);

                      // pretend assembling of the element stiffness matrix
                      // register nonzero elements
                      for (unsigned int i = 0; i < am->cnt; i++)
                      {
                        if(am->dof[i] >= 0)
                        {
                          for (unsigned int j = 0; j < an->cnt; j++)
                          {
                            if(an->dof[j] >= 0)
                            {
                              if(blocks[m][el]) mat->pre_add_ij(am->dof[i], an->dof[j]);
                              if(blocks[el][m]) mat->pre_add_ij(an->dof[j], am->dof[i]);
                            }
                            delete an;
                          }
                        }
                      }
                    }

                    // Deallocation an array of arrays of neighboring elements
                    // for every mesh x edge.
                    for(unsigned int el = 0; el < spaces_size; el++)
                    {
                      for(int ed = 0; ed < num_edges; ed++)
                        delete [] neighbor_elems_arrays[el][ed];
                      delete [] neighbor_elems_arrays[el];
                    }
                    delete [] neighbor_elems_arrays;

                    // The same, only for number of elements.
                    for(unsigned int el = 0; el < spaces_size; el++)
                      delete [] neighbor_elems_counts[el];
                    delete [] neighbor_elems_counts;
                  }
                }
              }
            }
          }

          // Go through all equation-blocks of the local stiffness matrix.
          for (unsigned int m = 0; m < spaces_size; m++)
          {
            for (unsigned int n = 0; n < spaces_size; n++)
            {
              if(blocks[m][n] && current_state->e[m] && current_state->e[n])
              {
                AsmList<Scalar>*am = &(al[m]);
                AsmList<Scalar>*an = &(al[n]);

                // Pretend assembling of the element stiffness matrix.
                for (unsigned int i = 0; i < am->cnt; i++)
                {
                  if(am->dof[i] >= 0)
                    for (unsigned int j = 0; j < an->cnt; j++)
                      if(an->dof[j] >= 0)
                        mat->pre_add_ij(am->dof[i], an->dof[j]);
                }
              }
            }
          }
        }

        delete [] al;
        delete [] blocks;

        mat->alloc();
      }

      // WARNING: unlike Matrix<Scalar>::alloc(), Vector<Scalar>::alloc(ndof) frees the memory occupied
      // by previous vector before allocating
      if(!vector_structure_reusable && rhs)
      {
        vector_structure_reusable = true;
        rhs->alloc(ndof);
      }
    }

    template<typename Scalar>
    void DiscreteProblemSelectiveAssembler<Scalar>::set_spaces(Hermes::vector<SpaceSharedPtr<Scalar> >& spacesToSet)
    {
      if(!sp_seq)
      {
        // Internal variables settings.
        this->spaces_size = spacesToSet.size();
        sp_seq = new int[spaces_size];
        memset(sp_seq, -1, sizeof(int) * spaces_size);
      }
      else
      {
        for(unsigned int i = 0; i < spaces_size; i++)
        {
          int new_sp_seq = spacesToSet[i]->get_seq();

          if(new_sp_seq != sp_seq[i])
          {
            matrix_structure_reusable = false;
            vector_structure_reusable = false;
          }
          sp_seq[i] = new_sp_seq;
        }
      }

      if(spacesToSet[0]->get_mesh()->get_boundary_markers_conversion().min_marker_unused != surface_markers_size)
      {
        surface_markers_size = spacesToSet[0]->get_mesh()->get_boundary_markers_conversion().min_marker_unused;
        
        if(this->matrix_surface_recalculation)
          free(this->matrix_surface_recalculation);
        
        this->matrix_surface_recalculation = (bool*)calloc(surface_markers_size, sizeof(bool));
        
        if(this->vector_surface_recalculation)
          free(this->vector_surface_recalculation);
        
        this->vector_surface_recalculation = (bool*)calloc(surface_markers_size, sizeof(bool));


        if(this->matrix_surface_forms_recalculation)
          free(this->matrix_surface_forms_recalculation);
        
        this->matrix_surface_forms_recalculation = (bool**)calloc(surface_markers_size, sizeof(bool*));
        if(this->mfsurf_forms_size > 0)
        {
          for(int i = 0; i < this->surface_markers_size; i++)
          {
            if(matrix_surface_forms_recalculation[i])
              free(matrix_surface_forms_recalculation[i]);

            matrix_surface_forms_recalculation[i] = (bool*)calloc(this->mfsurf_forms_size, sizeof(bool));
          }
        }

        if(this->vector_surface_forms_recalculation)
          free(this->vector_surface_forms_recalculation);
        
        this->vector_surface_forms_recalculation = (bool**)calloc(surface_markers_size, sizeof(bool*));
        if(this->vfsurf_forms_size > 0)
        {
          for(int i = 0; i < this->surface_markers_size; i++)
          {
            if(vector_surface_forms_recalculation[i])
              free(vector_surface_forms_recalculation[i]);

            vector_surface_forms_recalculation[i] = (bool*)calloc(this->vfsurf_forms_size, sizeof(bool));
          }
        }
      }

      if(spacesToSet[0]->get_mesh()->get_boundary_markers_conversion().min_marker_unused != volume_markers_size)
      {
        volume_markers_size = spacesToSet[0]->get_mesh()->get_boundary_markers_conversion().min_marker_unused;
        
        if(this->matrix_volume_recalculation)
          free(this->matrix_volume_recalculation);
        
        this->matrix_volume_recalculation = (bool*)calloc(volume_markers_size, sizeof(bool));
        
        if(this->vector_volume_recalculation)
          free(this->vector_volume_recalculation);
        
        this->vector_volume_recalculation = (bool*)calloc(volume_markers_size, sizeof(bool));


        if(this->matrix_volume_forms_recalculation)
          free(this->matrix_volume_forms_recalculation);
        
        this->matrix_volume_forms_recalculation = (bool**)calloc(volume_markers_size, sizeof(bool*));
        if(this->mfvol_forms_size > 0)
        {
          for(int i = 0; i < this->volume_markers_size; i++)
          {
            if(matrix_volume_forms_recalculation[i])
              free(matrix_volume_forms_recalculation[i]);

            matrix_volume_forms_recalculation[i] = (bool*)calloc(this->mfvol_forms_size, sizeof(bool));
          }
        }

        if(this->vector_volume_forms_recalculation)
          free(this->vector_volume_forms_recalculation);
        
        this->vector_volume_forms_recalculation = (bool**)calloc(volume_markers_size, sizeof(bool*));
        if(this->vfvol_forms_size > 0)
        {
          for(int i = 0; i < this->volume_markers_size; i++)
          {
            if(vector_volume_forms_recalculation[i])
              free(vector_volume_forms_recalculation[i]);

            vector_volume_forms_recalculation[i] = (bool*)calloc(this->vfvol_forms_size, sizeof(bool));
          }
        }
      }
    }

    template<typename Scalar>
    void DiscreteProblemSelectiveAssembler<Scalar>::set_weak_formulation(WeakForm<Scalar>* wf_)
    {
      Mixins::DiscreteProblemWeakForm<Scalar>::set_weak_formulation(wf_);

      this->matrix_structure_reusable = false;
      this->vector_structure_reusable = false;

      if(spaces_size == 0)
        return;

      if(this->wf->mfvol.size() != this->mfvol_forms_size)
      {
        this->mfvol_forms_size = this->wf->mfvol.size();
        for(int i = 0; i < this->volume_markers_size; i++)
        {
          if(matrix_volume_forms_recalculation[i])
            free(matrix_volume_forms_recalculation[i]);
          matrix_volume_forms_recalculation[i] = (bool*)calloc(this->mfvol_forms_size, sizeof(bool));
        }
      }

      if(this->wf->vfvol.size() != this->vfvol_forms_size)
      {
        this->vfvol_forms_size = this->wf->vfvol.size();
        for(int i = 0; i < this->volume_markers_size; i++)
        {
          if(vector_volume_forms_recalculation[i])
            free(vector_volume_forms_recalculation[i]);
          vector_volume_forms_recalculation[i] = (bool*)calloc(this->vfvol_forms_size, sizeof(bool));
        }
      }

      if(this->wf->mfsurf.size() != this->mfsurf_forms_size)
      {
        this->mfsurf_forms_size = this->wf->mfsurf.size();
        for(int i = 0; i < this->surface_markers_size; i++)
        {
          if(matrix_surface_forms_recalculation[i])
            free(matrix_surface_forms_recalculation[i]);
          matrix_surface_forms_recalculation[i] = (bool*)calloc(this->mfsurf_forms_size, sizeof(bool));
        }
      }

      if(this->wf->vfsurf.size() != this->vfsurf_forms_size)
      {
        this->vfsurf_forms_size = this->wf->vfsurf.size();
        for(int i = 0; i < this->surface_markers_size; i++)
        {
          if(vector_surface_forms_recalculation[i])
            free(vector_surface_forms_recalculation[i]);
          vector_surface_forms_recalculation[i] = (bool*)calloc(this->vfsurf_forms_size, sizeof(bool));
        }
      }
    }

    template<typename Scalar>
    bool DiscreteProblemSelectiveAssembler<Scalar>::form_to_be_assembled(MatrixForm<Scalar>* form, Traverse::State* current_state)
    {
      if(current_state->e[form->i] && current_state->e[form->j])
      {
        if(fabs(form->scaling_factor) < 1e-12)
          return false;

        // If a block scaling table is provided, and if the scaling coefficient
        // A_mn for this block is zero, then the form does not need to be assembled.
        if(this->block_weights)
          if(fabs(this->block_weights->get_A(form->i, form->j)) < 1e-12)
            return false;
        return true;
      }
      return false;
    }

    template<typename Scalar>
    bool DiscreteProblemSelectiveAssembler<Scalar>::form_to_be_assembled(MatrixFormVol<Scalar>* form, Traverse::State* current_state)
    {
      if(!form_to_be_assembled((MatrixForm<Scalar>*)form, current_state))
        return false;

      if(form->assembleEverywhere)
        return true;

      int this_marker = current_state->rep->marker;
      for (unsigned int ss = 0; ss < form->areas_internal.size(); ss++)
        if(form->areas_internal[ss] == this_marker)
          return true;

      return false;
    }

    template<typename Scalar>
    bool DiscreteProblemSelectiveAssembler<Scalar>::form_to_be_assembled(MatrixFormSurf<Scalar>* form, Traverse::State* current_state)
    {
      if(!form_to_be_assembled((MatrixForm<Scalar>*)form, current_state))
        return false;

      if(current_state->rep->en[current_state->isurf]->marker == 0)
        return false;

      if(form->assembleEverywhere)
        return true;

      int this_marker = current_state->rep->en[current_state->isurf]->marker;
      for (unsigned int ss = 0; ss < form->areas_internal.size(); ss++)
        if(form->areas_internal[ss] == this_marker)
          return true;

      return false;
    }

    template<typename Scalar>
    bool DiscreteProblemSelectiveAssembler<Scalar>::form_to_be_assembled(MatrixFormDG<Scalar>* form, Traverse::State* current_state)
    {
      return form_to_be_assembled((MatrixForm<Scalar>*)form, current_state);
    }

    template<typename Scalar>
    bool DiscreteProblemSelectiveAssembler<Scalar>::form_to_be_assembled(VectorForm<Scalar>* form, Traverse::State* current_state)
    {
      if(!current_state->e[form->i])
        return false;
      if(fabs(form->scaling_factor) < 1e-12)
        return false;

      return true;
    }

    template<typename Scalar>
    bool DiscreteProblemSelectiveAssembler<Scalar>::form_to_be_assembled(VectorFormVol<Scalar>* form, Traverse::State* current_state)
    {
      if(!form_to_be_assembled((VectorForm<Scalar>*)form, current_state))
        return false;

      if(form->assembleEverywhere)
        return true;

      int this_marker = current_state->rep->marker;
      for (unsigned int ss = 0; ss < form->areas_internal.size(); ss++)
        if(form->areas_internal[ss] == this_marker)
          return true;

      return false;
    }

    template<typename Scalar>
    bool DiscreteProblemSelectiveAssembler<Scalar>::form_to_be_assembled(VectorFormSurf<Scalar>* form, Traverse::State* current_state)
    {
      if(!form_to_be_assembled((VectorForm<Scalar>*)form, current_state))
        return false;

      if(current_state->rep->en[current_state->isurf]->marker == 0)
        return false;

      if(form->assembleEverywhere)
        return true;

      int this_marker = current_state->rep->en[current_state->isurf]->marker;
      for (unsigned int ss = 0; ss < form->areas_internal.size(); ss++)
        if(form->areas_internal[ss] == this_marker)
          return true;

      return false;
    }

    template<typename Scalar>
    bool DiscreteProblemSelectiveAssembler<Scalar>::form_to_be_assembled(VectorFormDG<Scalar>* form, Traverse::State* current_state)
    {
      return form_to_be_assembled((VectorForm<Scalar>*)form, current_state);
    }

    template class HERMES_API DiscreteProblemSelectiveAssembler<double>;
    template class HERMES_API DiscreteProblemSelectiveAssembler<std::complex<double> >;
  }
}