// Copyright (C) 2012, 2013, 2014  David Maxwell
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include "IPTotalVariationFunctional.hh"

IPTotalVariationFunctional2S::IPTotalVariationFunctional2S(IceGrid &grid, 
  double c, double exponent, double eps, 
  IceModelVec2Int *dirichletLocations) : 
    IPFunctional<IceModelVec2S>(grid), m_dirichletIndices(dirichletLocations),
    m_c(c), m_lebesgue_exp(exponent), m_epsilon_sq(eps*eps) {
}

PetscErrorCode IPTotalVariationFunctional2S::valueAt(IceModelVec2S &x, double *OUTPUT) {

  PetscErrorCode   ierr;

  // The value of the objective
  double value = 0;

  double **x_a;
  double x_e[FEQuadrature::Nk];
  double x_q[FEQuadrature::Nq], dxdx_q[FEQuadrature::Nq], dxdy_q[FEQuadrature::Nq];
  ierr = x.get_array(x_a); CHKERRQ(ierr);

  // Jacobian times weights for quadrature.
  double JxW[FEQuadrature::Nq];
  m_quadrature.getWeightedJacobian(JxW);

  DirichletData dirichletBC;
  ierr = dirichletBC.init(m_dirichletIndices); CHKERRQ(ierr);

  // Loop through all LOCAL elements.
  int xs = m_element_index.lxs, xm = m_element_index.lxm,
           ys = m_element_index.lys, ym = m_element_index.lym;
  for (int i=xs; i<xs+xm; i++) {
    for (int j=ys; j<ys+ym; j++) {
      m_dofmap.reset(i,j,m_grid);

      // Obtain values of x at the quadrature points for the element.
      m_dofmap.extractLocalDOFs(x_a,x_e);
      if(dirichletBC) dirichletBC.updateHomogeneous(m_dofmap,x_e);
      m_quadrature.computeTrialFunctionValues(x_e,x_q,dxdx_q,dxdy_q);

      for (int q=0; q<FEQuadrature::Nq; q++) {
        value += m_c*JxW[q]*pow(m_epsilon_sq + dxdx_q[q]*dxdx_q[q] + dxdy_q[q]*dxdy_q[q],m_lebesgue_exp/2);
      } // q
    } // j
  } // i

  ierr = PISMGlobalSum(&value, OUTPUT, m_grid.com); CHKERRQ(ierr);

  ierr = dirichletBC.finish(); CHKERRQ(ierr);

  ierr = x.end_access(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode IPTotalVariationFunctional2S::gradientAt(IceModelVec2S &x, IceModelVec2S &gradient) {

  PetscErrorCode   ierr;

  // Clear the gradient before doing anything with it.
  ierr = gradient.set(0); CHKERRQ(ierr);

  double **x_a;
  double x_e[FEQuadrature::Nk];
  double x_q[FEQuadrature::Nq], dxdx_q[FEQuadrature::Nq], dxdy_q[FEQuadrature::Nq];
  ierr = x.get_array(x_a); CHKERRQ(ierr);

  double **gradient_a;
  double gradient_e[FEQuadrature::Nk];
  ierr = gradient.get_array(gradient_a); CHKERRQ(ierr);

  // An Nq by Nk array of test function values.
  const FEFunctionGerm (*test)[FEQuadrature::Nk] = m_quadrature.testFunctionValues();

  // Jacobian times weights for quadrature.
  double JxW[FEQuadrature::Nq];
  m_quadrature.getWeightedJacobian(JxW);

  DirichletData dirichletBC;
  ierr = dirichletBC.init(m_dirichletIndices); CHKERRQ(ierr);

  // Loop through all local and ghosted elements.
  int xs = m_element_index.xs, xm = m_element_index.xm,
           ys = m_element_index.ys, ym = m_element_index.ym;
  for (int i=xs; i<xs+xm; i++) {
    for (int j=ys; j<ys+ym; j++) {

      // Reset the DOF map for this element.
      m_dofmap.reset(i,j,m_grid);

      // Obtain values of x at the quadrature points for the element.
      m_dofmap.extractLocalDOFs(i,j,x_a,x_e);
      if(dirichletBC) {
        dirichletBC.constrain(m_dofmap);
        dirichletBC.updateHomogeneous(m_dofmap,x_e);
      }
      m_quadrature.computeTrialFunctionValues(x_e,x_q,dxdx_q,dxdy_q);

      // Zero out the element-local residual in prep for updating it.
      for(int k=0;k<FEQuadrature::Nk;k++){
        gradient_e[k] = 0;
      }

      for (int q=0; q<FEQuadrature::Nq; q++) {
        const double &dxdx_qq=dxdx_q[q],  &dxdy_qq=dxdy_q[q]; 
        for(int k=0; k<FEQuadrature::Nk; k++ ) {
          gradient_e[k] += m_c*JxW[q]*(m_lebesgue_exp)*pow(m_epsilon_sq + dxdx_q[q]*dxdx_q[q] + dxdy_q[q]*dxdy_q[q],m_lebesgue_exp/2-1) 
            *(dxdx_qq*test[q][k].dx + dxdy_qq*test[q][k].dy);
        } // k
      } // q
      m_dofmap.addLocalResidualBlock(gradient_e,gradient_a);
    } // j
  } // i

  ierr = dirichletBC.finish(); CHKERRQ(ierr);
  ierr = x.end_access(); CHKERRQ(ierr);
  ierr = gradient.end_access(); CHKERRQ(ierr);
  return 0;
}
