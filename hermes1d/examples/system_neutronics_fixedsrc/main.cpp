#define HERMES_REPORT_WARN
#define HERMES_REPORT_INFO
#define HERMES_REPORT_VERBOSE
#define HERMES_REPORT_FILE "application.log"
#include "hermes1d.h"

//  This example solves a 1D fixed source problem for the neutron diffusion eqn.
//  in a two-group approximation.
//  The core is composed of a single, 80cm wide slab. Reflective boundary 
//  condition is prescribed on the left end, zero-flux condition on the right end
//  (homogeneous b.c. of Neumann/Dirichlet type, respectively). There is
//  a uniform source of 1.5 fast neutrons (group 1) per cm per sec. 
//	Reference:
// 		HP-Space ADAPTATION FOR 1-D MULTIGROUP NEUTRON DIFFUSION PROBLEMS,
// 		A MSc. Thesis by YAQI WANG, Texas A&M University, 2006,
//		Example 4.A (pp. 168).
//
//  PDE: -(D1.u1')' + Sa1.u1 = Q 
//		   -(D2.u2')' + Sa2.u2 = S12.u1.
//
//  Interval: .
//
//  BC:  .
//
// Problem specification (core geometry, material properties, initial FE space).
#include "neutronics_problem_def.cpp"

// Common functions for neutronics problems (requires variable declarations from
// "neutronics_problem_def.cpp").
#include "neutronics_common.cpp"

// Weak forms for the problem (requires variable declarations from
// "neutronics_problem_def.cpp").
#include "forms.cpp"

// General input (external source problem).

bool flag = false;											// Flag for debugging purposes.
bool verbose = true;										

int N_SLN = 1;              						// Number of solutions.

// Newton's method.
double NEWTON_TOL = 1e-5;               // Tolerance.
int NEWTON_MAX_ITER = 150;              // Max. number of Newton iterations.

MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_AMESOS, SOLVER_MUMPS, SOLVER_NOX, 
                                                  // SOLVER_PARDISO, SOLVER_PETSC, SOLVER_UMFPACK.

int main() {

  // Create space.
  // Transform input data to the format used by the "Space" constructor.
  SpaceData *md = new SpaceData();
  Space* space = new Space(md->N_macroel, md->interfaces, md->poly_orders, md->material_markers, md->subdivisions, N_GRP, N_SLN);  
  delete md;
  
  // Enumerate basis functions, info for user.
  info("N_dof = %d.", space->assign_dofs());
  // Plot the space.
  space->plot("space.gp");

  for (int g = 0; g < N_GRP; g++)  {
  	space->set_bc_right_dirichlet(g, flux_right_surf[g]);
	}
  
  // Initialize the weak formulation.
  WeakForm wf(2);
  wf.add_matrix_form(0, 0, jacobian_fuel_0_0, fuel);
  wf.add_matrix_form(0, 1, jacobian_fuel_0_1, fuel);
  wf.add_matrix_form(1, 0, jacobian_fuel_1_0, fuel);    
  wf.add_matrix_form(1, 1, jacobian_fuel_1_1, fuel);
    
  wf.add_vector_form(0, residual_fuel_0, fuel);  
  wf.add_vector_form(1, residual_fuel_1, fuel); 

  wf.add_vector_form_surf(0, residual_surf_left_0, BOUNDARY_LEFT);
  wf.add_vector_form_surf(1, residual_surf_left_1, BOUNDARY_LEFT);

  // Initialize the FE problem.
  DiscreteProblem *dp = new DiscreteProblem(&wf, space);
	  	
  // Newton's loop.
  // Fill vector coeff_vec using dof and coeffs arrays in elements.
  double *coeff_vec = new double[Space::get_num_dofs(space)];
  solution_to_vector(space, coeff_vec);

  // Set up the solver, matrix, and rhs according to the solver selection.
  SparseMatrix* matrix = create_matrix(matrix_solver);
  Vector* rhs = create_vector(matrix_solver);
  Solver* solver = create_linear_solver(matrix_solver, matrix, rhs);

  int it = 1;
  while (1) {
    // Obtain the number of degrees of freedom.
    int ndof = Space::get_num_dofs(space);

    // Assemble the Jacobian matrix and residual vector.
    dp->assemble(matrix, rhs);

    // Calculate the l2-norm of residual vector.
    double res_norm_squared = 0;
    for(int i=0; i<ndof; i++) res_norm_squared += rhs->get(i)*rhs->get(i);

    // Info for user.
    info("---- Newton iter %d, residual norm: %.15f", it, sqrt(res_norm_squared));

    // If l2 norm of the residual vector is within tolerance, then quit.
    // NOTE: at least one full iteration forced
    //       here because sometimes the initial
    //       residual on fine mesh is too small.
    if(res_norm_squared < NEWTON_TOL*NEWTON_TOL && it > 1) break;

    // Multiply the residual vector with -1 since the matrix 
    // equation reads J(Y^n) \deltaY^{n+1} = -F(Y^n).
    for(int i=0; i<ndof; i++) rhs->set(i, -rhs->get(i));

    // Solve the linear system.
    if(!solver->solve())
      error ("Matrix solver failed.\n");

    // Add \deltaY^{n+1} to Y^n.
    for (int i = 0; i < ndof; i++) coeff_vec[i] += solver->get_solution()[i];

    // If the maximum number of iteration has been reached, then quit.
    if (it >= NEWTON_MAX_ITER) error ("Newton method did not converge.");
    
    // Copy coefficients from vector y to elements.
    vector_to_solution(coeff_vec, space);

    it++;
  }
  
  // Plot the solution.
  Linearizer l(space);
  l.plot_solution("solution.gp");

	// Calculate flux integral for comparison with the reference value.
	double I = calc_integrated_flux(space, 1, 60., 80.);
	double Iref = 134.9238787715397;
	info("I = %.13f, err = %.13f%%", I, 100.*(I - Iref)/Iref );
	
  info("Done.");
  return 1;
}
