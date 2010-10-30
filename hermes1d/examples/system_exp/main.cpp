#define HERMES_REPORT_WARN
#define HERMES_REPORT_INFO
#define HERMES_REPORT_VERBOSE
#define HERMES_REPORT_FILE "application.log"
#include "hermes1d.h"

//  This example solves a system of two linear second-order equations.
//
//  PDE: - u'' + v - f_0 = 0
//       - v'' + u - f_1 = 0.
//
//  Interval: (A, B).
//
//  BC: Dirichlet, u(A) = exp(A), u(B) = exp(B), v(A) = exp(-A), v(B) = exp(-B).
//
//  Exact solution: u(x) = exp(x), v(x) = exp(-x).
//
//  The following parameters can be changed:
const int NEQ = 2;                      // Number of equations.
const int NELEM = 2;                    // Number of elements.
const double A = 0, B = 1;              // Domain end points.
const int P_INIT = 2;                   // Polynomial degree.

// Newton's method.
double NEWTON_TOL = 1e-5;               // Tolerance.
int NEWTON_MAX_ITER = 150;              // Max. number of Newton iterations.

MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_AMESOS, SOLVER_MUMPS, SOLVER_NOX, 
                                                  // SOLVER_PARDISO, SOLVER_PETSC, SOLVER_UMFPACK.

// Boundary conditions.
double Val_dir_left_0 = exp(A);
double Val_dir_right_0 = exp(B);
double Val_dir_left_1 = exp(-A);
double Val_dir_right_1 = exp(-B);

// Function f_0(x).
double f_0(double x) {
  return -exp(x) + exp(-x);
}

// Function f_1(x).
double f_1(double x) {
  return -exp(-x) + exp(x);
}

// Weak forms for Jacobi matrix and residual.
#include "forms.cpp"


int main() {
  // Create space, set Dirichlet BC, enumerate basis functions.
  Space* space = new Space(A, B, NELEM, P_INIT, NEQ);
  space->set_bc_left_dirichlet(0, Val_dir_left_0);
  space->set_bc_right_dirichlet(0, Val_dir_right_0);
  space->set_bc_left_dirichlet(1, Val_dir_left_1);
  space->set_bc_right_dirichlet(1, Val_dir_right_1);
  info("N_dof = %d.", space->assign_dofs());

  // Initialize the weak formulation.
  WeakForm wf(2);
  wf.add_matrix_form(0, 0, jacobian_0_0);
  wf.add_matrix_form(0, 1, jacobian_0_1);
  wf.add_matrix_form(1, 0, jacobian_1_0);
  wf.add_matrix_form(1, 1, jacobian_1_1);
  wf.add_vector_form(0, residual_0);
  wf.add_vector_form(1, residual_1);

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

  // Plot the resulting space.
  space->plot("space.gp");

  info("Done.");
  return 1;
}

